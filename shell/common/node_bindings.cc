// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/node_bindings.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/containers/fixed_flat_set.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/trace_event/trace_event.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/content_paths.h"
#include "electron/buildflags/buildflags.h"
#include "electron/fuses.h"
#include "shell/browser/api/electron_api_app.h"
#include "shell/common/api/electron_bindings.h"
#include "shell/common/electron_command_line.h"
#include "shell/common/gin_converters/file_path_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/event.h"
#include "shell/common/gin_helper/event_emitter_caller.h"
#include "shell/common/gin_helper/locker.h"
#include "shell/common/gin_helper/microtasks_scope.h"
#include "shell/common/mac/main_application_bundle.h"
#include "shell/common/node_includes.h"
#include "shell/common/options_switches.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_initializer.h"  // nogncheck
#include "third_party/electron_node/src/debug_utils.h"

#if !IS_MAS_BUILD()
#include "shell/common/crash_keys.h"
#endif

#define ELECTRON_BROWSER_BINDINGS(V)     \
  V(electron_browser_app)                \
  V(electron_browser_auto_updater)       \
  V(electron_browser_browser_view)       \
  V(electron_browser_content_tracing)    \
  V(electron_browser_crash_reporter)     \
  V(electron_browser_desktop_capturer)   \
  V(electron_browser_dialog)             \
  V(electron_browser_event_emitter)      \
  V(electron_browser_global_shortcut)    \
  V(electron_browser_in_app_purchase)    \
  V(electron_browser_menu)               \
  V(electron_browser_message_port)       \
  V(electron_browser_native_theme)       \
  V(electron_browser_net)                \
  V(electron_browser_notification)       \
  V(electron_browser_power_monitor)      \
  V(electron_browser_power_save_blocker) \
  V(electron_browser_protocol)           \
  V(electron_browser_printing)           \
  V(electron_browser_push_notifications) \
  V(electron_browser_safe_storage)       \
  V(electron_browser_session)            \
  V(electron_browser_screen)             \
  V(electron_browser_system_preferences) \
  V(electron_browser_base_window)        \
  V(electron_browser_tray)               \
  V(electron_browser_utility_process)    \
  V(electron_browser_view)               \
  V(electron_browser_web_contents)       \
  V(electron_browser_web_contents_view)  \
  V(electron_browser_web_frame_main)     \
  V(electron_browser_web_view_manager)   \
  V(electron_browser_window)

#define ELECTRON_COMMON_BINDINGS(V)   \
  V(electron_common_asar)             \
  V(electron_common_clipboard)        \
  V(electron_common_command_line)     \
  V(electron_common_crashpad_support) \
  V(electron_common_environment)      \
  V(electron_common_features)         \
  V(electron_common_native_image)     \
  V(electron_common_shell)            \
  V(electron_common_v8_util)

#define ELECTRON_RENDERER_BINDINGS(V) \
  V(electron_renderer_context_bridge) \
  V(electron_renderer_crash_reporter) \
  V(electron_renderer_ipc)            \
  V(electron_renderer_web_frame)

#define ELECTRON_UTILITY_BINDINGS(V) V(electron_utility_parent_port)

#define ELECTRON_VIEWS_BINDINGS(V) V(electron_browser_image_view)

#define ELECTRON_TESTING_BINDINGS(V) V(electron_common_testing)

// This is used to load built-in bindings. Instead of using
// __attribute__((constructor)), we call the _register_<modname>
// function for each built-in bindings explicitly. This is only
// forward declaration. The definitions are in each binding's
// implementation when calling the NODE_LINKED_BINDING_CONTEXT_AWARE.
#define V(modname) void _register_##modname();
ELECTRON_BROWSER_BINDINGS(V)
ELECTRON_COMMON_BINDINGS(V)
ELECTRON_RENDERER_BINDINGS(V)
ELECTRON_UTILITY_BINDINGS(V)
#if BUILDFLAG(ENABLE_VIEWS_API)
ELECTRON_VIEWS_BINDINGS(V)
#endif
#if DCHECK_IS_ON()
ELECTRON_TESTING_BINDINGS(V)
#endif
#undef V

namespace {

void stop_and_close_uv_loop(uv_loop_t* loop) {
  uv_stop(loop);

  auto const ensure_closing = [](uv_handle_t* handle, void*) {
    // We should be using the UvHandle wrapper everywhere, in which case
    // all handles should already be in a closing state...
    DCHECK(uv_is_closing(handle));
    // ...but if a raw handle got through, through, do the right thing anyway
    if (!uv_is_closing(handle)) {
      uv_close(handle, nullptr);
    }
  };

  uv_walk(loop, ensure_closing, nullptr);

  // All remaining handles are in a closing state now.
  // Pump the event loop so that they can finish closing.
  for (;;)
    if (uv_run(loop, UV_RUN_DEFAULT) == 0)
      break;

  DCHECK_EQ(0, uv_loop_alive(loop));
  node::CheckedUvLoopClose(loop);
}

bool g_is_initialized = false;

void V8FatalErrorCallback(const char* location, const char* message) {
  LOG(ERROR) << "Fatal error in V8: " << location << " " << message;

#if !IS_MAS_BUILD()
  electron::crash_keys::SetCrashKey("electron.v8-fatal.message", message);
  electron::crash_keys::SetCrashKey("electron.v8-fatal.location", location);
#endif

  volatile int* zero = nullptr;
  *zero = 0;
}

bool AllowWasmCodeGenerationCallback(v8::Local<v8::Context> context,
                                     v8::Local<v8::String> source) {
  // If we're running with contextIsolation enabled in the renderer process,
  // fall back to Blink's logic.
  if (node::Environment::GetCurrent(context) == nullptr) {
    if (!electron::IsRendererProcess())
      return false;
    return blink::V8Initializer::WasmCodeGenerationCheckCallbackInMainThread(
        context, source);
  }

  return node::AllowWasmCodeGenerationCallback(context, source);
}

v8::ModifyCodeGenerationFromStringsResult ModifyCodeGenerationFromStrings(
    v8::Local<v8::Context> context,
    v8::Local<v8::Value> source,
    bool is_code_like) {
  if (node::Environment::GetCurrent(context) == nullptr) {
    // No node environment means we're in the renderer process, either in a
    // sandboxed renderer or in an unsandboxed renderer with context isolation
    // enabled.
    if (!electron::IsRendererProcess()) {
      NOTREACHED();
      return {false, {}};
    }
    return blink::V8Initializer::CodeGenerationCheckCallbackInMainThread(
        context, source, is_code_like);
  }

  // If we get here then we have a node environment, so either a) we're in the
  // non-rendrer process, or b) we're in the renderer process in a context that
  // has both node and blink, i.e. contextIsolation disabled.

  // If we're in the renderer with contextIsolation disabled, ask blink first
  // (for CSP), and iff that allows codegen, delegate to node.
  if (electron::IsRendererProcess()) {
    v8::ModifyCodeGenerationFromStringsResult result =
        blink::V8Initializer::CodeGenerationCheckCallbackInMainThread(
            context, source, is_code_like);
    if (!result.codegen_allowed)
      return result;
  }

  // If we're in the main process or utility process, delegate to node.
  return node::ModifyCodeGenerationFromStrings(context, source, is_code_like);
}

void ErrorMessageListener(v8::Local<v8::Message> message,
                          v8::Local<v8::Value> data) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  node::Environment* env = node::Environment::GetCurrent(isolate);
  if (env) {
    gin_helper::MicrotasksScope microtasks_scope(
        isolate, env->context()->GetMicrotaskQueue(),
        v8::MicrotasksScope::kDoNotRunMicrotasks);
    // Emit the after() hooks now that the exception has been handled.
    // Analogous to node/lib/internal/process/execution.js#L176-L180
    if (env->async_hooks()->fields()[node::AsyncHooks::kAfter]) {
      while (env->async_hooks()->fields()[node::AsyncHooks::kStackLength]) {
        node::AsyncWrap::EmitAfter(env, env->execution_async_id());
        env->async_hooks()->pop_async_context(env->execution_async_id());
      }
    }

    // Ensure that the async id stack is properly cleared so the async
    // hook stack does not become corrupted.
    env->async_hooks()->clear_async_id_stack();
  }
}

// Only allow DebugOptions in non-ELECTRON_RUN_AS_NODE mode.
// If node CLI inspect support is disabled, allow no debug options.
bool IsAllowedDebugOption(base::StringPiece option) {
  static constexpr auto options = base::MakeFixedFlatSet<base::StringPiece>({
      "--debug",
      "--debug-brk",
      "--debug-port",
      "--inspect",
      "--inspect-brk",
      "--inspect-brk-node",
      "--inspect-port",
      "--inspect-publish-uid",
  });

  return electron::fuses::IsNodeCliInspectEnabled() && options.contains(option);
}

// Initialize NODE_OPTIONS to pass to Node.js
// See https://nodejs.org/api/cli.html#cli_node_options_options
void SetNodeOptions(base::Environment* env) {
  // Options that are unilaterally disallowed
  static constexpr auto disallowed = base::MakeFixedFlatSet<base::StringPiece>({
      "--enable-fips",
      "--force-fips",
      "--openssl-config",
      "--use-bundled-ca",
      "--use-openssl-ca",
  });

  static constexpr auto pkg_opts = base::MakeFixedFlatSet<base::StringPiece>({
      "--http-parser",
      "--max-http-header-size",
  });

  if (env->HasVar("NODE_OPTIONS")) {
    if (electron::fuses::IsNodeOptionsEnabled()) {
      std::string options;
      env->GetVar("NODE_OPTIONS", &options);
      std::vector<std::string> parts = base::SplitString(
          options, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

      bool is_packaged_app = electron::api::App::IsPackaged();

      for (const auto& part : parts) {
        // Strip off values passed to individual NODE_OPTIONs
        std::string option = part.substr(0, part.find('='));

        if (is_packaged_app && !pkg_opts.contains(option)) {
          // Explicitly disallow majority of NODE_OPTIONS in packaged apps
          LOG(ERROR) << "Most NODE_OPTIONs are not supported in packaged apps."
                     << " See documentation for more details.";
          options.erase(options.find(option), part.length());
        } else if (disallowed.contains(option)) {
          // Remove NODE_OPTIONS specifically disallowed for use in Node.js
          // through Electron owing to constraints like BoringSSL.
          LOG(ERROR) << "The NODE_OPTION " << option
                     << " is not supported in Electron";
          options.erase(options.find(option), part.length());
        }
      }

      // overwrite new NODE_OPTIONS without unsupported variables
      env->SetVar("NODE_OPTIONS", options);
    } else {
      LOG(ERROR) << "NODE_OPTIONS have been disabled in this app";
      env->SetVar("NODE_OPTIONS", "");
    }
  }
}

}  // namespace

namespace electron {

namespace {

base::FilePath GetResourcesPath() {
  if (!electron::fuses::IsEmbeddedAsarIntegrityValidationEnabled() &&
      !electron::api::App::IsPackaged()) {
    base::FilePath resources_path =
        base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
            switches::kResourcesPath);
    if (!resources_path.empty()) {
      if (base::PathExists(resources_path)) {
        return resources_path;
      }
      LOG(ERROR) << "--resources-path points to a non-existent directory";
      resources_path.clear();
    }
  }
#if BUILDFLAG(IS_MAC)
  return MainApplicationBundlePath().Append("Contents").Append("Resources");
#else
  auto* command_line = base::CommandLine::ForCurrentProcess();
  base::FilePath exec_path(command_line->GetProgram());
  base::PathService::Get(base::FILE_EXE, &exec_path);

  return exec_path.DirName().Append(FILE_PATH_LITERAL("resources"));
#endif
}

}  // namespace

NodeBindings::NodeBindings(BrowserEnvironment browser_env)
    : browser_env_(browser_env) {
  if (browser_env == BrowserEnvironment::kWorker) {
    uv_loop_init(&worker_loop_);
    uv_loop_ = &worker_loop_;
  } else {
    uv_loop_ = uv_default_loop();
  }

  // Interrupt embed polling when a handle is started.
  uv_loop_configure(uv_loop_, UV_LOOP_INTERRUPT_ON_IO_CHANGE);
}

NodeBindings::~NodeBindings() {
  // Quit the embed thread.
  embed_closed_ = true;
  uv_sem_post(&embed_sem_);

  WakeupEmbedThread();

  // Wait for everything to be done.
  uv_thread_join(&embed_thread_);

  // Clear uv.
  uv_sem_destroy(&embed_sem_);
  dummy_uv_handle_.reset();

  // Clean up worker loop
  if (in_worker_loop())
    stop_and_close_uv_loop(uv_loop_);
}

void NodeBindings::RegisterBuiltinBindings() {
#define V(modname) _register_##modname();
  if (IsBrowserProcess()) {
    ELECTRON_BROWSER_BINDINGS(V)
#if BUILDFLAG(ENABLE_VIEWS_API)
    ELECTRON_VIEWS_BINDINGS(V)
#endif
  }
  ELECTRON_COMMON_BINDINGS(V)
  if (IsRendererProcess()) {
    ELECTRON_RENDERER_BINDINGS(V)
  }
  if (IsUtilityProcess()) {
    ELECTRON_UTILITY_BINDINGS(V)
  }
#if DCHECK_IS_ON()
  ELECTRON_TESTING_BINDINGS(V)
#endif
#undef V
}

bool NodeBindings::IsInitialized() {
  return g_is_initialized;
}

// Initialize Node.js cli options to pass to Node.js
// See https://nodejs.org/api/cli.html#cli_options
void NodeBindings::SetNodeCliFlags() {
  const auto argv = base::CommandLine::ForCurrentProcess()->argv();
  std::vector<std::string> args;

  // TODO(codebytere): We need to set the first entry in args to the
  // process name owing to src/node_options-inl.h#L286-L290 but this is
  // redundant and so should be refactored upstream.
  args.reserve(argv.size() + 1);
  args.emplace_back("electron");

  for (const auto& arg : argv) {
#if BUILDFLAG(IS_WIN)
    const auto& option = base::WideToUTF8(arg);
#else
    const auto& option = arg;
#endif
    const auto stripped = base::StringPiece(option).substr(0, option.find('='));

    // Only allow in no-op (--) option or DebugOptions
    if (IsAllowedDebugOption(stripped) || stripped == "--")
      args.push_back(option);
  }

  // We need to disable Node.js' fetch implementation to prevent
  // conflict with Blink's in renderer and worker processes.
  if (browser_env_ == BrowserEnvironment::kRenderer ||
      browser_env_ == BrowserEnvironment::kWorker) {
    args.push_back("--no-experimental-fetch");
  }

  std::vector<std::string> errors;
  const int exit_code = ProcessGlobalArgs(&args, nullptr, &errors,
                                          node::kDisallowedInEnvironment);

  const std::string err_str = "Error parsing Node.js cli flags ";
  if (exit_code != 0) {
    LOG(ERROR) << err_str;
  } else if (!errors.empty()) {
    LOG(ERROR) << err_str << base::JoinString(errors, " ");
  }
}

void NodeBindings::Initialize(v8::Local<v8::Context> context) {
  TRACE_EVENT0("electron", "NodeBindings::Initialize");
  // Open node's error reporting system for browser process.

#if BUILDFLAG(IS_LINUX)
  // Get real command line in renderer process forked by zygote.
  if (browser_env_ != BrowserEnvironment::kBrowser)
    ElectronCommandLine::InitializeFromCommandLine();
#endif

  // Explicitly register electron's builtin bindings.
  RegisterBuiltinBindings();

  // Parse and set Node.js cli flags.
  SetNodeCliFlags();

  auto env = base::Environment::Create();
  SetNodeOptions(env.get());

  std::vector<std::string> argv = {"electron"};
  std::vector<std::string> exec_argv;
  std::vector<std::string> errors;
  uint64_t process_flags = node::ProcessFlags::kNoFlags;
  // We do not want the child processes spawned from the utility process
  // to inherit the custom stdio handles created for the parent.
  if (browser_env_ != BrowserEnvironment::kUtility)
    process_flags |= node::ProcessFlags::kEnableStdioInheritance;
  if (!fuses::IsNodeOptionsEnabled())
    process_flags |= node::ProcessFlags::kDisableNodeOptionsEnv;

  int exit_code = node::InitializeNodeWithArgs(
      &argv, &exec_argv, &errors,
      static_cast<node::ProcessFlags::Flags>(process_flags));

  for (const std::string& error : errors)
    fprintf(stderr, "%s: %s\n", argv[0].c_str(), error.c_str());

  if (exit_code != 0)
    exit(exit_code);

#if BUILDFLAG(IS_WIN)
  // uv_init overrides error mode to suppress the default crash dialog, bring
  // it back if user wants to show it.
  if (browser_env_ == BrowserEnvironment::kBrowser ||
      env->HasVar("ELECTRON_DEFAULT_ERROR_MODE"))
    SetErrorMode(GetErrorMode() & ~SEM_NOGPFAULTERRORBOX);
#endif

  gin_helper::internal::Event::GetConstructor(context);

  g_is_initialized = true;
}

node::Environment* NodeBindings::CreateEnvironment(
    v8::Handle<v8::Context> context,
    node::MultiIsolatePlatform* platform,
    std::vector<std::string> args,
    std::vector<std::string> exec_args) {
  // Feed node the path to initialization script.
  std::string process_type;
  switch (browser_env_) {
    case BrowserEnvironment::kBrowser:
      process_type = "browser";
      break;
    case BrowserEnvironment::kRenderer:
      process_type = "renderer";
      break;
    case BrowserEnvironment::kWorker:
      process_type = "worker";
      break;
    case BrowserEnvironment::kUtility:
      process_type = "utility";
      break;
  }

  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary global(isolate, context->Global());

  if (browser_env_ == BrowserEnvironment::kBrowser) {
    const std::vector<std::string> search_paths = {"app.asar", "app",
                                                   "default_app.asar"};
    const std::vector<std::string> app_asar_search_paths = {"app.asar"};
    context->Global()->SetPrivate(
        context,
        v8::Private::ForApi(
            isolate,
            gin::ConvertToV8(isolate, "appSearchPaths").As<v8::String>()),
        gin::ConvertToV8(isolate,
                         electron::fuses::IsOnlyLoadAppFromAsarEnabled()
                             ? app_asar_search_paths
                             : search_paths));
  }

  base::FilePath resources_path = GetResourcesPath();
  std::string init_script = "electron/js2c/" + process_type + "_init";

  args.insert(args.begin() + 1, init_script);

  if (!isolate_data_)
    isolate_data_ = node::CreateIsolateData(isolate, uv_loop_, platform);

  node::Environment* env;
  uint64_t flags = node::EnvironmentFlags::kDefaultFlags |
                   node::EnvironmentFlags::kHideConsoleWindows |
                   node::EnvironmentFlags::kNoGlobalSearchPaths;

  if (browser_env_ == BrowserEnvironment::kRenderer ||
      browser_env_ == BrowserEnvironment::kWorker) {
    // Only one ESM loader can be registered per isolate -
    // in renderer processes this should be blink. We need to tell Node.js
    // not to register its handler (overriding blinks) in non-browser processes.
    // We also avoid overriding globals like setImmediate, clearImmediate
    // queueMicrotask etc during the bootstrap phase of Node.js
    // for processes that already have these defined by DOM.
    // Check //third_party/electron_node/lib/internal/bootstrap/node.js
    // for the list of overrides on globalThis.
    flags |= node::EnvironmentFlags::kNoRegisterESMLoader |
             node::EnvironmentFlags::kNoBrowserGlobals |
             node::EnvironmentFlags::kNoCreateInspector;
  }

  if (!electron::fuses::IsNodeCliInspectEnabled()) {
    // If --inspect and friends are disabled we also shouldn't listen for
    // SIGUSR1
    flags |= node::EnvironmentFlags::kNoStartDebugSignalHandler;
  }

  {
    v8::TryCatch try_catch(isolate);
    env = node::CreateEnvironment(
        isolate_data_, context, args, exec_args,
        static_cast<node::EnvironmentFlags::Flags>(flags));

    if (try_catch.HasCaught()) {
      std::string err_msg =
          "Failed to initialize node environment in process: " + process_type;
      v8::Local<v8::Message> message = try_catch.Message();
      std::string msg;
      if (!message.IsEmpty() &&
          gin::ConvertFromV8(isolate, message->Get(), &msg))
        err_msg += " , with error: " + msg;
      LOG(ERROR) << err_msg;
    }
  }

  DCHECK(env);

  node::IsolateSettings is;

  // Use a custom fatal error callback to allow us to add
  // crash message and location to CrashReports.
  is.fatal_error_callback = V8FatalErrorCallback;

  // We don't want to abort either in the renderer or browser processes.
  // We already listen for uncaught exceptions and handle them there.
  // For utility process we expect the process to behave as standard
  // Node.js runtime and abort the process with appropriate exit
  // code depending on a handler being set for `uncaughtException` event.
  if (browser_env_ != BrowserEnvironment::kUtility) {
    is.should_abort_on_uncaught_exception_callback = [](v8::Isolate*) {
      return false;
    };
  }

  // Use a custom callback here to allow us to leverage Blink's logic in the
  // renderer process.
  is.allow_wasm_code_generation_callback = AllowWasmCodeGenerationCallback;
  is.flags |= node::IsolateSettingsFlags::
      ALLOW_MODIFY_CODE_GENERATION_FROM_STRINGS_CALLBACK;
  is.modify_code_generation_from_strings_callback =
      ModifyCodeGenerationFromStrings;

  if (browser_env_ == BrowserEnvironment::kBrowser ||
      browser_env_ == BrowserEnvironment::kUtility) {
    // Node.js requires that microtask checkpoints be explicitly invoked.
    is.policy = v8::MicrotasksPolicy::kExplicit;
  } else {
    // Blink expects the microtasks policy to be kScoped, but Node.js expects it
    // to be kExplicit. In the renderer, there can be many contexts within the
    // same isolate, so we don't want to change the existing policy here, which
    // could be either kExplicit or kScoped depending on whether we're executing
    // from within a Node.js or a Blink entrypoint. Instead, the policy is
    // toggled to kExplicit when entering Node.js through UvRunOnce.
    is.policy = context->GetIsolate()->GetMicrotasksPolicy();

    // We do not want to use Node.js' message listener as it interferes with
    // Blink's.
    is.flags &= ~node::IsolateSettingsFlags::MESSAGE_LISTENER_WITH_ERROR_LEVEL;

    // Isolate message listeners are additive (you can add multiple), so instead
    // we add an extra one here to ensure that the async hook stack is properly
    // cleared when errors are thrown.
    context->GetIsolate()->AddMessageListenerWithErrorLevel(
        ErrorMessageListener, v8::Isolate::kMessageError);

    // We do not want to use the promise rejection callback that Node.js uses,
    // because it does not send PromiseRejectionEvents to the global script
    // context. We need to use the one Blink already provides.
    is.flags |=
        node::IsolateSettingsFlags::SHOULD_NOT_SET_PROMISE_REJECTION_CALLBACK;

    // We do not want to use the stack trace callback that Node.js uses,
    // because it relies on Node.js being aware of the current Context and
    // that's not always the case. We need to use the one Blink already
    // provides.
    is.flags |=
        node::IsolateSettingsFlags::SHOULD_NOT_SET_PREPARE_STACK_TRACE_CALLBACK;
  }

  node::SetIsolateUpForNode(context->GetIsolate(), is);

  gin_helper::Dictionary process(context->GetIsolate(), env->process_object());
  process.SetReadOnly("type", process_type);
  process.Set("resourcesPath", resources_path);
  // The path to helper app.
  base::FilePath helper_exec_path;
  base::PathService::Get(content::CHILD_PROCESS_EXE, &helper_exec_path);
  process.Set("helperExecPath", helper_exec_path);

  return env;
}

node::Environment* NodeBindings::CreateEnvironment(
    v8::Handle<v8::Context> context,
    node::MultiIsolatePlatform* platform) {
#if BUILDFLAG(IS_WIN)
  auto& electron_args = ElectronCommandLine::argv();
  std::vector<std::string> args(electron_args.size());
  std::transform(electron_args.cbegin(), electron_args.cend(), args.begin(),
                 [](auto& a) { return base::WideToUTF8(a); });
#else
  auto args = ElectronCommandLine::argv();
#endif
  return CreateEnvironment(context, platform, args, {});
}

void NodeBindings::LoadEnvironment(node::Environment* env) {
  node::LoadEnvironment(env, node::StartExecutionCallback{});
  gin_helper::EmitEvent(env->isolate(), env->process_object(), "loaded");
}

void NodeBindings::PrepareEmbedThread() {
  // IOCP does not change for the process until the loop is recreated,
  // we ensure that there is only a single polling thread satisfying
  // the concurrency limit set from CreateIoCompletionPort call by
  // uv_loop_init for the lifetime of this process.
  // More background can be found at:
  // https://github.com/microsoft/vscode/issues/142786#issuecomment-1061673400
  if (initialized_)
    return;

  // Add dummy handle for libuv, otherwise libuv would quit when there is
  // nothing to do.
  uv_async_init(uv_loop_, dummy_uv_handle_.get(), nullptr);

  // Start worker that will interrupt main loop when having uv events.
  uv_sem_init(&embed_sem_, 0);
  uv_thread_create(&embed_thread_, EmbedThreadRunner, this);
}

void NodeBindings::StartPolling() {
  // Avoid calling UvRunOnce if the loop is already active,
  // otherwise it can lead to situations were the number of active
  // threads processing on IOCP is greater than the concurrency limit.
  if (initialized_)
    return;

  initialized_ = true;

  // The MessageLoop should have been created, remember the one in main thread.
  task_runner_ = base::SingleThreadTaskRunner::GetCurrentDefault();

  // Run uv loop for once to give the uv__io_poll a chance to add all events.
  UvRunOnce();
}

void NodeBindings::UvRunOnce() {
  node::Environment* env = uv_env();

  // When doing navigation without restarting renderer process, it may happen
  // that the node environment is destroyed but the message loop is still there.
  // In this case we should not run uv loop.
  if (!env)
    return;

  v8::HandleScope handle_scope(env->isolate());

  // Enter node context while dealing with uv events.
  v8::Context::Scope context_scope(env->context());

  // Node.js expects `kExplicit` microtasks policy and will run microtasks
  // checkpoints after every call into JavaScript. Since we use a different
  // policy in the renderer - switch to `kExplicit` and then drop back to the
  // previous policy value.
  v8::MicrotaskQueue* microtask_queue = env->context()->GetMicrotaskQueue();
  auto old_policy = microtask_queue->microtasks_policy();
  DCHECK_EQ(microtask_queue->GetMicrotasksScopeDepth(), 0);
  microtask_queue->set_microtasks_policy(v8::MicrotasksPolicy::kExplicit);

  if (browser_env_ != BrowserEnvironment::kBrowser)
    TRACE_EVENT_BEGIN0("devtools.timeline", "FunctionCall");

  // Deal with uv events.
  int r = uv_run(uv_loop_, UV_RUN_NOWAIT);

  if (browser_env_ != BrowserEnvironment::kBrowser)
    TRACE_EVENT_END0("devtools.timeline", "FunctionCall");

  microtask_queue->set_microtasks_policy(old_policy);

  if (r == 0)
    base::RunLoop().QuitWhenIdle();  // Quit from uv.

  // Tell the worker thread to continue polling.
  uv_sem_post(&embed_sem_);
}

void NodeBindings::WakeupMainThread() {
  DCHECK(task_runner_);
  task_runner_->PostTask(FROM_HERE, base::BindOnce(&NodeBindings::UvRunOnce,
                                                   weak_factory_.GetWeakPtr()));
}

void NodeBindings::WakeupEmbedThread() {
  uv_async_send(dummy_uv_handle_.get());
}

// static
void NodeBindings::EmbedThreadRunner(void* arg) {
  auto* self = static_cast<NodeBindings*>(arg);

  while (true) {
    // Wait for the main loop to deal with events.
    uv_sem_wait(&self->embed_sem_);
    if (self->embed_closed_)
      break;

    // Wait for something to happen in uv loop.
    // Note that the PollEvents() is implemented by derived classes, so when
    // this class is being destructed the PollEvents() would not be available
    // anymore. Because of it we must make sure we only invoke PollEvents()
    // when this class is alive.
    self->PollEvents();
    if (self->embed_closed_)
      break;

    // Deal with event in main thread.
    self->WakeupMainThread();
  }
}

}  // namespace electron
