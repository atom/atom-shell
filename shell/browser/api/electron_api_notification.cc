// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_notification.h"

#include "base/uuid.h"
#include "gin/handle.h"
#include "shell/browser/api/electron_api_menu.h"
#include "shell/browser/browser.h"
#include "shell/browser/electron_browser_client.h"
#include "shell/common/gin_converters/image_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/error_thrower.h"
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
    opts.Get("icon", &icon_);
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
  Emit("click");
}

void Notification::NotificationReplied(const std::string& reply) {
  Emit("reply", reply);
}

void Notification::NotificationDisplayed() {
  Emit("show");
}

void Notification::NotificationFailed(const std::string& error) {
  Emit("failed", error);
}

void Notification::NotificationDestroyed() {}

void Notification::NotificationClosed() {
  Emit("close");
}

void Notification::Close() {
  if (notification_) {
    if (notification_->is_dismissed()) {
      notification_->Remove();
    } else {
      notification_->Dismiss();
    }
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
      .SetProperty("title", &Notification::title, &Notification::SetTitle)
      .SetProperty("subtitle", &Notification::subtitle,
                   &Notification::SetSubtitle)
      .SetProperty("body", &Notification::body, &Notification::SetBody)
      .SetProperty("silent", &Notification::is_silent, &Notification::SetSilent)
      .SetProperty("hasReply", &Notification::has_reply,
                   &Notification::SetHasReply)
      .SetProperty("timeoutType", &Notification::timeout_type,
                   &Notification::SetTimeoutType)
      .SetProperty("replyPlaceholder", &Notification::reply_placeholder,
                   &Notification::SetReplyPlaceholder)
      .SetProperty("urgency", &Notification::urgency, &Notification::SetUrgency)
      .SetProperty("sound", &Notification::sound, &Notification::SetSound)
      .SetProperty("actions", &Notification::actions, &Notification::SetActions)
      .SetProperty("closeButtonText", &Notification::close_button_text,
                   &Notification::SetCloseButtonText)
      .SetProperty("toastXml", &Notification::toast_xml,
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
