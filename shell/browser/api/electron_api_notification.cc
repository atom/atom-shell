// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_notification.h"

#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "gin/handle.h"
#include "shell/browser/api/electron_api_menu.h"
#include "shell/browser/browser.h"
#include "shell/browser/electron_browser_client.h"
#include "shell/common/gin_converters/image_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/object_template_builder.h"
#include "shell/common/node_includes.h"
#include "url/gurl.h"

namespace gin {

template <>
struct Converter<electron::NotificationAction> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     electron::NotificationAction* out) {
    gin::Dictionary dict(isolate);
    if (!ConvertFromV8(isolate, val, &dict))
      return false;

    if (!dict.Get("type", &(out->type))) {
      return false;
    }
    dict.Get("text", &(out->text));
    return true;
  }

  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   electron::NotificationAction val) {
    auto dict = gin::Dictionary::CreateEmpty(isolate);
    dict.Set("text", val.text);
    dict.Set("type", val.type);
    return ConvertToV8(isolate, dict);
  }
};

}  // namespace gin

namespace electron::api {

gin::WrapperInfo Notification::kWrapperInfo = {gin::kEmbedderNativeGin};

Notification::Notification(gin::Arguments* args) {
  presenter_ = static_cast<ElectronBrowserClient*>(ElectronBrowserClient::Get())
                   ->GetNotificationPresenter();

  gin::Dictionary opts(nullptr);
  if (args->GetNext(&opts)) {
    opts.Get("title", &title_);
    opts.Get("subtitle", &subtitle_);
    opts.Get("body", &body_);
    has_icon_ = opts.Get("icon", &icon_);
    if (has_icon_) {
      opts.Get("icon", &icon_path_);
    }
    opts.Get("silent", &silent_);
    opts.Get("replyPlaceholder", &reply_placeholder_);
    opts.Get("urgency", &urgency_);
    opts.Get("hasReply", &has_reply_);
    opts.Get("timeoutType", &timeout_type_);
    opts.Get("actions", &actions_);
    opts.Get("sound", &sound_);
    opts.Get("closeButtonText", &close_button_text_);
    opts.Get("toastXml", &toast_xml_);
  }

  // The notification should not be garbage collected until it is interacted
  // with
  if (timeout_type_ == u"never")
    Pin(JavascriptEnvironment::GetIsolate());
}

Notification::~Notification() {
  if (notification_)
    notification_->set_delegate(nullptr);
}

// static
gin::Handle<Notification> Notification::New(gin_helper::ErrorThrower thrower,
                                            gin::Arguments* args) {
  if (!Browser::Get()->is_ready()) {
    thrower.ThrowError("Cannot create Notification before app is ready");
    return gin::Handle<Notification>();
  }
  return gin::CreateHandle(thrower.isolate(), new Notification(args));
}

// Getters
std::u16string Notification::GetTitle() const {
  return title_;
}

std::u16string Notification::GetSubtitle() const {
  return subtitle_;
}

std::u16string Notification::GetBody() const {
  return body_;
}

bool Notification::GetSilent() const {
  return silent_;
}

bool Notification::GetHasReply() const {
  return has_reply_;
}

std::u16string Notification::GetTimeoutType() const {
  return timeout_type_;
}

std::u16string Notification::GetReplyPlaceholder() const {
  return reply_placeholder_;
}

std::u16string Notification::GetSound() const {
  return sound_;
}

std::u16string Notification::GetUrgency() const {
  return urgency_;
}

std::vector<electron::NotificationAction> Notification::GetActions() const {
  return actions_;
}

std::u16string Notification::GetCloseButtonText() const {
  return close_button_text_;
}

std::u16string Notification::GetToastXml() const {
  return toast_xml_;
}

// Setters
void Notification::SetTitle(const std::u16string& new_title) {
  title_ = new_title;
}

void Notification::SetSubtitle(const std::u16string& new_subtitle) {
  subtitle_ = new_subtitle;
}

void Notification::SetBody(const std::u16string& new_body) {
  body_ = new_body;
}

void Notification::SetSilent(bool new_silent) {
  silent_ = new_silent;
}

void Notification::SetHasReply(bool new_has_reply) {
  has_reply_ = new_has_reply;
}

void Notification::SetTimeoutType(const std::u16string& new_timeout_type) {
  // The notification should be garbage collected if it is not set to `never`
  if (timeout_type_ == u"never" && new_timeout_type != u"never") {
    Unpin();
  }
  timeout_type_ = new_timeout_type;
}

void Notification::SetReplyPlaceholder(const std::u16string& new_placeholder) {
  reply_placeholder_ = new_placeholder;
}

void Notification::SetSound(const std::u16string& new_sound) {
  sound_ = new_sound;
}

void Notification::SetUrgency(const std::u16string& new_urgency) {
  urgency_ = new_urgency;
}

void Notification::SetActions(
    const std::vector<electron::NotificationAction>& actions) {
  actions_ = actions;
}

void Notification::SetCloseButtonText(const std::u16string& text) {
  close_button_text_ = text;
}

void Notification::SetToastXml(const std::u16string& new_toast_xml) {
  toast_xml_ = new_toast_xml;
}

void Notification::NotificationAction(int index) {
  Emit("action", index);
}

void Notification::NotificationClick() {
  Unpin();
  Emit("click");
}

void Notification::NotificationReplied(const std::string& reply) {
  Unpin();
  Emit("reply", reply);
}

void Notification::NotificationDisplayed() {
  Emit("show");
}

void Notification::NotificationFailed(const std::string& error) {
  Unpin();
  Emit("failed", error);
}

void Notification::NotificationDestroyed() {
  Unpin();
}

void Notification::NotificationClosed() {
  Unpin();
  Emit("close");
}

void Notification::Close() {
  if (notification_) {
    Unpin();
    notification_->Dismiss();
    notification_->set_delegate(nullptr);
    notification_.reset();
  }
}

// Showing notifications
void Notification::Show() {
  Close();
  if (presenter_) {
    notification_ = presenter_->CreateNotification(
        this, base::Uuid::GenerateRandomV4().AsLowercaseString());
    if (notification_) {
      electron::NotificationOptions options;
      options.title = title_;
      options.subtitle = subtitle_;
      options.msg = body_;
      options.icon_url = GURL();
      options.icon = icon_.AsBitmap();
      options.silent = silent_;
      options.has_reply = has_reply_;
      options.timeout_type = timeout_type_;
      options.reply_placeholder = reply_placeholder_;
      options.actions = actions_;
      options.sound = sound_;
      options.close_button_text = close_button_text_;
      options.urgency = urgency_;
      options.toast_xml = toast_xml_;
      notification_->Show(options);
    }
  }
}

bool Notification::IsSupported() {
  return !!static_cast<ElectronBrowserClient*>(ElectronBrowserClient::Get())
               ->GetNotificationPresenter();
}

void Notification::FillObjectTemplate(v8::Isolate* isolate,
                                      v8::Local<v8::ObjectTemplate> templ) {
  gin::ObjectTemplateBuilder(isolate, GetClassName(), templ)
      .SetMethod("show", &Notification::Show)
      .SetMethod("close", &Notification::Close)
      .SetProperty("title", &Notification::GetTitle, &Notification::SetTitle)
      .SetProperty("subtitle", &Notification::GetSubtitle,
                   &Notification::SetSubtitle)
      .SetProperty("body", &Notification::GetBody, &Notification::SetBody)
      .SetProperty("silent", &Notification::GetSilent, &Notification::SetSilent)
      .SetProperty("hasReply", &Notification::GetHasReply,
                   &Notification::SetHasReply)
      .SetProperty("timeoutType", &Notification::GetTimeoutType,
                   &Notification::SetTimeoutType)
      .SetProperty("replyPlaceholder", &Notification::GetReplyPlaceholder,
                   &Notification::SetReplyPlaceholder)
      .SetProperty("urgency", &Notification::GetUrgency,
                   &Notification::SetUrgency)
      .SetProperty("sound", &Notification::GetSound, &Notification::SetSound)
      .SetProperty("actions", &Notification::GetActions,
                   &Notification::SetActions)
      .SetProperty("closeButtonText", &Notification::GetCloseButtonText,
                   &Notification::SetCloseButtonText)
      .SetProperty("toastXml", &Notification::GetToastXml,
                   &Notification::SetToastXml)
      .Build();
}

const char* Notification::GetTypeName() {
  return GetClassName();
}

}  // namespace electron::api

namespace {

using electron::api::Notification;

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary dict(isolate, exports);
  dict.Set("Notification", Notification::GetConstructor(context));
  dict.SetMethod("isSupported", &Notification::IsSupported);
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_browser_notification, Initialize)
