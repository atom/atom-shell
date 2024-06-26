// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_NOTIFICATIONS_LINUX_LIBNOTIFY_NOTIFICATION_H_
#define ELECTRON_SHELL_BROWSER_NOTIFICATIONS_LINUX_LIBNOTIFY_NOTIFICATION_H_

#include "base/memory/raw_ptr_exclusion.h"
#include "library_loaders/libnotify_loader.h"
#include "shell/browser/notifications/notification.h"
#include "ui/base/glib/scoped_gsignal.h"

namespace electron {

class LibnotifyNotification : public Notification {
 public:
  LibnotifyNotification(NotificationDelegate* delegate,
                        NotificationPresenter* presenter);
  ~LibnotifyNotification() override;

  static bool Initialize();

  // Notification:
  void Show(const NotificationOptions& options) override;
  void Dismiss() override;

 private:
  void OnNotificationClosed(NotifyNotification* notification);
  static void OnNotificationView(NotifyNotification* notification,
                                 char* action,
                                 gpointer user_data);

  RAW_PTR_EXCLUSION NotifyNotification* notification_ = nullptr;

  ScopedGSignal signal_;
  bool on_dismissing_ = false;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_NOTIFICATIONS_LINUX_LIBNOTIFY_NOTIFICATION_H_
