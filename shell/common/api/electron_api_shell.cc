// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <string>

#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/file_path_converter.h"
#include "shell/common/gin_converters/guid_converter.h"
#include "shell/common/gin_converters/gurl_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/error_thrower.h"
#include "shell/common/gin_helper/promise.h"
#include "shell/common/node_includes.h"
#include "shell/common/platform_util.h"

#if BUILDFLAG(IS_WIN)
#include "base/win/scoped_com_initializer.h"
#include "base/win/shortcut.h"
#include "shell/common/thread_restrictions.h"

namespace gin {

template <>
struct Converter<base::win::ShortcutOperation> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Handle<v8::Value> val,
                     base::win::ShortcutOperation* out) {
    std::string operation;
    if (!ConvertFromV8(isolate, val, &operation))
      return false;
    if (operation.empty() || operation == "create")
      *out = base::win::ShortcutOperation::kCreateAlways;
    else if (operation == "update")
      *out = base::win::ShortcutOperation::kUpdateExisting;
    else if (operation == "replace")
      *out = base::win::ShortcutOperation::kReplaceExisting;
    else
      return false;
    return true;
  }
};

}  // namespace gin
#endif

namespace {

void OnOpenFinished(gin_helper::Promise<void> promise,
                    const std::string& error) {
  if (error.empty())
    promise.Resolve();
  else
    promise.RejectWithErrorMessage(error);
}

v8::Local<v8::Promise> OpenExternal(const GURL& url, gin::Arguments* args) {
  gin_helper::Promise<void> promise(args->isolate());
  v8::Local<v8::Promise> handle = promise.GetHandle();

  platform_util::OpenExternalOptions options;
  gin_helper::Dictionary obj;
  if (args->GetNext(&obj)) {
    obj.Get("activate", &options.activate);
    obj.Get("workingDirectory", &options.working_dir);
    obj.Get("logUsage", &options.log_usage);
  }

  platform_util::OpenExternal(
      url, options, base::BindOnce(&OnOpenFinished, std::move(promise)));
  return handle;
}

v8::Local<v8::Promise> OpenPath(v8::Isolate* isolate,
                                const base::FilePath& full_path) {
  gin_helper::Promise<const std::string&> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  platform_util::OpenPath(
      full_path,
      base::BindOnce(
          [](gin_helper::Promise<const std::string&> promise,
             const std::string& err_msg) { promise.Resolve(err_msg); },
          std::move(promise)));
  return handle;
}

v8::Local<v8::Promise> TrashItem(v8::Isolate* isolate,
                                 const base::FilePath& path) {
  gin_helper::Promise<void> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  platform_util::TrashItem(
      path, base::BindOnce(
                [](gin_helper::Promise<void> promise, bool success,
                   const std::string& error) {
                  if (success) {
                    promise.Resolve();
                  } else {
                    promise.RejectWithErrorMessage(error);
                  }
                },
                std::move(promise)));
  return handle;
}

#if BUILDFLAG(IS_WIN)

bool WriteShortcutLink(const base::FilePath& shortcut_path,
                       gin_helper::Arguments* args) {
  base::win::ShortcutOperation operation =
      base::win::ShortcutOperation::kCreateAlways;
  args->GetNext(&operation);
  auto options = gin::Dictionary::CreateEmpty(args->isolate());
  if (!args->GetNext(&options)) {
    args->ThrowError();
    return false;
  }

  base::win::ShortcutProperties properties;
  base::FilePath path;
  std::wstring str;
  UUID toastActivatorClsid;
  int index;
  if (options.Get("target", &path))
    properties.set_target(path);
  if (options.Get("cwd", &path))
    properties.set_working_dir(path);
  if (options.Get("args", &str))
    properties.set_arguments(str);
  if (options.Get("description", &str))
    properties.set_description(str);
  if (options.Get("icon", &path) && options.Get("iconIndex", &index))
    properties.set_icon(path, index);
  if (options.Get("appUserModelId", &str))
    properties.set_app_id(str);
  if (options.Get("toastActivatorClsid", &toastActivatorClsid))
    properties.set_toast_activator_clsid(toastActivatorClsid);

  electron::ScopedAllowBlockingForElectron allow_blocking;
  base::win::ScopedCOMInitializer com_initializer;
  return base::win::CreateOrUpdateShortcutLink(shortcut_path, properties,
                                               operation);
}

v8::Local<v8::Value> ReadShortcutLink(gin_helper::ErrorThrower thrower,
                                      const base::FilePath& path) {
  using base::win::ShortcutProperties;
  auto options = gin::Dictionary::CreateEmpty(thrower.isolate());
  electron::ScopedAllowBlockingForElectron allow_blocking;
  base::win::ScopedCOMInitializer com_initializer;
  base::win::ShortcutProperties properties;
  if (!base::win::ResolveShortcutProperties(
          path, ShortcutProperties::PROPERTIES_ALL, &properties)) {
    thrower.ThrowError("Failed to read shortcut link");
    return v8::Null(thrower.isolate());
  }
  options.Set("target", properties.target);
  options.Set("cwd", properties.working_dir);
  options.Set("args", properties.arguments);
  options.Set("description", properties.description);
  options.Set("icon", properties.icon);
  options.Set("iconIndex", properties.icon_index);
  options.Set("appUserModelId", properties.app_id);
  options.Set("toastActivatorClsid", properties.toast_activator_clsid);
  return gin::ConvertToV8(thrower.isolate(), options);
}
#endif

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  gin_helper::Dictionary dict(context->GetIsolate(), exports);
  dict.SetMethod("showItemInFolder", &platform_util::ShowItemInFolder);
  dict.SetMethod("openPath", &OpenPath);
  dict.SetMethod("openExternal", &OpenExternal);
  dict.SetMethod("trashItem", &TrashItem);
  dict.SetMethod("beep", &platform_util::Beep);
#if BUILDFLAG(IS_WIN)
  dict.SetMethod("writeShortcutLink", &WriteShortcutLink);
  dict.SetMethod("readShortcutLink", &ReadShortcutLink);
#endif
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_common_shell, Initialize)
