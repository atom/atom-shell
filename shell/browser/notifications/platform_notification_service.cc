// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#include "shell/browser/notifications/platform_notification_service.h"

#include "content/public/browser/notification_event_dispatcher.h"
#include "content/public/browser/render_process_host.h"
#include "shell/browser/electron_browser_client.h"
#include "shell/browser/notifications/notification.h"
#include "shell/browser/notifications/notification_delegate.h"
#include "shell/browser/notifications/notification_presenter.h"
#include "third_party/blink/public/common/notifications/notification_resources.h"
#include "third_party/blink/public/common/notifications/platform_notification_data.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace electron {

namespace {

void OnWebNotificationAllowed(base::WeakPtr<Notification> notification,
                              const SkBitmap& icon,
                              const blink::PlatformNotificationData& data,
                              bool audio_muted,
                              bool allowed) {
  if (!notification)
    return;
  if (allowed) {
    electron::NotificationOptions options;
    options.title = data.title;
    options.msg = data.body;
    options.tag = data.tag;
    options.icon_url = data.icon;
    options.icon = icon;
    options.silent = audio_muted ? true : data.silent;
    options.has_reply = false;
    if (data.require_interaction)
      options.timeout_type = u"never";

    notification->Show(options);
  } else {
    notification->Destroy();
  }
}

class NotificationDelegateImpl final : public electron::NotificationDelegate {
 public:
  explicit NotificationDelegateImpl(const std::string& notification_id)
      : notification_id_(notification_id) {}

  // disable copy
  NotificationDelegateImpl(const NotificationDelegateImpl&) = delete;
  NotificationDelegateImpl& operator=(const NotificationDelegateImpl&) = delete;

  // electron::NotificationDelegate
  void NotificationDestroyed() override { delete this; }

  void NotificationClick() override {
    content::NotificationEventDispatcher::GetInstance()
        ->DispatchNonPersistentClickEvent(notification_id_, base::DoNothing());
  }

  void NotificationClosed() override {
    content::NotificationEventDispatcher::GetInstance()
        ->DispatchNonPersistentCloseEvent(notification_id_, base::DoNothing());
  }

  void NotificationDisplayed() override {
    content::NotificationEventDispatcher::GetInstance()
        ->DispatchNonPersistentShowEvent(notification_id_);
  }

 private:
  std::string notification_id_;
};

}  // namespace

PlatformNotificationService::PlatformNotificationService(
    ElectronBrowserClient* browser_client)
    : browser_client_(browser_client) {}

PlatformNotificationService::~PlatformNotificationService() = default;

void PlatformNotificationService::DisplayNotification(
    content::RenderFrameHost* render_frame_host,
    const std::string& notification_id,
    const GURL& origin,
    const GURL& document_url,
    const blink::PlatformNotificationData& notification_data,
    const blink::NotificationResources& notification_resources) {
  auto* presenter = browser_client_->GetNotificationPresenter();
  if (!presenter)
    return;

  // If a new notification is created with the same tag as an
  // existing one, replace the old notification with the new one.
  // The notification_id is generated from the tag, so the only way a
  // notification will be closed as a result of this call is if one with
  // the same tag is already extant.
  //
  // See: https://notifications.spec.whatwg.org/#showing-a-notification
  presenter->CloseNotificationWithId(notification_id);

  auto* delegate = new NotificationDelegateImpl(notification_id);

  auto notification = presenter->CreateNotification(delegate, notification_id);
  if (notification) {
    browser_client_->WebNotificationAllowed(
        render_frame_host,
        base::BindRepeating(&OnWebNotificationAllowed, notification,
                            notification_resources.notification_icon,
                            notification_data));
  }
}

void PlatformNotificationService::CloseNotification(
    const std::string& notification_id) {
  auto* presenter = browser_client_->GetNotificationPresenter();
  if (!presenter)
    return;
  presenter->CloseNotificationWithId(notification_id);
}

void PlatformNotificationService::GetDisplayedNotifications(
    DisplayedNotificationsCallback callback) {}

void PlatformNotificationService::GetDisplayedNotificationsForOrigin(
    const GURL& origin,
    DisplayedNotificationsCallback callback) {}

int64_t PlatformNotificationService::ReadNextPersistentNotificationId() {
  // Electron doesn't support persistent notifications.
  return 0;
}

void PlatformNotificationService::RecordNotificationUkmEvent(
    const content::NotificationDatabaseData& data) {}

void PlatformNotificationService::ScheduleTrigger(base::Time timestamp) {}

base::Time PlatformNotificationService::ReadNextTriggerTimestamp() {
  return base::Time::Max();
}

}  // namespace electron
