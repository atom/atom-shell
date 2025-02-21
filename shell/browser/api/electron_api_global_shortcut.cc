// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_global_shortcut.h"

#include <vector>

#include "extensions/common/command.h"
#include "gin/dictionary.h"
#include "gin/handle.h"
#include "gin/object_template_builder.h"
#include "shell/browser/api/electron_api_system_preferences.h"
#include "shell/browser/browser.h"
#include "shell/common/gin_converters/accelerator_converter.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/node_includes.h"

#if BUILDFLAG(IS_MAC)
#include "base/mac/mac_util.h"
#endif

using extensions::Command;
using ui::GlobalAcceleratorListener;

namespace {

#if BUILDFLAG(IS_MAC)
bool RegisteringMediaKeyForUntrustedClient(const ui::Accelerator& accelerator) {
  return accelerator.IsMediaKey() &&
         !electron::api::SystemPreferences::IsTrustedAccessibilityClient(false);
}

bool MapHasMediaKeys(
    const std::map<ui::Accelerator, base::RepeatingClosure>& accelerator_map) {
  return std::ranges::any_of(
      accelerator_map, [](const auto& ac) { return ac.first.IsMediaKey(); });
}
#endif

}  // namespace

namespace electron::api {

gin::WrapperInfo GlobalShortcut::kWrapperInfo = {gin::kEmbedderNativeGin};

GlobalShortcut::GlobalShortcut(v8::Isolate* isolate) {}

GlobalShortcut::~GlobalShortcut() {
  UnregisterAll();
}

void GlobalShortcut::OnKeyPressed(const ui::Accelerator& accelerator) {
  if (!accelerator_callback_map_.contains(accelerator)) {
    // This should never occur, because if it does,
    // ui::GlobalAcceleratorListener notifies us with wrong accelerator.
    NOTREACHED();
  }
  accelerator_callback_map_[accelerator].Run();
}

void GlobalShortcut::ExecuteCommand(const extensions::ExtensionId& extension_id,
                                    const std::string& command_id) {
  // Ignore extension commands
}

bool GlobalShortcut::RegisterAll(
    const std::vector<ui::Accelerator>& accelerators,
    const base::RepeatingClosure& callback) {
  if (!electron::Browser::Get()->is_ready()) {
    gin_helper::ErrorThrower(JavascriptEnvironment::GetIsolate())
        .ThrowError("globalShortcut cannot be used before the app is ready");
    return false;
  }
  std::vector<ui::Accelerator> registered;

  for (auto& accelerator : accelerators) {
    if (!Register(accelerator, callback)) {
      // Unregister all shortcuts if any failed.
      UnregisterSome(registered);
      return false;
    }

    registered.push_back(accelerator);
  }
  return true;
}

bool GlobalShortcut::Register(const ui::Accelerator& accelerator,
                              const base::RepeatingClosure& callback) {
  if (!electron::Browser::Get()->is_ready()) {
    gin_helper::ErrorThrower(JavascriptEnvironment::GetIsolate())
        .ThrowError("globalShortcut cannot be used before the app is ready");
    return false;
  }
#if BUILDFLAG(IS_MAC)
  if (accelerator.IsMediaKey()) {
    if (RegisteringMediaKeyForUntrustedClient(accelerator))
      return false;

    ui::GlobalAcceleratorListener::SetShouldUseInternalMediaKeyHandling(false);
  }
#endif

  if (!ui::GlobalAcceleratorListener::GetInstance()->RegisterAccelerator(
          accelerator, this)) {
    return false;
  }

  accelerator_callback_map_[accelerator] = callback;
  return true;
}

void GlobalShortcut::Unregister(const ui::Accelerator& accelerator) {
  if (!electron::Browser::Get()->is_ready()) {
    gin_helper::ErrorThrower(JavascriptEnvironment::GetIsolate())
        .ThrowError("globalShortcut cannot be used before the app is ready");
    return;
  }
  if (accelerator_callback_map_.erase(accelerator) == 0)
    return;

#if BUILDFLAG(IS_MAC)
  if (accelerator.IsMediaKey() && !MapHasMediaKeys(accelerator_callback_map_)) {
    ui::GlobalAcceleratorListener::SetShouldUseInternalMediaKeyHandling(true);
  }
#endif

  ui::GlobalAcceleratorListener::GetInstance()->UnregisterAccelerator(
      accelerator, this);
}

void GlobalShortcut::UnregisterSome(
    const std::vector<ui::Accelerator>& accelerators) {
  for (auto& accelerator : accelerators) {
    Unregister(accelerator);
  }
}

bool GlobalShortcut::IsRegistered(const ui::Accelerator& accelerator) {
  return accelerator_callback_map_.contains(accelerator);
}

void GlobalShortcut::UnregisterAll() {
  if (!electron::Browser::Get()->is_ready()) {
    gin_helper::ErrorThrower(JavascriptEnvironment::GetIsolate())
        .ThrowError("globalShortcut cannot be used before the app is ready");
    return;
  }
  accelerator_callback_map_.clear();
  ui::GlobalAcceleratorListener::GetInstance()->UnregisterAccelerators(this);
}

// static
gin::Handle<GlobalShortcut> GlobalShortcut::Create(v8::Isolate* isolate) {
  return gin::CreateHandle(isolate, new GlobalShortcut(isolate));
}

// static
gin::ObjectTemplateBuilder GlobalShortcut::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin::Wrappable<GlobalShortcut>::GetObjectTemplateBuilder(isolate)
      .SetMethod("registerAll", &GlobalShortcut::RegisterAll)
      .SetMethod("register", &GlobalShortcut::Register)
      .SetMethod("isRegistered", &GlobalShortcut::IsRegistered)
      .SetMethod("unregister", &GlobalShortcut::Unregister)
      .SetMethod("unregisterAll", &GlobalShortcut::UnregisterAll);
}

const char* GlobalShortcut::GetTypeName() {
  return "GlobalShortcut";
}

}  // namespace electron::api

namespace {

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  gin::Dictionary dict(isolate, exports);
  dict.Set("globalShortcut", electron::api::GlobalShortcut::Create(isolate));
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_browser_global_shortcut, Initialize)
