// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#ifndef ELECTRON_SHELL_BROWSER_MEDIA_MEDIA_CAPTURE_DEVICES_DISPATCHER_H_
#define ELECTRON_SHELL_BROWSER_MEDIA_MEDIA_CAPTURE_DEVICES_DISPATCHER_H_

#include "components/webrtc/media_stream_device_enumerator_impl.h"
#include "content/public/browser/media_observer.h"
#include "content/public/browser/media_stream_request.h"
#include "third_party/blink/public/common/mediastream/media_stream_request.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"

namespace electron {

// This singleton is used to receive updates about media events from the content
// layer.
class MediaCaptureDevicesDispatcher
    : public content::MediaObserver,
      public webrtc::MediaStreamDeviceEnumeratorImpl {
 public:
  static MediaCaptureDevicesDispatcher* GetInstance();

  // Overridden from content::MediaObserver:
  void OnAudioCaptureDevicesChanged() override;
  void OnVideoCaptureDevicesChanged() override;
  void OnMediaRequestStateChanged(int render_process_id,
                                  int render_view_id,
                                  int page_request_id,
                                  const GURL& security_origin,
                                  blink::mojom::MediaStreamType stream_type,
                                  content::MediaRequestState state) override;
  void OnCreatingAudioStream(int render_process_id,
                             int render_view_id) override;
  void OnSetCapturingLinkSecured(int render_process_id,
                                 int render_frame_id,
                                 int page_request_id,
                                 blink::mojom::MediaStreamType stream_type,
                                 bool is_secure) override;
  const std::optional<blink::MediaStreamDevice>
  GetPreferredAudioDeviceForBrowserContext(
      content::BrowserContext* browser_context,
      const std::vector<std::string>& eligible_audio_device_ids) const override;
  const std::optional<blink::MediaStreamDevice>
  GetPreferredVideoDeviceForBrowserContext(
      content::BrowserContext* browser_context,
      const std::vector<std::string>& eligible_video_device_ids) const override;

  // disable copy
  MediaCaptureDevicesDispatcher(const MediaCaptureDevicesDispatcher&) = delete;
  MediaCaptureDevicesDispatcher& operator=(
      const MediaCaptureDevicesDispatcher&) = delete;

 private:
  friend base::NoDestructor<MediaCaptureDevicesDispatcher>;

  MediaCaptureDevicesDispatcher();
  ~MediaCaptureDevicesDispatcher() override;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_MEDIA_MEDIA_CAPTURE_DEVICES_DISPATCHER_H_
