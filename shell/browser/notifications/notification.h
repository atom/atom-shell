// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_NOTIFICATIONS_NOTIFICATION_H_
#define ELECTRON_SHELL_BROWSER_NOTIFICATIONS_NOTIFICATION_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "url/gurl.h"

namespace electron {

class NotificationDelegate;
class NotificationPresenter;

struct NotificationAction {
  std::u16string type;
  std::u16string text;
};

struct NotificationOptions {
  std::u16string title;
  std::u16string subtitle;
  std::u16string msg;
  std::string tag;
  bool silent;
  GURL icon_url;
  SkBitmap icon;
  bool has_reply;
  std::u16string timeout_type;
  std::u16string reply_placeholder;
  std::u16string sound;
  std::u16string urgency;  // Linux
  std::vector<NotificationAction> actions;
  std::u16string close_button_text;
  std::u16string toast_xml;

  NotificationOptions();
  ~NotificationOptions();
};

class Notification {
 public:
  virtual ~Notification();

  // Shows the notification.
  virtual void Show(const NotificationOptions& options) = 0;

  // Dismisses the notification. On some platforms this will result in full
  // removal and destruction of the notification, but if the initial dismissal
  // does not fully get rid of the notification it will be destroyed in Remove.
  virtual void Dismiss() = 0;

  // Removes the notification if it was not fully removed during dismissal,
  // as can happen on some platforms including Windows.
  virtual void Remove() {}

  // Should be called by derived classes.
  void NotificationClicked();
  void NotificationDismissed(bool should_destroy = true);
  void NotificationFailed(const std::string& error = "");

  // delete this.
  void Destroy();

  base::WeakPtr<Notification> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  void set_delegate(NotificationDelegate* delegate) { delegate_ = delegate; }
  void set_notification_id(const std::string& id) { notification_id_ = id; }
  void set_is_dismissed(bool dismissed) { is_dismissed_ = dismissed; }

  NotificationDelegate* delegate() const { return delegate_; }
  NotificationPresenter* presenter() const { return presenter_; }
  const std::string& notification_id() const { return notification_id_; }
  bool is_dismissed() const { return is_dismissed_; }

  // disable copy
  Notification(const Notification&) = delete;
  Notification& operator=(const Notification&) = delete;

 protected:
  Notification(NotificationDelegate* delegate,
               NotificationPresenter* presenter);

 private:
  raw_ptr<NotificationDelegate> delegate_;
  raw_ptr<NotificationPresenter> presenter_;
  std::string notification_id_;
  bool is_dismissed_ = false;

  base::WeakPtrFactory<Notification> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_NOTIFICATIONS_NOTIFICATION_H_
