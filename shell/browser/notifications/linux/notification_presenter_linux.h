// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Copyright (c) 2013 Patrick Reynolds <piki@github.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#ifndef ELECTRON_SHELL_BROWSER_NOTIFICATIONS_LINUX_NOTIFICATION_PRESENTER_LINUX_H_
#define ELECTRON_SHELL_BROWSER_NOTIFICATIONS_LINUX_NOTIFICATION_PRESENTER_LINUX_H_

#include "shell/browser/notifications/notification_presenter.h"

namespace electron {

class NotificationPresenterLinux : public NotificationPresenter {
 public:
  NotificationPresenterLinux();
  ~NotificationPresenterLinux() override;

 private:
  // NotificationPresenter
  Notification* CreateNotificationObject(
      NotificationDelegate* delegate) override;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_NOTIFICATIONS_LINUX_NOTIFICATION_PRESENTER_LINUX_H_
