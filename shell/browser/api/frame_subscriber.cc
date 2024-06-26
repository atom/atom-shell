// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/frame_subscriber.h"

#include <utility>

#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "media/capture/mojom/video_capture_buffer.mojom.h"
#include "media/capture/mojom/video_capture_types.mojom.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/viz/privileged/mojom/compositing/frame_sink_video_capture.mojom-shared.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/skbitmap_operations.h"

namespace electron::api {

constexpr static int kMaxFrameRate = 30;

FrameSubscriber::FrameSubscriber(content::WebContents* web_contents,
                                 const FrameCaptureCallback& callback,
                                 bool only_dirty)
    : content::WebContentsObserver(web_contents),
      callback_(callback),
      only_dirty_(only_dirty) {
  AttachToHost(web_contents->GetPrimaryMainFrame()->GetRenderWidgetHost());
}

FrameSubscriber::~FrameSubscriber() = default;

void FrameSubscriber::AttachToHost(content::RenderWidgetHost* host) {
  host_ = host;

  // The view can be null if the renderer process has crashed.
  // (https://crbug.com/847363)
  if (!host_->GetView())
    return;

  // Create and configure the video capturer.
  gfx::Size size = GetRenderViewSize();
  DCHECK(!size.IsEmpty());
  video_capturer_ = host_->GetView()->CreateVideoCapturer();
  video_capturer_->SetResolutionConstraints(size, size, true);
  video_capturer_->SetAutoThrottlingEnabled(false);
  video_capturer_->SetMinSizeChangePeriod(base::TimeDelta());
  video_capturer_->SetFormat(media::PIXEL_FORMAT_ARGB);
  video_capturer_->SetMinCapturePeriod(base::Seconds(1) / kMaxFrameRate);
  video_capturer_->Start(this, viz::mojom::BufferFormatPreference::kDefault);
}

void FrameSubscriber::DetachFromHost() {
  if (!host_)
    return;
  video_capturer_.reset();
  host_ = nullptr;
}

void FrameSubscriber::RenderFrameCreated(
    content::RenderFrameHost* render_frame_host) {
  if (!host_)
    AttachToHost(render_frame_host->GetRenderWidgetHost());
}

void FrameSubscriber::RenderViewDeleted(content::RenderViewHost* host) {
  if (host->GetWidget() == host_) {
    DetachFromHost();
  }
}

void FrameSubscriber::PrimaryPageChanged(content::Page& page) {
  if (auto* host = page.GetMainDocument().GetMainFrame()->GetRenderWidgetHost();
      host_ != host) {
    DetachFromHost();
    AttachToHost(host);
  }
}

void FrameSubscriber::OnFrameCaptured(
    ::media::mojom::VideoBufferHandlePtr data,
    ::media::mojom::VideoFrameInfoPtr info,
    const gfx::Rect& content_rect,
    mojo::PendingRemote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks>
        callbacks) {
  auto& data_region = data->get_read_only_shmem_region();

  gfx::Size size = GetRenderViewSize();
  if (size != content_rect.size()) {
    video_capturer_->SetResolutionConstraints(size, size, true);
    video_capturer_->RequestRefreshFrame();
    return;
  }

  mojo::Remote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks>
      callbacks_remote(std::move(callbacks));
  if (!data_region.IsValid()) {
    callbacks_remote->Done();
    return;
  }
  base::ReadOnlySharedMemoryMapping mapping = data_region.Map();
  if (!mapping.IsValid()) {
    DLOG(ERROR) << "Shared memory mapping failed.";
    return;
  }
  if (mapping.size() <
      media::VideoFrame::AllocationSize(info->pixel_format, info->coded_size)) {
    DLOG(ERROR) << "Shared memory size was less than expected.";
    return;
  }

  // The SkBitmap's pixels will be marked as immutable, but the installPixels()
  // API requires a non-const pointer. So, cast away the const.
  void* const pixels = const_cast<void*>(mapping.memory());

  // Call installPixels() with a |releaseProc| that: 1) notifies the capturer
  // that this consumer has finished with the frame, and 2) releases the shared
  // memory mapping.
  struct FramePinner {
    // Keeps the shared memory that backs |frame_| mapped.
    base::ReadOnlySharedMemoryMapping mapping;
    // Prevents FrameSinkVideoCapturer from recycling the shared memory that
    // backs |frame_|.
    mojo::Remote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks> releaser;
  };

  SkBitmap bitmap;
  bitmap.installPixels(
      SkImageInfo::MakeN32(content_rect.width(), content_rect.height(),
                           kPremul_SkAlphaType),
      pixels,
      media::VideoFrame::RowBytes(media::VideoFrame::Plane::kARGB,
                                  info->pixel_format, info->coded_size.width()),
      [](void* addr, void* context) {
        delete static_cast<FramePinner*>(context);
      },
      new FramePinner{std::move(mapping), std::move(callbacks_remote)});
  bitmap.setImmutable();

  Done(content_rect, bitmap);
}

void FrameSubscriber::OnNewSubCaptureTargetVersion(uint32_t crop_version) {}

void FrameSubscriber::OnFrameWithEmptyRegionCapture() {}

void FrameSubscriber::OnStopped() {}

void FrameSubscriber::OnLog(const std::string& message) {}

void FrameSubscriber::Done(const gfx::Rect& damage, const SkBitmap& frame) {
  if (frame.drawsNothing())
    return;

  const SkBitmap& bitmap = only_dirty_ ? SkBitmapOperations::CreateTiledBitmap(
                                             frame, damage.x(), damage.y(),
                                             damage.width(), damage.height())
                                       : frame;

  // Copying SkBitmap does not copy the internal pixels, we have to manually
  // allocate and write pixels otherwise crash may happen when the original
  // frame is modified.
  SkBitmap copy;
  copy.allocPixels(SkImageInfo::Make(bitmap.width(), bitmap.height(),
                                     kN32_SkColorType, kPremul_SkAlphaType));
  SkPixmap pixmap;
  bool success = bitmap.peekPixels(&pixmap) && copy.writePixels(pixmap, 0, 0);
  CHECK(success);

  callback_.Run(gfx::Image::CreateFrom1xBitmap(copy), damage);
}

gfx::Size FrameSubscriber::GetRenderViewSize() const {
  content::RenderWidgetHostView* view = host_->GetView();
  gfx::Size size = view->GetViewBounds().size();
  return gfx::ToRoundedSize(
      gfx::ScaleSize(gfx::SizeF(size), view->GetDeviceScaleFactor()));
}

}  // namespace electron::api
