// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Copyright (c) 2013 Adam Roben <adam@roben.org>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#ifndef ELECTRON_SHELL_BROWSER_NOTIFICATIONS_MAC_NOTIFICATION_PRESENTER_MAC_H_
#define ELECTRON_SHELL_BROWSER_NOTIFICATIONS_MAC_NOTIFICATION_PRESENTER_MAC_H_

#include "shell/browser/notifications/mac/notification_center_delegate.h"
#include "shell/browser/notifications/notification_presenter.h"

namespace electron {

class CocoaNotification;

class NotificationPresenterMac : public NotificationPresenter {
 public:
  CocoaNotification* GetNotification(NSUserNotification* ns_notification);

  NotificationPresenterMac();
  ~NotificationPresenterMac() override;

 private:
  // NotificationPresenter
  Notification* CreateNotificationObject(
      NotificationDelegate* delegate) override;

  NotificationCenterDelegate* __strong notification_center_delegate_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_NOTIFICATIONS_MAC_NOTIFICATION_PRESENTER_MAC_H_
