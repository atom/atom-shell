// Copyright (c) 2024 Microsoft, GmbH.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/gin_converters/login_item_settings_converter.h"

#if BUILDFLAG(IS_MAC)
#include "base/mac/mac_util.h"
#endif

#include "shell/browser/browser.h"
#include "shell/common/gin_helper/dictionary.h"

namespace gin {

#if BUILDFLAG(IS_WIN)
bool Converter<electron::LaunchItem>::FromV8(v8::Isolate* isolate,
                                             v8::Local<v8::Value> val,
                                             electron::LaunchItem* out) {
  gin_helper::Dictionary dict;
  if (!ConvertFromV8(isolate, val, &dict))
    return false;

  dict.Get("name", &(out->name));
  dict.Get("path", &(out->path));
  dict.Get("args", &(out->args));
  dict.Get("scope", &(out->scope));
  dict.Get("enabled", &(out->enabled));
  return true;
}

v8::Local<v8::Value> Converter<electron::LaunchItem>::ToV8(
    v8::Isolate* isolate,
    electron::LaunchItem val) {
  auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
  dict.Set("name", val.name);
  dict.Set("path", val.path);
  dict.Set("args", val.args);
  dict.Set("scope", val.scope);
  dict.Set("enabled", val.enabled);
  return dict.GetHandle();
}
#endif

bool Converter<electron::LoginItemSettings>::FromV8(
    v8::Isolate* isolate,
    v8::Local<v8::Value> val,
    electron::LoginItemSettings* out) {
  gin_helper::Dictionary dict;
  if (!ConvertFromV8(isolate, val, &dict))
    return false;

  dict.Get("openAtLogin", &(out->open_at_login));
  dict.Get("openAsHidden", &(out->open_as_hidden));
  dict.Get("path", &(out->path));
  dict.Get("args", &(out->args));
#if BUILDFLAG(IS_WIN)
  dict.Get("name", &(out->name));
  dict.Get("enabled", &(out->enabled));
#elif BUILDFLAG(IS_MAC)
  dict.Get("serviceName", &(out->service_name));
  dict.Get("type", &(out->type));
#endif
  return true;
}

v8::Local<v8::Value> Converter<electron::LoginItemSettings>::ToV8(
    v8::Isolate* isolate,
    electron::LoginItemSettings val) {
  auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
#if BUILDFLAG(IS_WIN)
  dict.Set("launchItems", val.launch_items);
  dict.Set("executableWillLaunchAtLogin", val.executable_will_launch_at_login);
#elif BUILDFLAG(IS_MAC)
  if (base::mac::MacOSMajorVersion() >= 13)
    dict.Set("status", val.status);
#endif
  dict.Set("openAtLogin", val.open_at_login);
  dict.Set("openAsHidden", val.open_as_hidden);
  dict.Set("restoreState", val.restore_state);
  dict.Set("wasOpenedAtLogin", val.opened_at_login);
  dict.Set("wasOpenedAsHidden", val.opened_as_hidden);
  return dict.GetHandle();
}

}  // namespace gin
