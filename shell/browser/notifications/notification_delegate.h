// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_NOTIFICATIONS_NOTIFICATION_DELEGATE_H_
#define ELECTRON_SHELL_BROWSER_NOTIFICATIONS_NOTIFICATION_DELEGATE_H_

#include <string>

namespace electron {

class NotificationDelegate {
 public:
  // The native Notification object is destroyed.
  virtual void NotificationDestroyed() {}

  // Failed to send the notification.
  virtual void NotificationFailed(const std::string& error) {}

  // Notification was replied to (SAP-14036 upgrade for persistent notifications
  // support)
  virtual void NotificationReplied(const std::string& reply) {}
  virtual void NotificationAction(int index) {}

  virtual void NotificationClick() {}
  virtual void NotificationClosed(bool is_persistent = false) {}
  virtual void NotificationDisplayed() {}

 protected:
  NotificationDelegate() = default;
  ~NotificationDelegate() = default;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_NOTIFICATIONS_NOTIFICATION_DELEGATE_H_
