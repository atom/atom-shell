// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#ifndef ELECTRON_SHELL_BROWSER_NOTIFICATIONS_PLATFORM_NOTIFICATION_SERVICE_H_
#define ELECTRON_SHELL_BROWSER_NOTIFICATIONS_PLATFORM_NOTIFICATION_SERVICE_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/platform_notification_service.h"

namespace electron {

class ElectronBrowserClient;

class PlatformNotificationService
    : public content::PlatformNotificationService {
 public:
  explicit PlatformNotificationService(ElectronBrowserClient* browser_client);
  ~PlatformNotificationService() override;

  // disable copy
  PlatformNotificationService(const PlatformNotificationService&) = delete;
  PlatformNotificationService& operator=(const PlatformNotificationService&) =
      delete;

 protected:
  // content::PlatformNotificationService:
  void DisplayNotification(
      content::RenderFrameHost* render_frame_host,
      const std::string& notification_id,
      const GURL& origin,
      const GURL& document_url,
      const blink::PlatformNotificationData& notification_data,
      const blink::NotificationResources& notification_resources) override;
  void DisplayPersistentNotification(
      const std::string& notification_id,
      const GURL& service_worker_scope,
      const GURL& origin,
      const blink::PlatformNotificationData& notification_data,
      const blink::NotificationResources& notification_resources) override {}
  void ClosePersistentNotification(
      const std::string& notification_id) override {}
  void CloseNotification(const std::string& notification_id) override;
  void GetDisplayedNotifications(
      DisplayedNotificationsCallback callback) override;
  void GetDisplayedNotificationsForOrigin(
      const GURL& origin,
      DisplayedNotificationsCallback callback) override;
  int64_t ReadNextPersistentNotificationId() override;
  void RecordNotificationUkmEvent(
      const content::NotificationDatabaseData& data) override;
  void ScheduleTrigger(base::Time timestamp) override;
  base::Time ReadNextTriggerTimestamp() override;

 private:
  raw_ptr<ElectronBrowserClient> browser_client_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_NOTIFICATIONS_PLATFORM_NOTIFICATION_SERVICE_H_
