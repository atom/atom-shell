// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Copyright (c) 2015 Felix Rieseberg <feriese@microsoft.com> and
// Jason Poon <jason.poon@microsoft.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

// Usage Example (JavaScript:
// var windowsNotification = new Notification("Test Title", {
//   body: "Hi, I'm an example body. How are you?",
//   icon: "file:///C:/Path/To/Your/Image.png"
// });

// windowsNotification.onshow  = function () {
//   console.log("Notification shown")
// };
// windowsNotification.onclick = function () {
//   console.log("Notification clicked")
// };
// windowsNotification.onclose = function () {
//   console.log("Notification dismissed")
// };

#ifndef ELECTRON_SHELL_BROWSER_NOTIFICATIONS_WIN_NOTIFICATION_PRESENTER_WIN_H_
#define ELECTRON_SHELL_BROWSER_NOTIFICATIONS_WIN_NOTIFICATION_PRESENTER_WIN_H_

#include "base/files/scoped_temp_dir.h"
#include "shell/browser/notifications/notification_presenter.h"

class GURL;
class SkBitmap;

namespace electron {

class NotificationPresenterWin : public NotificationPresenter {
 public:
  NotificationPresenterWin();
  ~NotificationPresenterWin() override;

  bool Init();

  std::wstring SaveIconToFilesystem(const SkBitmap& icon, const GURL& origin);

 private:
  // NotificationPresenter
  Notification* CreateNotificationObject(
      NotificationDelegate* delegate) override;

  base::ScopedTempDir temp_dir_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_NOTIFICATIONS_WIN_NOTIFICATION_PRESENTER_WIN_H_
