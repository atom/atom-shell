// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/javascript_environment.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

#include "base/allocator/partition_alloc_features.h"
#include "base/allocator/partition_allocator/src/partition_alloc/partition_alloc.h"
#include "base/bits.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/no_destructor.h"
#include "base/task/current_thread.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool/initialization_util.h"
#include "base/trace_event/trace_event.h"
#include "gin/array_buffer.h"
#include "gin/v8_initializer.h"
#include "shell/browser/microtasks_runner.h"
#include "shell/common/gin_helper/cleaned_up_at_exit.h"
#include "shell/common/node_includes.h"
#include "third_party/blink/public/common/switches.h"
#include "third_party/electron_node/src/node_wasm_web_api.h"

namespace {
v8::Isolate* g_isolate;
}

namespace gin {

class ConvertableToTraceFormatWrapper final
    : public base::trace_event::ConvertableToTraceFormat {
 public:
  explicit ConvertableToTraceFormatWrapper(
      std::unique_ptr<v8::ConvertableToTraceFormat> inner)
      : inner_(std::move(inner)) {}
  ~ConvertableToTraceFormatWrapper() override = default;

  // disable copy
  ConvertableToTraceFormatWrapper(const ConvertableToTraceFormatWrapper&) =
      delete;
  ConvertableToTraceFormatWrapper& operator=(
      const ConvertableToTraceFormatWrapper&) = delete;

  void AppendAsTraceFormat(std::string* out) const final {
    inner_->AppendAsTraceFormat(out);
  }

 private:
  std::unique_ptr<v8::ConvertableToTraceFormat> inner_;
};

}  // namespace gin

// Allow std::unique_ptr<v8::ConvertableToTraceFormat> to be a valid
// initialization value for trace macros.
template <>
struct base::trace_event::TraceValue::Helper<
    std::unique_ptr<v8::ConvertableToTraceFormat>> {
  static constexpr unsigned char kType = TRACE_VALUE_TYPE_CONVERTABLE;
  static inline void SetValue(
      TraceValue* v,
      std::unique_ptr<v8::ConvertableToTraceFormat> value) {
    // NOTE: |as_convertable| is an owning pointer, so using new here
    // is acceptable.
    v->as_convertable =
        new gin::ConvertableToTraceFormatWrapper(std::move(value));
  }
};

namespace electron {

namespace {

gin::IsolateHolder CreateIsolateHolder(v8::Isolate* isolate) {
  std::unique_ptr<v8::Isolate::CreateParams> create_params =
      gin::IsolateHolder::getDefaultIsolateParams();
  // Align behavior with V8 Isolate default for Node.js.
  // This is necessary for important aspects of Node.js
  // including heap and cpu profilers to function properly.

  return gin::IsolateHolder(
      base::SingleThreadTaskRunner::GetCurrentDefault(),
      gin::IsolateHolder::kSingleThread,
      gin::IsolateHolder::IsolateType::kUtility, std::move(create_params),
      gin::IsolateHolder::IsolateCreationMode::kNormal, nullptr, isolate);
}

}  // namespace

JavascriptEnvironment::JavascriptEnvironment(uv_loop_t* event_loop,
                                             bool setup_wasm_streaming)
    : isolate_holder_{CreateIsolateHolder(
          Initialize(event_loop, setup_wasm_streaming))},
      isolate_{isolate_holder_.isolate()},
      locker_{isolate_} {
  isolate_->Enter();

  v8::HandleScope scope(isolate_);
  auto context = node::NewContext(isolate_);
  CHECK(!context.IsEmpty());

  context->Enter();
}

JavascriptEnvironment::~JavascriptEnvironment() {
  DCHECK_NE(platform_, nullptr);

  {
    v8::HandleScope scope(isolate_);
    isolate_->GetCurrentContext()->Exit();
  }
  isolate_->Exit();
  g_isolate = nullptr;

  platform_->UnregisterIsolate(isolate_);
}

v8::Isolate* JavascriptEnvironment::Initialize(uv_loop_t* event_loop,
                                               bool setup_wasm_streaming) {
  auto* cmd = base::CommandLine::ForCurrentProcess();
  // --js-flags.
  std::string js_flags = "--no-freeze-flags-after-init ";
  js_flags.append(cmd->GetSwitchValueASCII(blink::switches::kJavaScriptFlags));
  v8::V8::SetFlagsFromString(js_flags.c_str(), js_flags.size());

  // The V8Platform of gin relies on Chromium's task schedule, which has not
  // been started at this point, so we have to rely on Node's V8Platform.
  auto* tracing_agent = node::CreateAgent();
  auto* tracing_controller = tracing_agent->GetTracingController();
  node::tracing::TraceEventHelper::SetAgent(tracing_agent);
  platform_ = node::MultiIsolatePlatform::Create(
      base::RecommendedMaxNumberOfThreadsInThreadGroup(3, 8, 0.1, 0),
      tracing_controller, gin::V8Platform::GetCurrentPageAllocator());

  v8::V8::InitializePlatform(platform_.get());
  gin::IsolateHolder::Initialize(gin::IsolateHolder::kNonStrictMode,
                                 gin::ArrayBufferAllocator::SharedInstance(),
                                 nullptr /* external_reference_table */,
                                 js_flags, nullptr /* fatal_error_callback */,
                                 nullptr /* oom_error_callback */,
                                 false /* create_v8_platform */);

  v8::Isolate* isolate = v8::Isolate::Allocate();
  platform_->RegisterIsolate(isolate, event_loop);

  // This is done here because V8 checks for the callback in NewContext.
  // Our setup order doesn't allow for calling SetupIsolateForNode
  // before NewContext without polluting JavaScriptEnvironment with
  // Node.js logic and so we conditionally do it here to keep
  // concerns separate.
  if (setup_wasm_streaming) {
    isolate->SetWasmStreamingCallback(
        node::wasm_web_api::StartStreamingCompilation);
  }

  g_isolate = isolate;

  return isolate;
}

// static
v8::Isolate* JavascriptEnvironment::GetIsolate() {
  CHECK(g_isolate);
  return g_isolate;
}

void JavascriptEnvironment::CreateMicrotasksRunner() {
  DCHECK(!microtasks_runner_);
  microtasks_runner_ = std::make_unique<MicrotasksRunner>(isolate());
  base::CurrentThread::Get()->AddTaskObserver(microtasks_runner_.get());
}

void JavascriptEnvironment::DestroyMicrotasksRunner() {
  DCHECK(microtasks_runner_);
  {
    v8::HandleScope scope(isolate_);
    gin_helper::CleanedUpAtExit::DoCleanup();
  }
  base::CurrentThread::Get()->RemoveTaskObserver(microtasks_runner_.get());
}

}  // namespace electron
