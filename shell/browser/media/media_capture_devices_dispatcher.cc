// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#include "shell/browser/media/media_capture_devices_dispatcher.h"

#include "components/webrtc/media_stream_devices_util.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/media_capture_devices.h"

using content::BrowserThread;

namespace electron {

MediaCaptureDevicesDispatcher* MediaCaptureDevicesDispatcher::GetInstance() {
  static base::NoDestructor<MediaCaptureDevicesDispatcher> instance;
  return instance.get();
}

MediaCaptureDevicesDispatcher::MediaCaptureDevicesDispatcher() {
  // MediaCaptureDevicesDispatcher is a singleton. It should be created on
  // UI thread.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

MediaCaptureDevicesDispatcher::~MediaCaptureDevicesDispatcher() = default;

const std::optional<blink::MediaStreamDevice>
MediaCaptureDevicesDispatcher::GetPreferredAudioDeviceForBrowserContext(
    content::BrowserContext* browser_context,
    const std::vector<std::string>& eligible_audio_device_ids) const {
  auto audio_devices = GetAudioCaptureDevices();
  if (!eligible_audio_device_ids.empty()) {
    audio_devices =
        webrtc::FilterMediaDevices(audio_devices, eligible_audio_device_ids);
  }

  if (audio_devices.empty())
    return std::nullopt;

  return audio_devices.front();
}

const std::optional<blink::MediaStreamDevice>
MediaCaptureDevicesDispatcher::GetPreferredVideoDeviceForBrowserContext(
    content::BrowserContext* browser_context,
    const std::vector<std::string>& eligible_video_device_ids) const {
  auto video_devices = GetVideoCaptureDevices();
  if (!eligible_video_device_ids.empty()) {
    video_devices =
        webrtc::FilterMediaDevices(video_devices, eligible_video_device_ids);
  }

  if (video_devices.empty())
    return std::nullopt;

  return video_devices.front();
}

}  // namespace electron
