// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/renderer/electron_sandboxed_renderer_client.h"

#include <iterator>
#include <tuple>
#include <vector>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/process/process_handle.h"
#include "base/process/process_metrics.h"
#include "content/public/renderer/render_frame.h"
#include "electron/buildflags/buildflags.h"
#include "shell/common/api/electron_bindings.h"
#include "shell/common/application_info.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/microtasks_scope.h"
#include "shell/common/node_bindings.h"
#include "shell/common/node_includes.h"
#include "shell/common/node_util.h"
#include "shell/common/options_switches.h"
#include "shell/renderer/electron_render_frame_observer.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
#include "third_party/blink/public/web/blink.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/electron_node/src/node_binding.h"

namespace electron {

namespace {

const char kEmitProcessEventKey[] = "emit-process-event";
const char kBindingCacheKey[] = "native-binding-cache";

v8::Local<v8::Object> GetBindingCache(v8::Isolate* isolate) {
  auto context = isolate->GetCurrentContext();
  gin_helper::Dictionary global(isolate, context->Global());
  v8::Local<v8::Value> cache;

  if (!global.GetHidden(kBindingCacheKey, &cache)) {
    cache = v8::Object::New(isolate);
    global.SetHidden(kBindingCacheKey, cache);
  }

  return cache->ToObject(context).ToLocalChecked();
}

// adapted from node.cc
v8::Local<v8::Value> GetBinding(v8::Isolate* isolate,
                                v8::Local<v8::String> key,
                                gin_helper::Arguments* margs) {
  v8::Local<v8::Object> exports;
  std::string binding_key = gin::V8ToString(isolate, key);
  gin_helper::Dictionary cache(isolate, GetBindingCache(isolate));

  if (cache.Get(binding_key.c_str(), &exports)) {
    return exports;
  }

  auto* mod = node::binding::get_linked_module(binding_key.c_str());

  if (!mod) {
    char errmsg[1024];
    snprintf(errmsg, sizeof(errmsg), "No such binding: %s",
             binding_key.c_str());
    margs->ThrowError(errmsg);
    return exports;
  }

  exports = v8::Object::New(isolate);
  DCHECK_EQ(mod->nm_register_func, nullptr);
  DCHECK_NE(mod->nm_context_register_func, nullptr);
  mod->nm_context_register_func(exports, v8::Null(isolate),
                                isolate->GetCurrentContext(), mod->nm_priv);
  cache.Set(binding_key.c_str(), exports);
  return exports;
}

v8::Local<v8::Value> CreatePreloadScript(v8::Isolate* isolate,
                                         v8::Local<v8::String> source) {
  auto context = isolate->GetCurrentContext();
  auto maybe_script = v8::Script::Compile(context, source);
  v8::Local<v8::Script> script;
  if (!maybe_script.ToLocal(&script))
    return v8::Local<v8::Value>();
  return script->Run(context).ToLocalChecked();
}

double Uptime() {
  return (base::Time::Now() - base::Process::Current().CreationTime())
      .InSecondsF();
}

void InvokeEmitProcessEvent(v8::Handle<v8::Context> context,
                            const std::string& event_name) {
  auto* isolate = context->GetIsolate();
  // set by sandboxed_renderer/init.js
  auto binding_key = gin::ConvertToV8(isolate, kEmitProcessEventKey)
                         ->ToString(context)
                         .ToLocalChecked();
  auto private_binding_key = v8::Private::ForApi(isolate, binding_key);
  auto global_object = context->Global();
  v8::Local<v8::Value> callback_value;
  if (!global_object->GetPrivate(context, private_binding_key)
           .ToLocal(&callback_value))
    return;
  if (callback_value.IsEmpty() || !callback_value->IsFunction())
    return;
  auto callback = callback_value.As<v8::Function>();
  v8::Local<v8::Value> args[] = {gin::ConvertToV8(isolate, event_name)};
  std::ignore =
      callback->Call(context, callback, std::size(args), std::data(args));
}

}  // namespace

ElectronSandboxedRendererClient::ElectronSandboxedRendererClient() {
  // Explicitly register electron's builtin bindings.
  NodeBindings::RegisterBuiltinBindings();
  metrics_ = base::ProcessMetrics::CreateCurrentProcessMetrics();
}

ElectronSandboxedRendererClient::~ElectronSandboxedRendererClient() = default;

void ElectronSandboxedRendererClient::InitializeBindings(
    v8::Local<v8::Object> binding,
    v8::Local<v8::Context> context,
    content::RenderFrame* render_frame) {
  auto* isolate = context->GetIsolate();
  gin_helper::Dictionary b(isolate, binding);
  b.SetMethod("get", GetBinding);
  b.SetMethod("createPreloadScript", CreatePreloadScript);

  auto process = gin_helper::Dictionary::CreateEmpty(isolate);
  b.Set("process", process);

  ElectronBindings::BindProcess(isolate, &process, metrics_.get());
  BindProcess(isolate, &process, render_frame);

  process.SetMethod("uptime", Uptime);
  process.Set("argv", base::CommandLine::ForCurrentProcess()->argv());
  process.SetReadOnly("pid", base::GetCurrentProcId());
  process.SetReadOnly("sandboxed", true);
  process.SetReadOnly("type", "renderer");
}

void ElectronSandboxedRendererClient::RenderFrameCreated(
    content::RenderFrame* render_frame) {
  new ElectronRenderFrameObserver(render_frame, this);
  RendererClientBase::RenderFrameCreated(render_frame);
}

void ElectronSandboxedRendererClient::RunScriptsAtDocumentStart(
    content::RenderFrame* render_frame) {
  RendererClientBase::RunScriptsAtDocumentStart(render_frame);
  EmitProcessEvent(render_frame, "document-start");
}

void ElectronSandboxedRendererClient::RunScriptsAtDocumentEnd(
    content::RenderFrame* render_frame) {
  RendererClientBase::RunScriptsAtDocumentEnd(render_frame);
  EmitProcessEvent(render_frame, "document-end");
}

void ElectronSandboxedRendererClient::DidCreateScriptContext(
    v8::Handle<v8::Context> context,
    content::RenderFrame* render_frame) {
  // Only allow preload for the main frame or
  // For devtools we still want to run the preload_bundle script
  // Or when nodeSupport is explicitly enabled in sub frames
  if (!ShouldLoadPreload(context, render_frame))
    return;

  injected_frames_.insert(render_frame);

  // Wrap the bundle into a function that receives the binding object as
  // argument.
  auto* isolate = context->GetIsolate();
  auto binding = v8::Object::New(isolate);
  InitializeBindings(binding, context, render_frame);

  std::vector<v8::Local<v8::String>> sandbox_preload_bundle_params = {
      node::FIXED_ONE_BYTE_STRING(isolate, "binding")};

  std::vector<v8::Local<v8::Value>> sandbox_preload_bundle_args = {binding};

  util::CompileAndCall(
      isolate->GetCurrentContext(), "electron/js2c/sandbox_bundle",
      &sandbox_preload_bundle_params, &sandbox_preload_bundle_args);

  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(context);
  InvokeEmitProcessEvent(context, "loaded");
}

void ElectronSandboxedRendererClient::WillReleaseScriptContext(
    v8::Handle<v8::Context> context,
    content::RenderFrame* render_frame) {
  if (injected_frames_.erase(render_frame) == 0)
    return;

  auto* isolate = context->GetIsolate();
  gin_helper::MicrotasksScope microtasks_scope(
      isolate, context->GetMicrotaskQueue(),
      v8::MicrotasksScope::kDoNotRunMicrotasks);
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(context);
  InvokeEmitProcessEvent(context, "exit");
}

void ElectronSandboxedRendererClient::EmitProcessEvent(
    content::RenderFrame* render_frame,
    const char* event_name) {
  if (!base::Contains(injected_frames_, render_frame))
    return;

  blink::WebLocalFrame* frame = render_frame->GetWebFrame();
  v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();
  v8::HandleScope handle_scope{isolate};

  v8::Local<v8::Context> context = GetContext(frame, isolate);
  gin_helper::MicrotasksScope microtasks_scope(
      isolate, context->GetMicrotaskQueue(),
      v8::MicrotasksScope::kDoNotRunMicrotasks);
  v8::Context::Scope context_scope(context);

  InvokeEmitProcessEvent(context, event_name);
}

}  // namespace electron
