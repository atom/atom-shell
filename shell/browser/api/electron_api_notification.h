// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_NOTIFICATION_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_NOTIFICATION_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "gin/wrappable.h"
#include "shell/browser/event_emitter_mixin.h"
#include "shell/browser/notifications/notification.h"
#include "shell/browser/notifications/notification_delegate.h"
#include "shell/browser/notifications/notification_presenter.h"
#include "shell/common/gin_helper/cleaned_up_at_exit.h"
#include "shell/common/gin_helper/constructible.h"
#include "ui/gfx/image/image.h"

namespace gin {
class Arguments;
template <typename T>
class Handle;
}  // namespace gin

namespace gin_helper {
class ErrorThrower;
}  // namespace gin_helper

namespace electron::api {

class Notification : public gin::Wrappable<Notification>,
                     public gin_helper::EventEmitterMixin<Notification>,
                     public gin_helper::Constructible<Notification>,
                     public gin_helper::CleanedUpAtExit,
                     public NotificationDelegate {
 public:
  static bool IsSupported();

  // gin_helper::Constructible
  static gin::Handle<Notification> New(gin_helper::ErrorThrower thrower,
                                       gin::Arguments* args);
  static void FillObjectTemplate(v8::Isolate*, v8::Local<v8::ObjectTemplate>);
  static const char* GetClassName() { return "Notification"; }

  // NotificationDelegate:
  void NotificationAction(int index) override;
  void NotificationClick() override;
  void NotificationReplied(const std::string& reply) override;
  void NotificationDisplayed() override;
  void NotificationDestroyed() override;
  void NotificationClosed() override;
  void NotificationFailed(const std::string& error) override;

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  const char* GetTypeName() override;

  // disable copy
  Notification(const Notification&) = delete;
  Notification& operator=(const Notification&) = delete;

 protected:
  explicit Notification(gin::Arguments* args);
  ~Notification() override;

  void Show();
  void Close();

  // Prop Getters
  const std::u16string& title() const { return title_; }
  const std::u16string& subtitle() const { return subtitle_; }
  const std::u16string& body() const { return body_; }
  bool is_silent() const { return silent_; }
  bool has_reply() const { return has_reply_; }
  const std::u16string& timeout_type() const { return timeout_type_; }
  const std::u16string& reply_placeholder() const { return reply_placeholder_; }
  const std::u16string& urgency() const { return urgency_; }
  const std::u16string& sound() const { return sound_; }
  const std::vector<electron::NotificationAction>& actions() const {
    return actions_;
  }
  const std::u16string& close_button_text() const { return close_button_text_; }
  const std::u16string& toast_xml() const { return toast_xml_; }

  // Prop Setters
  void SetTitle(const std::u16string& new_title);
  void SetSubtitle(const std::u16string& new_subtitle);
  void SetBody(const std::u16string& new_body);
  void SetSilent(bool new_silent);
  void SetHasReply(bool new_has_reply);
  void SetUrgency(const std::u16string& new_urgency);
  void SetTimeoutType(const std::u16string& new_timeout_type);
  void SetReplyPlaceholder(const std::u16string& new_placeholder);
  void SetSound(const std::u16string& sound);
  void SetActions(const std::vector<electron::NotificationAction>& actions);
  void SetCloseButtonText(const std::u16string& text);
  void SetToastXml(const std::u16string& new_toast_xml);

 private:
  std::u16string title_;
  std::u16string subtitle_;
  std::u16string body_;
  gfx::Image icon_;
  bool silent_ = false;
  bool has_reply_ = false;
  std::u16string timeout_type_;
  std::u16string reply_placeholder_;
  std::u16string sound_;
  std::u16string urgency_;
  std::vector<electron::NotificationAction> actions_;
  std::u16string close_button_text_;
  std::u16string toast_xml_;

  raw_ptr<electron::NotificationPresenter> presenter_;

  base::WeakPtr<electron::Notification> notification_;
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_NOTIFICATION_H_
