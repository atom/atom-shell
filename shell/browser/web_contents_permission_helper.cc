// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/web_contents_permission_helper.h"

#include <string_view>
#include <utility>

#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents_user_data.h"
#include "shell/browser/electron_permission_manager.h"
// #include "shell/browser/media/media_stream_devices_controller.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/webrtc/media_stream_devices_controller.h"
#include "shell/browser/media/media_capture_devices_dispatcher.h"

namespace {

constexpr std::string_view MediaStreamTypeToString(
    blink::mojom::MediaStreamType type) {
  switch (type) {
    case blink::mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE:
      return "audio";
    case blink::mojom::MediaStreamType::DEVICE_VIDEO_CAPTURE:
      return "video";
    default:
      return "unknown";
  }
}

}  // namespace

namespace electron {

namespace {

[[nodiscard]] content::DesktopMediaID GetScreenId(
    const std::vector<std::string>& requested_video_device_ids) {
  if (!requested_video_device_ids.empty() &&
      !requested_video_device_ids.front().empty())
    return content::DesktopMediaID::Parse(requested_video_device_ids.front());

  // If the device id wasn't specified then this is a screen capture request
  // (i.e. chooseDesktopMedia() API wasn't used to generate device id).
  return content::DesktopMediaID(content::DesktopMediaID::TYPE_SCREEN,
                                 -1 /* kFullDesktopScreenId */);
}

// Handles requests for legacy-style `navigator.getUserMedia(...)` calls.
// This includes desktop capture through the chromeMediaSource /
// chromeMediaSourceId constraints.
void HandleUserMediaRequest(const content::MediaStreamRequest& request,
                            content::MediaResponseCallback callback) {
  blink::mojom::StreamDevicesSetPtr stream_devices_set =
      blink::mojom::StreamDevicesSet::New();
  stream_devices_set->stream_devices.emplace_back(
      blink::mojom::StreamDevices::New());
  blink::mojom::StreamDevices& devices = *stream_devices_set->stream_devices[0];

  if (request.audio_type ==
      blink::mojom::MediaStreamType::GUM_TAB_AUDIO_CAPTURE) {
    devices.audio_device = blink::MediaStreamDevice(
        blink::mojom::MediaStreamType::GUM_TAB_AUDIO_CAPTURE, "", "");
  }
  if (request.video_type ==
      blink::mojom::MediaStreamType::GUM_TAB_VIDEO_CAPTURE) {
    devices.video_device = blink::MediaStreamDevice(
        blink::mojom::MediaStreamType::GUM_TAB_VIDEO_CAPTURE, "", "");
  }
  if (request.audio_type ==
      blink::mojom::MediaStreamType::GUM_DESKTOP_AUDIO_CAPTURE) {
    devices.audio_device = blink::MediaStreamDevice(
        blink::mojom::MediaStreamType::GUM_DESKTOP_AUDIO_CAPTURE, "loopback",
        "System Audio");
  }
  if (request.video_type ==
      blink::mojom::MediaStreamType::GUM_DESKTOP_VIDEO_CAPTURE) {
    devices.video_device = blink::MediaStreamDevice(
        blink::mojom::MediaStreamType::GUM_DESKTOP_VIDEO_CAPTURE,
        GetScreenId(request.requested_video_device_ids).ToString(), "Screen");
  }

  bool empty =
      !devices.audio_device.has_value() && !devices.video_device.has_value();
  std::move(callback).Run(
      *stream_devices_set,
      empty ? blink::mojom::MediaStreamRequestResult::NO_HARDWARE
            : blink::mojom::MediaStreamRequestResult::OK,
      nullptr);
}

void OnMediaStreamRequestResponse(
    content::MediaResponseCallback callback,
    const blink::mojom::StreamDevicesSet& stream_devices_set,
    blink::mojom::MediaStreamRequestResult result,
    bool blocked_by_permissions_policy,
    ContentSetting audio_setting,
    ContentSetting video_setting) {
  std::move(callback).Run(stream_devices_set, result, nullptr);
}

void MediaAccessAllowed(const content::MediaStreamRequest& request,
                        content::MediaResponseCallback callback,
                        bool allowed) {
  if (allowed) {
    if (request.video_type ==
            blink::mojom::MediaStreamType::GUM_DESKTOP_VIDEO_CAPTURE ||
        request.audio_type ==
            blink::mojom::MediaStreamType::GUM_DESKTOP_AUDIO_CAPTURE ||
        request.video_type ==
            blink::mojom::MediaStreamType::GUM_TAB_VIDEO_CAPTURE ||
        request.audio_type ==
            blink::mojom::MediaStreamType::GUM_TAB_AUDIO_CAPTURE) {
      HandleUserMediaRequest(request, std::move(callback));
    } else if (request.video_type ==
                   blink::mojom::MediaStreamType::DEVICE_VIDEO_CAPTURE ||
               request.audio_type ==
                   blink::mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE) {
      webrtc::MediaStreamDevicesController::RequestPermissions(
          request, MediaCaptureDevicesDispatcher::GetInstance(),
          base::BindOnce(&OnMediaStreamRequestResponse, std::move(callback)),
          allowed);
    } else if (request.video_type ==
                   blink::mojom::MediaStreamType::DISPLAY_VIDEO_CAPTURE ||
               request.video_type == blink::mojom::MediaStreamType::
                                         DISPLAY_VIDEO_CAPTURE_THIS_TAB ||
               request.video_type ==
                   blink::mojom::MediaStreamType::DISPLAY_VIDEO_CAPTURE_SET ||
               request.audio_type ==
                   blink::mojom::MediaStreamType::DISPLAY_AUDIO_CAPTURE) {
      content::RenderFrameHost* rfh = content::RenderFrameHost::FromID(
          request.render_process_id, request.render_frame_id);
      if (!rfh)
        return;

      content::BrowserContext* browser_context = rfh->GetBrowserContext();
      ElectronBrowserContext* electron_browser_context =
          static_cast<ElectronBrowserContext*>(browser_context);
      auto split_callback = base::SplitOnceCallback(std::move(callback));
      if (electron_browser_context->ChooseDisplayMediaDevice(
              request, std::move(split_callback.second)))
        return;
      std::move(split_callback.first)
          .Run(blink::mojom::StreamDevicesSet(),
               blink::mojom::MediaStreamRequestResult::NOT_SUPPORTED, nullptr);
    } else {
      std::move(callback).Run(
          blink::mojom::StreamDevicesSet(),
          blink::mojom::MediaStreamRequestResult::NOT_SUPPORTED, nullptr);
    }
  } else {
    std::move(callback).Run(
        blink::mojom::StreamDevicesSet(),
        blink::mojom::MediaStreamRequestResult::PERMISSION_DENIED, nullptr);
  }
}

void OnPermissionResponse(base::OnceCallback<void(bool)> callback,
                          blink::mojom::PermissionStatus status) {
  if (status == blink::mojom::PermissionStatus::GRANTED)
    std::move(callback).Run(true);
  else
    std::move(callback).Run(false);
}

}  // namespace

WebContentsPermissionHelper::WebContentsPermissionHelper(
    content::WebContents* web_contents)
    : content::WebContentsUserData<WebContentsPermissionHelper>(*web_contents),
      web_contents_(web_contents) {}

WebContentsPermissionHelper::~WebContentsPermissionHelper() = default;

void WebContentsPermissionHelper::RequestPermission(
    content::RenderFrameHost* requesting_frame,
    blink::PermissionType permission,
    base::OnceCallback<void(bool)> callback,
    bool user_gesture,
    base::Value::Dict details) {
  auto* permission_manager = static_cast<ElectronPermissionManager*>(
      web_contents_->GetBrowserContext()->GetPermissionControllerDelegate());
  auto origin = web_contents_->GetLastCommittedURL();
  permission_manager->RequestPermissionWithDetails(
      permission, requesting_frame, origin, false, std::move(details),
      base::BindOnce(&OnPermissionResponse, std::move(callback)));
}

bool WebContentsPermissionHelper::CheckPermission(
    blink::PermissionType permission,
    base::Value::Dict details) const {
  auto* rfh = web_contents_->GetPrimaryMainFrame();
  auto* permission_manager = static_cast<ElectronPermissionManager*>(
      web_contents_->GetBrowserContext()->GetPermissionControllerDelegate());
  auto origin = web_contents_->GetLastCommittedURL();
  return permission_manager->CheckPermissionWithDetails(permission, rfh, origin,
                                                        std::move(details));
}

void WebContentsPermissionHelper::RequestFullscreenPermission(
    content::RenderFrameHost* requesting_frame,
    base::OnceCallback<void(bool)> callback) {
  RequestPermission(
      requesting_frame,
      static_cast<blink::PermissionType>(PermissionType::FULLSCREEN),
      std::move(callback));
}

void WebContentsPermissionHelper::RequestMediaAccessPermission(
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback response_callback) {
  auto callback = base::BindOnce(&MediaAccessAllowed, request,
                                 std::move(response_callback));

  base::Value::Dict details;
  base::Value::List media_types;
  if (request.audio_type ==
      blink::mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE) {
    media_types.Append("audio");
  }
  if (request.video_type ==
      blink::mojom::MediaStreamType::DEVICE_VIDEO_CAPTURE) {
    media_types.Append("video");
  }
  details.Set("mediaTypes", std::move(media_types));
  details.Set("securityOrigin", request.security_origin.spec());

  // The permission type doesn't matter here, AUDIO_CAPTURE/VIDEO_CAPTURE
  // are presented as same type in content_converter.h.
  RequestPermission(content::RenderFrameHost::FromID(request.render_process_id,
                                                     request.render_frame_id),
                    blink::PermissionType::AUDIO_CAPTURE, std::move(callback),
                    false, std::move(details));
}

void WebContentsPermissionHelper::RequestWebNotificationPermission(
    content::RenderFrameHost* requesting_frame,
    base::OnceCallback<void(bool)> callback) {
  RequestPermission(requesting_frame, blink::PermissionType::NOTIFICATIONS,
                    std::move(callback));
}

void WebContentsPermissionHelper::RequestPointerLockPermission(
    bool user_gesture,
    bool last_unlocked_by_target,
    base::OnceCallback<void(content::WebContents*, bool, bool, bool)>
        callback) {
  RequestPermission(web_contents_->GetPrimaryMainFrame(),
                    blink::PermissionType::POINTER_LOCK,
                    base::BindOnce(std::move(callback), web_contents_,
                                   user_gesture, last_unlocked_by_target),
                    user_gesture);
}

void WebContentsPermissionHelper::RequestKeyboardLockPermission(
    bool esc_key_locked,
    base::OnceCallback<void(content::WebContents*, bool, bool)> callback) {
  RequestPermission(
      web_contents_->GetPrimaryMainFrame(),
      blink::PermissionType::KEYBOARD_LOCK,
      base::BindOnce(std::move(callback), web_contents_, esc_key_locked));
}

void WebContentsPermissionHelper::RequestOpenExternalPermission(
    content::RenderFrameHost* requesting_frame,
    base::OnceCallback<void(bool)> callback,
    bool user_gesture,
    const GURL& url) {
  base::Value::Dict details;
  details.Set("externalURL", url.spec());
  RequestPermission(
      requesting_frame,
      static_cast<blink::PermissionType>(PermissionType::OPEN_EXTERNAL),
      std::move(callback), user_gesture, std::move(details));
}

bool WebContentsPermissionHelper::CheckMediaAccessPermission(
    const url::Origin& security_origin,
    blink::mojom::MediaStreamType type) const {
  base::Value::Dict details;
  details.Set("securityOrigin", security_origin.GetURL().spec());
  details.Set("mediaType", MediaStreamTypeToString(type));
  // The permission type doesn't matter here, AUDIO_CAPTURE/VIDEO_CAPTURE
  // are presented as same type in content_converter.h.
  return CheckPermission(blink::PermissionType::AUDIO_CAPTURE,
                         std::move(details));
}

bool WebContentsPermissionHelper::CheckSerialAccessPermission(
    const url::Origin& embedding_origin) const {
  base::Value::Dict details;
  details.Set("securityOrigin", embedding_origin.GetURL().spec());
  return CheckPermission(
      static_cast<blink::PermissionType>(PermissionType::SERIAL),
      std::move(details));
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(WebContentsPermissionHelper);

}  // namespace electron
