// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_FRAME_SUBSCRIBER_H_
#define ELECTRON_SHELL_BROWSER_API_FRAME_SUBSCRIBER_H_

#include <memory>
#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/viz/host/client_frame_sink_video_capturer.h"
#include "content/public/browser/web_contents_observer.h"
#include "media/capture/mojom/video_capture_buffer.mojom-forward.h"
#include "v8/include/v8-forward.h"

namespace gfx {
class Image;
class Rect;
}  // namespace gfx

namespace mojo {
template <typename T>
class PendingRemote;
}  // namespace mojo

namespace electron::api {

class WebContents;

class FrameSubscriber : private content::WebContentsObserver,
                        private viz::mojom::FrameSinkVideoConsumer {
 public:
  using FrameCaptureCallback =
      base::RepeatingCallback<void(const gfx::Image&, const gfx::Rect&)>;

  FrameSubscriber(content::WebContents* web_contents,
                  const FrameCaptureCallback& callback,
                  bool only_dirty);
  ~FrameSubscriber() override;

  // disable copy
  FrameSubscriber(const FrameSubscriber&) = delete;
  FrameSubscriber& operator=(const FrameSubscriber&) = delete;

 private:
  void AttachToHost(content::RenderWidgetHost* host);
  void DetachFromHost();

  // content::WebContentsObserver
  void RenderFrameCreated(content::RenderFrameHost* render_frame_host) override;
  void PrimaryPageChanged(content::Page& page) override;
  void RenderViewDeleted(content::RenderViewHost* host) override;

  // viz::mojom::FrameSinkVideoConsumer
  void OnFrameCaptured(
      ::media::mojom::VideoBufferHandlePtr data,
      ::media::mojom::VideoFrameInfoPtr info,
      const gfx::Rect& content_rect,
      mojo::PendingRemote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks>
          callbacks) override;
  void OnNewSubCaptureTargetVersion(uint32_t crop_version) override {}
  void OnFrameWithEmptyRegionCapture() override {}
  void OnStopped() override {}
  void OnLog(const std::string& message) override {}

  void Done(const gfx::Rect& damage, const SkBitmap& frame);

  // Get the pixel size of render view.
  gfx::Size GetRenderViewSize() const;

  FrameCaptureCallback callback_;
  bool only_dirty_;

  raw_ptr<content::RenderWidgetHost> host_;
  std::unique_ptr<viz::ClientFrameSinkVideoCapturer> video_capturer_;

  base::WeakPtrFactory<FrameSubscriber> weak_ptr_factory_{this};
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_FRAME_SUBSCRIBER_H_
