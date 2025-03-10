// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_auto_updater.h"

#include "base/time/time.h"
#include "gin/handle.h"
#include "shell/browser/javascript_environment.h"
#include "shell/browser/native_window.h"
#include "shell/browser/window_list.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/time_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/event_emitter_caller.h"
#include "shell/common/gin_helper/object_template_builder.h"
#include "shell/common/node_includes.h"

namespace electron::api {

gin::WrapperInfo AutoUpdater::kWrapperInfo = {gin::kEmbedderNativeGin};

AutoUpdater::AutoUpdater() {
  auto_updater::AutoUpdater::SetDelegate(this);
}

AutoUpdater::~AutoUpdater() {
  auto_updater::AutoUpdater::SetDelegate(nullptr);
}

void AutoUpdater::OnError(const std::string& message) {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Object> wrapper;

  // We do not use gin::EmitEvent here because we do not want to
  // put this in its own CallbackScope and delegate to Node.js'
  // specialized handling for Error objects.
  if (GetWrapper(isolate).ToLocal(&wrapper)) {
    auto error = v8::Exception::Error(gin::StringToV8(isolate, message));
    std::vector<v8::Local<v8::Value>> args = {
        gin::StringToV8(isolate, "error"),
        gin::ConvertToV8(
            isolate,
            error->ToObject(isolate->GetCurrentContext()).ToLocalChecked()),
        gin::StringToV8(isolate, message),
    };

    gin_helper::MicrotasksScope microtasks_scope{
        wrapper->GetCreationContextChecked(), true,
        v8::MicrotasksScope::kRunMicrotasks};

    node::MakeCallback(isolate, wrapper, "emit", args.size(), args.data(),
                       {0, 0});
  }
}

void AutoUpdater::OnError(const std::string& message,
                          const int code,
                          const std::string& domain) {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Object> wrapper;
  if (GetWrapper(isolate).ToLocal(&wrapper)) {
    auto error = v8::Exception::Error(gin::StringToV8(isolate, message));
    auto errorObject =
        error->ToObject(isolate->GetCurrentContext()).ToLocalChecked();

    auto context = isolate->GetCurrentContext();

    // add two new params for better error handling
    errorObject
        ->Set(context, gin::StringToV8(isolate, "code"),
              v8::Integer::New(isolate, code))
        .Check();
    errorObject
        ->Set(context, gin::StringToV8(isolate, "domain"),
              gin::StringToV8(isolate, domain))
        .Check();

    gin_helper::EmitEvent(isolate, wrapper, "error", errorObject, message);
  }
}

void AutoUpdater::OnCheckingForUpdate() {
  Emit("checking-for-update");
}

void AutoUpdater::OnUpdateAvailable() {
  Emit("update-available");
}

void AutoUpdater::OnUpdateNotAvailable() {
  Emit("update-not-available");
}

void AutoUpdater::OnUpdateDownloaded(const std::string& release_notes,
                                     const std::string& release_name,
                                     const base::Time& release_date,
                                     const std::string& url) {
  Emit("update-downloaded", release_notes, release_name, release_date, url,
       // Keep compatibility with old APIs.
       base::BindRepeating(&AutoUpdater::QuitAndInstall,
                           base::Unretained(this)));
}

void AutoUpdater::OnWindowAllClosed() {
  QuitAndInstall();
}

void AutoUpdater::QuitAndInstall() {
  Emit("before-quit-for-update");

  // If we don't have any window then quitAndInstall immediately.
  if (WindowList::IsEmpty()) {
    auto_updater::AutoUpdater::QuitAndInstall();
    return;
  }

  // Otherwise do the restart after all windows have been closed.
  WindowList::AddObserver(this);
  WindowList::CloseAllWindows();
}

// static
gin::Handle<AutoUpdater> AutoUpdater::Create(v8::Isolate* isolate) {
  return gin::CreateHandle(isolate, new AutoUpdater());
}

gin::ObjectTemplateBuilder AutoUpdater::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin_helper::EventEmitterMixin<AutoUpdater>::GetObjectTemplateBuilder(
             isolate)
      .SetMethod("checkForUpdates", &auto_updater::AutoUpdater::CheckForUpdates)
      .SetMethod("getFeedURL", &auto_updater::AutoUpdater::GetFeedURL)
      .SetMethod("setFeedURL", &auto_updater::AutoUpdater::SetFeedURL)
#if DCHECK_IS_ON()
      .SetMethod("isVersionAllowedForUpdate",
                 &auto_updater::AutoUpdater::IsVersionAllowedForUpdate)
#endif
      .SetMethod("prepareUpdateFromFile",
                 &auto_updater::AutoUpdater::PrepareUpdateFromFile)
      .SetMethod("quitAndInstall", &AutoUpdater::QuitAndInstall);
}

const char* AutoUpdater::GetTypeName() {
  return "AutoUpdater";
}

}  // namespace electron::api

namespace {

using electron::api::AutoUpdater;

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary dict(isolate, exports);
  dict.Set("autoUpdater", AutoUpdater::Create(isolate));
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_browser_auto_updater, Initialize)
