// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/api/electron_bindings.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/contains.h"
#include "base/logging.h"
#include "base/process/process.h"
#include "base/process/process_handle.h"
#include "base/system/sys_info.h"
#include "services/resource_coordinator/public/cpp/memory_instrumentation/global_memory_dump.h"
#include "services/resource_coordinator/public/cpp/memory_instrumentation/memory_instrumentation.h"
#include "shell/browser/browser.h"
#include "shell/common/application_info.h"
#include "shell/common/gin_converters/file_path_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/microtasks_scope.h"
#include "shell/common/gin_helper/promise.h"
#include "shell/common/heap_snapshot.h"
#include "shell/common/node_includes.h"
#include "shell/common/process_util.h"
#include "shell/common/thread_restrictions.h"
#include "third_party/blink/renderer/platform/heap/process_heap.h"  // nogncheck

namespace electron {

ElectronBindings::ElectronBindings(uv_loop_t* loop) {
  uv_async_init(loop, call_next_tick_async_.get(), OnCallNextTick);
  call_next_tick_async_.get()->data = this;
  metrics_ = base::ProcessMetrics::CreateCurrentProcessMetrics();
}

ElectronBindings::~ElectronBindings() = default;

// static
void ElectronBindings::BindProcess(v8::Isolate* isolate,
                                   gin_helper::Dictionary* process,
                                   base::ProcessMetrics* metrics) {
  // These bindings are shared between sandboxed & unsandboxed renderers
  process->SetMethod("crash", &Crash);
  process->SetMethod("hang", &Hang);
  process->SetMethod("getCreationTime", &GetCreationTime);
  process->SetMethod("getHeapStatistics", &GetHeapStatistics);
  process->SetMethod("getBlinkMemoryInfo", &GetBlinkMemoryInfo);
  if (electron::IsBrowserProcess()) {
    process->SetMethod("getProcessMemoryInfo", &GetProcessMemoryInfo);
  }
  process->SetMethod("getSystemMemoryInfo", &GetSystemMemoryInfo);
  process->SetMethod("getSystemVersion",
                     &base::SysInfo::OperatingSystemVersion);
  process->SetMethod("getCPUUsage",
                     base::BindRepeating(&ElectronBindings::GetCPUUsage,
                                         base::Unretained(metrics)));

#if IS_MAS_BUILD()
  process->SetReadOnly("mas", true);
#endif

#if BUILDFLAG(IS_WIN)
  if (IsRunningInDesktopBridge())
    process->SetReadOnly("windowsStore", true);
#endif
}

void ElectronBindings::BindTo(v8::Isolate* isolate,
                              v8::Local<v8::Object> process) {
  gin_helper::Dictionary dict(isolate, process);
  BindProcess(isolate, &dict, metrics_.get());

  dict.SetMethod("takeHeapSnapshot", &TakeHeapSnapshot);
#if BUILDFLAG(IS_POSIX)
  dict.SetMethod("setFdLimit", &base::IncreaseFdLimitTo);
#endif
  dict.SetMethod("activateUvLoop",
                 base::BindRepeating(&ElectronBindings::ActivateUVLoop,
                                     base::Unretained(this)));
}

void ElectronBindings::EnvironmentDestroyed(node::Environment* env) {
  std::erase(pending_next_ticks_, env);
}

void ElectronBindings::ActivateUVLoop(v8::Isolate* isolate) {
  node::Environment* env = node::Environment::GetCurrent(isolate);
  if (base::Contains(pending_next_ticks_, env))
    return;

  pending_next_ticks_.push_back(env);
  uv_async_send(call_next_tick_async_.get());
}

// static
void ElectronBindings::OnCallNextTick(uv_async_t* handle) {
  auto* self = static_cast<ElectronBindings*>(handle->data);
  for (auto* env : self->pending_next_ticks_) {
    gin_helper::Locker locker(env->isolate());
    v8::Context::Scope context_scope(env->context());
    v8::HandleScope handle_scope(env->isolate());
    node::CallbackScope scope(env->isolate(), v8::Object::New(env->isolate()),
                              {0, 0});
  }

  self->pending_next_ticks_.clear();
}

// static
void ElectronBindings::Crash() {
  volatile int* zero = nullptr;
  *zero = 0;
}

// static
void ElectronBindings::Hang() {
  for (;;)
    base::PlatformThread::Sleep(base::Seconds(1));
}

// static
v8::Local<v8::Value> ElectronBindings::GetHeapStatistics(v8::Isolate* isolate) {
  v8::HeapStatistics v8_heap_stats;
  isolate->GetHeapStatistics(&v8_heap_stats);

  auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
  dict.Set("totalHeapSize",
           static_cast<double>(v8_heap_stats.total_heap_size() >> 10));
  dict.Set(
      "totalHeapSizeExecutable",
      static_cast<double>(v8_heap_stats.total_heap_size_executable() >> 10));
  dict.Set("totalPhysicalSize",
           static_cast<double>(v8_heap_stats.total_physical_size() >> 10));
  dict.Set("totalAvailableSize",
           static_cast<double>(v8_heap_stats.total_available_size() >> 10));
  dict.Set("usedHeapSize",
           static_cast<double>(v8_heap_stats.used_heap_size() >> 10));
  dict.Set("heapSizeLimit",
           static_cast<double>(v8_heap_stats.heap_size_limit() >> 10));
  dict.Set("mallocedMemory",
           static_cast<double>(v8_heap_stats.malloced_memory() >> 10));
  dict.Set("peakMallocedMemory",
           static_cast<double>(v8_heap_stats.peak_malloced_memory() >> 10));
  dict.Set("doesZapGarbage",
           static_cast<bool>(v8_heap_stats.does_zap_garbage()));

  return dict.GetHandle();
}

// static
v8::Local<v8::Value> ElectronBindings::GetCreationTime(v8::Isolate* isolate) {
  auto timeValue = base::Process::Current().CreationTime();
  if (timeValue.is_null()) {
    return v8::Null(isolate);
  }
  double jsTime = timeValue.InMillisecondsFSinceUnixEpoch();
  return v8::Number::New(isolate, jsTime);
}

// static
v8::Local<v8::Value> ElectronBindings::GetSystemMemoryInfo(
    v8::Isolate* isolate,
    gin_helper::Arguments* args) {
  base::SystemMemoryInfoKB mem_info;
  if (!base::GetSystemMemoryInfo(&mem_info)) {
    args->ThrowError("Unable to retrieve system memory information");
    return v8::Undefined(isolate);
  }

  auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
  dict.Set("total", mem_info.total);

  // See Chromium's "base/process/process_metrics.h" for an explanation.
  int free =
#if BUILDFLAG(IS_WIN)
      mem_info.avail_phys;
#else
      mem_info.free;
#endif
  dict.Set("free", free);

  // NB: These return bogus values on macOS
#if !BUILDFLAG(IS_MAC)
  dict.Set("swapTotal", mem_info.swap_total);
  dict.Set("swapFree", mem_info.swap_free);
#endif

  return dict.GetHandle();
}

// static
v8::Local<v8::Promise> ElectronBindings::GetProcessMemoryInfo(
    v8::Isolate* isolate) {
  CHECK(electron::IsBrowserProcess());
  gin_helper::Promise<gin_helper::Dictionary> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  if (!Browser::Get()->is_ready()) {
    promise.RejectWithErrorMessage(
        "Memory Info is available only after app ready");
    return handle;
  }

  v8::Global<v8::Context> context(isolate, isolate->GetCurrentContext());
  memory_instrumentation::MemoryInstrumentation::GetInstance()
      ->RequestGlobalDumpForPid(
          base::GetCurrentProcId(), std::vector<std::string>(),
          base::BindOnce(&ElectronBindings::DidReceiveMemoryDump,
                         std::move(context), std::move(promise),
                         base::GetCurrentProcId()));
  return handle;
}

// static
v8::Local<v8::Value> ElectronBindings::GetBlinkMemoryInfo(
    v8::Isolate* isolate) {
  auto allocated = blink::ProcessHeap::TotalAllocatedObjectSize();
  auto total = blink::ProcessHeap::TotalAllocatedSpace();

  auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
  dict.Set("allocated", static_cast<double>(allocated >> 10));
  dict.Set("total", static_cast<double>(total >> 10));
  return dict.GetHandle();
}

// static
void ElectronBindings::DidReceiveMemoryDump(
    v8::Global<v8::Context> context,
    gin_helper::Promise<gin_helper::Dictionary> promise,
    base::ProcessId target_pid,
    bool success,
    std::unique_ptr<memory_instrumentation::GlobalMemoryDump> global_dump) {
  v8::Isolate* isolate = promise.isolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> local_context =
      v8::Local<v8::Context>::New(isolate, context);
  gin_helper::MicrotasksScope microtasks_scope(
      isolate, local_context->GetMicrotaskQueue(), true);
  v8::Context::Scope context_scope(local_context);

  if (!success) {
    promise.RejectWithErrorMessage("Failed to create memory dump");
    return;
  }

  bool resolved = false;
  for (const memory_instrumentation::GlobalMemoryDump::ProcessDump& dump :
       global_dump->process_dumps()) {
    if (target_pid == dump.pid()) {
      auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
      const auto& osdump = dump.os_dump();
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_WIN)
      dict.Set("residentSet", osdump.resident_set_kb);
#endif
      dict.Set("private", osdump.private_footprint_kb);
      dict.Set("shared", osdump.shared_footprint_kb);
      promise.Resolve(dict);
      resolved = true;
      break;
    }
  }
  if (!resolved) {
    promise.RejectWithErrorMessage(
        R"(Failed to find current process memory details in memory dump)");
  }
}

// static
v8::Local<v8::Value> ElectronBindings::GetCPUUsage(
    base::ProcessMetrics* metrics,
    v8::Isolate* isolate) {
  auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
  int processor_count = base::SysInfo::NumberOfProcessors();

  // Default usage percentage to 0 for compatibility
  double usagePercent = 0;
  if (auto usage = metrics->GetCumulativeCPUUsage(); usage.has_value()) {
    dict.Set("cumulativeCPUUsage", usage->InSecondsF());
    usagePercent = metrics->GetPlatformIndependentCPUUsage(*usage);
  }

  dict.Set("percentCPUUsage", usagePercent / processor_count);

  // NB: This will throw NOTIMPLEMENTED() on Windows
  // For backwards compatibility, we'll return 0
#if !BUILDFLAG(IS_WIN)
  dict.Set("idleWakeupsPerSecond", metrics->GetIdleWakeupsPerSecond());
#else
  dict.Set("idleWakeupsPerSecond", 0);
#endif

  return dict.GetHandle();
}

// static
bool ElectronBindings::TakeHeapSnapshot(v8::Isolate* isolate,
                                        const base::FilePath& file_path) {
  ScopedAllowBlockingForElectron allow_blocking;

  base::File file(file_path,
                  base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);

  return electron::TakeHeapSnapshot(isolate, &file);
}

}  // namespace electron
