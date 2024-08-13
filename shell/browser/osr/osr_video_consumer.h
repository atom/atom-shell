// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_OSR_OSR_VIDEO_CONSUMER_H_
#define ELECTRON_SHELL_BROWSER_OSR_OSR_VIDEO_CONSUMER_H_

#include <memory>
#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/viz/host/client_frame_sink_video_capturer.h"
#include "media/capture/mojom/video_capture_buffer.mojom-forward.h"
#include "media/capture/mojom/video_capture_types.mojom.h"

namespace electron {

class OffScreenRenderWidgetHostView;

typedef base::RepeatingCallback<void(const gfx::Rect&, const SkBitmap&)>
    OnPaintCallback;

class OffScreenVideoConsumer : public viz::mojom::FrameSinkVideoConsumer {
 public:
  OffScreenVideoConsumer(OffScreenRenderWidgetHostView* view,
                         OnPaintCallback callback);
  ~OffScreenVideoConsumer() override;

  // disable copy
  OffScreenVideoConsumer(const OffScreenVideoConsumer&) = delete;
  OffScreenVideoConsumer& operator=(const OffScreenVideoConsumer&) = delete;

  void SetActive(bool active);
  void SetFrameRate(int frame_rate);
  void SizeChanged(const gfx::Size& size_in_pixels);

 private:
  // viz::mojom::FrameSinkVideoConsumer implementation.
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

  bool CheckContentRect(const gfx::Rect& content_rect);

  OnPaintCallback callback_;

  raw_ptr<OffScreenRenderWidgetHostView> view_;
  std::unique_ptr<viz::ClientFrameSinkVideoCapturer> video_capturer_;

  base::WeakPtrFactory<OffScreenVideoConsumer> weak_ptr_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_OSR_OSR_VIDEO_CONSUMER_H_
