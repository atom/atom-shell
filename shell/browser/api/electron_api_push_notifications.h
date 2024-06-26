// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_PUSH_NOTIFICATIONS_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_PUSH_NOTIFICATIONS_H_

#include <string>

#include <vector>
#include "gin/handle.h"
#include "gin/wrappable.h"
#include "shell/browser/browser_observer.h"
#include "shell/browser/electron_browser_client.h"
#include "shell/browser/event_emitter_mixin.h"
#include "shell/common/gin_helper/promise.h"

namespace electron::api {

class PushNotifications
    : public ElectronBrowserClient::Delegate,
      public gin::Wrappable<PushNotifications>,
      public gin_helper::EventEmitterMixin<PushNotifications>,
      private BrowserObserver {
 public:
  static PushNotifications* Get();
  static gin::Handle<PushNotifications> Create(v8::Isolate* isolate);

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  // disable copy
  PushNotifications(const PushNotifications&) = delete;
  PushNotifications& operator=(const PushNotifications&) = delete;

#if BUILDFLAG(IS_MAC)
  void OnDidReceiveAPNSNotification(const base::Value::Dict& user_info);
  void ResolveAPNSPromiseSetWithToken(const std::string& token_string);
  void RejectAPNSPromiseSetWithError(const std::string& error_message);
#endif

 private:
  PushNotifications();
  ~PushNotifications() override;
  // This set maintains all the promises that should be fulfilled
  // once macOS registers, or fails to register, for APNS
  std::vector<gin_helper::Promise<std::string>> apns_promise_set_;

#if BUILDFLAG(IS_MAC)
  v8::Local<v8::Promise> RegisterForAPNSNotifications(v8::Isolate* isolate);
  void UnregisterForAPNSNotifications();
#endif
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_PUSH_NOTIFICATIONS_H_
