// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/osr/osr_video_consumer.h"

#include <utility>

#include "media/base/limits.h"
#include "media/base/video_frame_metadata.h"
#include "media/capture/mojom/video_capture_buffer.mojom.h"
#include "media/capture/mojom/video_capture_types.mojom.h"
#include "services/viz/privileged/mojom/compositing/frame_sink_video_capture.mojom-shared.h"
#include "shell/browser/osr/osr_render_widget_host_view.h"
#include "ui/gfx/skbitmap_operations.h"

namespace {

bool IsValidMinAndMaxFrameSize(gfx::Size min_frame_size,
                               gfx::Size max_frame_size) {
  // Returns true if
  // 0 < |min_frame_size| <= |max_frame_size| <= media::limits::kMaxDimension.
  return 0 < min_frame_size.width() && 0 < min_frame_size.height() &&
         min_frame_size.width() <= max_frame_size.width() &&
         min_frame_size.height() <= max_frame_size.height() &&
         max_frame_size.width() <= media::limits::kMaxDimension &&
         max_frame_size.height() <= media::limits::kMaxDimension;
}

}  // namespace

namespace electron {

OffScreenVideoConsumer::OffScreenVideoConsumer(
    OffScreenRenderWidgetHostView* view,
    OnPaintCallback callback)
    : callback_(callback),
      view_(view),
      video_capturer_(view->CreateVideoCapturer()) {
  video_capturer_->SetAutoThrottlingEnabled(false);
  video_capturer_->SetMinSizeChangePeriod(base::TimeDelta());
  video_capturer_->SetFormat(media::PIXEL_FORMAT_ARGB);

  SizeChanged(view_->SizeInPixels());
  SetFrameRate(view_->frame_rate());
}

OffScreenVideoConsumer::~OffScreenVideoConsumer() = default;

void OffScreenVideoConsumer::SetActive(bool active) {
  if (active) {
    video_capturer_->Start(this, viz::mojom::BufferFormatPreference::kDefault);
  } else {
    video_capturer_->Stop();
  }
}

void OffScreenVideoConsumer::SetFrameRate(int frame_rate) {
  video_capturer_->SetMinCapturePeriod(base::Seconds(1) / frame_rate);
}

void OffScreenVideoConsumer::SizeChanged(const gfx::Size& size_in_pixels) {
  DCHECK(IsValidMinAndMaxFrameSize(size_in_pixels, size_in_pixels));
  video_capturer_->SetResolutionConstraints(size_in_pixels, size_in_pixels,
                                            true);
  video_capturer_->RequestRefreshFrame();
}

void OffScreenVideoConsumer::OnFrameCaptured(
    ::media::mojom::VideoBufferHandlePtr data,
    ::media::mojom::VideoFrameInfoPtr info,
    const gfx::Rect& content_rect,
    mojo::PendingRemote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks>
        callbacks) {
  auto& data_region = data->get_read_only_shmem_region();

  if (!CheckContentRect(content_rect)) {
    SizeChanged(view_->SizeInPixels());
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
    callbacks_remote->Done();
    return;
  }
  if (mapping.size() <
      media::VideoFrame::AllocationSize(info->pixel_format, info->coded_size)) {
    DLOG(ERROR) << "Shared memory size was less than expected.";
    callbacks_remote->Done();
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
    mojo::PendingRemote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks>
        releaser;
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
      new FramePinner{std::move(mapping), callbacks_remote.Unbind()});
  bitmap.setImmutable();

  std::optional<gfx::Rect> update_rect = info->metadata.capture_update_rect;
  if (!update_rect.has_value() || update_rect->IsEmpty()) {
    update_rect = content_rect;
  }

  callback_.Run(*update_rect, bitmap);
}

void OffScreenVideoConsumer::OnNewSubCaptureTargetVersion(
    uint32_t crop_version) {}

void OffScreenVideoConsumer::OnFrameWithEmptyRegionCapture() {}

void OffScreenVideoConsumer::OnStopped() {}

void OffScreenVideoConsumer::OnLog(const std::string& message) {}

bool OffScreenVideoConsumer::CheckContentRect(const gfx::Rect& content_rect) {
  gfx::Size view_size = view_->SizeInPixels();
  gfx::Size content_size = content_rect.size();

  if (std::abs(view_size.width() - content_size.width()) > 2) {
    return false;
  }

  if (std::abs(view_size.height() - content_size.height()) > 2) {
    return false;
  }

  return true;
}

}  // namespace electron
