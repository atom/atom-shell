// Copyright (c) 2022 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_utility_process.h"

#include <map>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/process/kill.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "chrome/browser/browser_process.h"
#include "content/public/browser/child_process_host.h"
#include "content/public/browser/service_process_host.h"
#include "content/public/common/result_codes.h"
#include "gin/handle.h"
#include "gin/object_template_builder.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "shell/browser/api/message_port.h"
#include "shell/browser/javascript_environment.h"
#include "shell/browser/net/system_network_context_manager.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/file_path_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/object_template_builder.h"
#include "shell/common/node_includes.h"
#include "shell/common/v8_value_serializer.h"
#include "third_party/blink/public/common/messaging/message_port_descriptor.h"
#include "third_party/blink/public/common/messaging/transferable_message_mojom_traits.h"

#if BUILDFLAG(IS_POSIX)
#include "base/posix/eintr_wrapper.h"
#endif

#if BUILDFLAG(IS_WIN)
#include <fcntl.h>
#include <io.h>
#include "base/win/windows_types.h"
#endif

namespace electron {

base::IDMap<api::UtilityProcessWrapper*, base::ProcessId>&
GetAllUtilityProcessWrappers() {
  static base::NoDestructor<
      base::IDMap<api::UtilityProcessWrapper*, base::ProcessId>>
      s_all_utility_process_wrappers;
  return *s_all_utility_process_wrappers;
}

namespace api {

gin::WrapperInfo UtilityProcessWrapper::kWrapperInfo = {
    gin::kEmbedderNativeGin};

UtilityProcessWrapper::UtilityProcessWrapper(
    node::mojom::NodeServiceParamsPtr params,
    std::u16string display_name,
    std::map<IOHandle, IOType> stdio,
    base::EnvironmentMap env_map,
    base::FilePath current_working_directory,
    bool use_plugin_helper,
    bool create_network_observer) {
#if BUILDFLAG(IS_WIN)
  base::win::ScopedHandle stdout_write(nullptr);
  base::win::ScopedHandle stderr_write(nullptr);
#elif BUILDFLAG(IS_POSIX)
  base::FileHandleMappingVector fds_to_remap;
#endif
  for (const auto& [io_handle, io_type] : stdio) {
    if (io_type == IOType::IO_PIPE) {
#if BUILDFLAG(IS_WIN)
      HANDLE read = nullptr;
      HANDLE write = nullptr;
      // Ideally we would create with SECURITY_ATTRIBUTES.bInheritHandles
      // set to TRUE so that the write handle can be duplicated into the
      // child process for use,
      // See
      // https://learn.microsoft.com/en-us/windows/win32/procthread/inheritance#inheriting-handles
      // for inheritance behavior of child process. But we don't do it here
      // since base::Launch already takes of setting the
      // inherit attribute when configuring
      // `base::LaunchOptions::handles_to_inherit` Refs
      // https://source.chromium.org/chromium/chromium/src/+/main:base/process/launch_win.cc;l=303-332
      if (!::CreatePipe(&read, &write, nullptr, 0)) {
        PLOG(ERROR) << "pipe creation failed";
        return;
      }
      if (io_handle == IOHandle::STDOUT) {
        stdout_write.Set(write);
        stdout_read_handle_ = read;
        stdout_read_fd_ =
            _open_osfhandle(reinterpret_cast<intptr_t>(read), _O_RDONLY);
      } else if (io_handle == IOHandle::STDERR) {
        stderr_write.Set(write);
        stderr_read_handle_ = read;
        stderr_read_fd_ =
            _open_osfhandle(reinterpret_cast<intptr_t>(read), _O_RDONLY);
      }
#elif BUILDFLAG(IS_POSIX)
      int pipe_fd[2];
      if (HANDLE_EINTR(pipe(pipe_fd)) < 0) {
        PLOG(ERROR) << "pipe creation failed";
        return;
      }
      if (io_handle == IOHandle::STDOUT) {
        fds_to_remap.emplace_back(pipe_fd[1], STDOUT_FILENO);
        stdout_read_fd_ = pipe_fd[0];
      } else if (io_handle == IOHandle::STDERR) {
        fds_to_remap.emplace_back(pipe_fd[1], STDERR_FILENO);
        stderr_read_fd_ = pipe_fd[0];
      }
#endif
    } else if (io_type == IOType::IO_IGNORE) {
#if BUILDFLAG(IS_WIN)
      HANDLE handle =
          CreateFileW(L"NUL", FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES,
                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                      OPEN_EXISTING, 0, nullptr);
      if (handle == INVALID_HANDLE_VALUE) {
        PLOG(ERROR) << "Failed to create null handle";
        return;
      }
      if (io_handle == IOHandle::STDOUT) {
        stdout_write.Set(handle);
      } else if (io_handle == IOHandle::STDERR) {
        stderr_write.Set(handle);
      }
#elif BUILDFLAG(IS_POSIX)
      int devnull = open("/dev/null", O_WRONLY);
      if (devnull < 0) {
        PLOG(ERROR) << "failed to open /dev/null";
        return;
      }
      if (io_handle == IOHandle::STDOUT) {
        fds_to_remap.emplace_back(devnull, STDOUT_FILENO);
      } else if (io_handle == IOHandle::STDERR) {
        fds_to_remap.emplace_back(devnull, STDERR_FILENO);
      }
#endif
    }
  }

  if (!content::ServiceProcessHost::HasObserver(this))
    content::ServiceProcessHost::AddObserver(this);

  mojo::PendingReceiver<node::mojom::NodeService> receiver =
      node_service_remote_.BindNewPipeAndPassReceiver();

  content::ServiceProcessHost::Launch(
      std::move(receiver),
      content::ServiceProcessHost::Options()
          .WithDisplayName(display_name.empty()
                               ? std::u16string(u"Node Utility Process")
                               : display_name)
          .WithExtraCommandLineSwitches(params->exec_args)
          .WithCurrentDirectory(current_working_directory)
          // Inherit parent process environment when there is no custom
          // environment provided by the user.
          .WithEnvironment(env_map,
                           env_map.empty() ? false : true /*clear_environment*/)
#if BUILDFLAG(IS_WIN)
          .WithStdoutHandle(std::move(stdout_write))
          .WithStderrHandle(std::move(stderr_write))
#elif BUILDFLAG(IS_POSIX)
          .WithAdditionalFds(std::move(fds_to_remap))
#endif
#if BUILDFLAG(IS_MAC)
          .WithChildFlags(use_plugin_helper
                              ? content::ChildProcessHost::CHILD_PLUGIN
                              : content::ChildProcessHost::CHILD_NORMAL)
#endif
          .WithProcessCallback(
              base::BindOnce(&UtilityProcessWrapper::OnServiceProcessLaunch,
                             weak_factory_.GetWeakPtr()))
          .Pass());

  node_service_remote_.set_disconnect_with_reason_handler(
      base::BindOnce(&UtilityProcessWrapper::OnServiceProcessDisconnected,
                     weak_factory_.GetWeakPtr()));

  // We use a separate message pipe to support postMessage API
  // instead of the existing receiver interface so that we can
  // support queuing of messages without having to block other
  // interfaces.
  blink::MessagePortDescriptorPair pipe;
  host_port_ = pipe.TakePort0();
  params->port = pipe.TakePort1();
  connector_ = std::make_unique<mojo::Connector>(
      host_port_.TakeHandleToEntangleWithEmbedder(),
      mojo::Connector::SINGLE_THREADED_SEND,
      base::SingleThreadTaskRunner::GetCurrentDefault());
  connector_->set_incoming_receiver(this);
  connector_->set_connection_error_handler(base::BindOnce(
      &UtilityProcessWrapper::CloseConnectorPort, weak_factory_.GetWeakPtr()));

  mojo::PendingRemote<network::mojom::URLLoaderFactory> url_loader_factory;
  network::mojom::URLLoaderFactoryParamsPtr loader_params =
      network::mojom::URLLoaderFactoryParams::New();
  loader_params->process_id = pid_;
  loader_params->is_orb_enabled = false;
  loader_params->is_trusted = true;
  if (create_network_observer) {
    url_loader_network_observer_.emplace();
    loader_params->url_loader_network_observer =
        url_loader_network_observer_->Bind();
  }
  network::mojom::NetworkContext* network_context =
      g_browser_process->system_network_context_manager()->GetContext();
  network_context->CreateURLLoaderFactory(
      url_loader_factory.InitWithNewPipeAndPassReceiver(),
      std::move(loader_params));
  params->url_loader_factory = std::move(url_loader_factory);
  mojo::PendingRemote<network::mojom::HostResolver> host_resolver;
  network_context->CreateHostResolver(
      {}, host_resolver.InitWithNewPipeAndPassReceiver());
  params->host_resolver = std::move(host_resolver);
  params->use_network_observer_from_url_loader_factory =
      create_network_observer;

  node_service_remote_->Initialize(std::move(params));
}

UtilityProcessWrapper::~UtilityProcessWrapper() {
  content::ServiceProcessHost::RemoveObserver(this);
}

void UtilityProcessWrapper::OnServiceProcessLaunch(
    const base::Process& process) {
  DCHECK(node_service_remote_.is_connected());
  pid_ = process.Pid();
  GetAllUtilityProcessWrappers().AddWithID(this, pid_);
  if (stdout_read_fd_ != -1)
    EmitWithoutEvent("stdout", stdout_read_fd_);
  if (stderr_read_fd_ != -1)
    EmitWithoutEvent("stderr", stderr_read_fd_);
  if (url_loader_network_observer_.has_value()) {
    url_loader_network_observer_->set_process_id(pid_);
  }
  EmitWithoutEvent("spawn");
}

void UtilityProcessWrapper::HandleTermination(uint64_t exit_code) {
  if (pid_ != base::kNullProcessId)
    GetAllUtilityProcessWrappers().Remove(pid_);
  CloseConnectorPort();

  EmitWithoutEvent("exit", exit_code);

  Unpin();
}

void UtilityProcessWrapper::OnServiceProcessDisconnected(
    uint32_t exit_code,
    const std::string& description) {
  if (description == "process_exit_termination")
    HandleTermination(exit_code);
}

void UtilityProcessWrapper::OnServiceProcessTerminatedNormally(
    const content::ServiceProcessInfo& info) {
  if (!info.IsService<node::mojom::NodeService>() ||
      info.GetProcess().Pid() != pid_)
    return;

  HandleTermination(info.exit_code());
}

void UtilityProcessWrapper::OnServiceProcessCrashed(
    const content::ServiceProcessInfo& info) {
  if (!info.IsService<node::mojom::NodeService>() ||
      info.GetProcess().Pid() != pid_)
    return;

  HandleTermination(info.exit_code());
}

void UtilityProcessWrapper::CloseConnectorPort() {
  if (!connector_closed_ && connector_->is_valid()) {
    host_port_.GiveDisentangledHandle(connector_->PassMessagePipe());
    connector_ = nullptr;
    host_port_.Reset();
    connector_closed_ = true;
  }
}

void UtilityProcessWrapper::Shutdown(uint64_t exit_code) {
  if (pid_ != base::kNullProcessId)
    GetAllUtilityProcessWrappers().Remove(pid_);
  node_service_remote_.reset();
  CloseConnectorPort();
  // Emit 'exit' event
  EmitWithoutEvent("exit", exit_code);
  Unpin();
}

void UtilityProcessWrapper::PostMessage(gin::Arguments* args) {
  if (!node_service_remote_.is_connected())
    return;

  blink::TransferableMessage transferable_message;
  v8::Local<v8::Value> message_value;
  if (args->GetNext(&message_value)) {
    if (!electron::SerializeV8Value(args->isolate(), message_value,
                                    &transferable_message)) {
      // SerializeV8Value sets an exception.
      return;
    }
  }

  v8::Local<v8::Value> transferables;
  std::vector<gin::Handle<MessagePort>> wrapped_ports;
  if (args->GetNext(&transferables)) {
    if (!gin::ConvertFromV8(args->isolate(), transferables, &wrapped_ports)) {
      gin_helper::ErrorThrower(args->isolate())
          .ThrowTypeError("Invalid value for transfer");
      return;
    }
  }

  bool threw_exception = false;
  transferable_message.ports = MessagePort::DisentanglePorts(
      args->isolate(), wrapped_ports, &threw_exception);
  if (threw_exception)
    return;

  mojo::Message mojo_message = blink::mojom::TransferableMessage::WrapAsMessage(
      std::move(transferable_message));
  connector_->Accept(&mojo_message);
}

bool UtilityProcessWrapper::Kill() const {
  if (pid_ == base::kNullProcessId)
    return false;
  base::Process process = base::Process::Open(pid_);
  bool result = process.Terminate(content::RESULT_CODE_NORMAL_EXIT, false);
  // Refs https://bugs.chromium.org/p/chromium/issues/detail?id=818244
  // Currently utility process is not sandboxed which
  // means Zygote is not used on linux, refs
  // content::UtilitySandboxedProcessLauncherDelegate::GetZygote.
  // If sandbox feature is enabled for the utility process, then the
  // process reap should be signaled through the zygote via
  // content::ZygoteCommunication::EnsureProcessTerminated.
  base::EnsureProcessTerminated(std::move(process));
  return result;
}

v8::Local<v8::Value> UtilityProcessWrapper::GetOSProcessId(
    v8::Isolate* isolate) const {
  if (pid_ == base::kNullProcessId)
    return v8::Undefined(isolate);
  return gin::ConvertToV8(isolate, pid_);
}

bool UtilityProcessWrapper::Accept(mojo::Message* mojo_message) {
  blink::TransferableMessage message;
  if (!blink::mojom::TransferableMessage::DeserializeFromMessage(
          std::move(*mojo_message), &message)) {
    return false;
  }

  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Value> message_value =
      electron::DeserializeV8Value(isolate, message);
  EmitWithoutEvent("message", message_value);
  return true;
}

// static
raw_ptr<UtilityProcessWrapper> UtilityProcessWrapper::FromProcessId(
    base::ProcessId pid) {
  auto* utility_process_wrapper = GetAllUtilityProcessWrappers().Lookup(pid);
  return !!utility_process_wrapper ? utility_process_wrapper : nullptr;
}

// static
gin::Handle<UtilityProcessWrapper> UtilityProcessWrapper::Create(
    gin::Arguments* args) {
  gin_helper::Dictionary dict;
  if (!args->GetNext(&dict)) {
    args->ThrowTypeError("Options must be an object.");
    return gin::Handle<UtilityProcessWrapper>();
  }

  std::u16string display_name;
  bool use_plugin_helper = false;
  bool create_network_observer = false;
  std::map<IOHandle, IOType> stdio;
  base::FilePath current_working_directory;
  base::EnvironmentMap env_map;
  node::mojom::NodeServiceParamsPtr params =
      node::mojom::NodeServiceParams::New();
  dict.Get("modulePath", &params->script);
  if (dict.Has("args") && !dict.Get("args", &params->args)) {
    args->ThrowTypeError("Invalid value for args");
    return gin::Handle<UtilityProcessWrapper>();
  }

  gin_helper::Dictionary opts;
  if (dict.Get("options", &opts)) {
    if (opts.Has("env") && !opts.Get("env", &env_map)) {
      args->ThrowTypeError("Invalid value for env");
      return gin::Handle<UtilityProcessWrapper>();
    }

    if (opts.Has("execArgv") && !opts.Get("execArgv", &params->exec_args)) {
      args->ThrowTypeError("Invalid value for execArgv");
      return gin::Handle<UtilityProcessWrapper>();
    }

    opts.Get("serviceName", &display_name);
    opts.Get("cwd", &current_working_directory);
    opts.Get("respondToAuthRequestsFromMainProcess", &create_network_observer);

    std::vector<std::string> stdio_arr{"ignore", "inherit", "inherit"};
    opts.Get("stdio", &stdio_arr);
    for (size_t i = 0; i < 3; i++) {
      IOType type;
      if (stdio_arr[i] == "ignore")
        type = IOType::IO_IGNORE;
      else if (stdio_arr[i] == "inherit")
        type = IOType::IO_INHERIT;
      else if (stdio_arr[i] == "pipe")
        type = IOType::IO_PIPE;

      stdio.emplace(static_cast<IOHandle>(i), type);
    }

#if BUILDFLAG(IS_MAC)
    opts.Get("allowLoadingUnsignedLibraries", &use_plugin_helper);
#endif
  }
  auto handle = gin::CreateHandle(
      args->isolate(), new UtilityProcessWrapper(
                           std::move(params), display_name, std::move(stdio),
                           env_map, current_working_directory,
                           use_plugin_helper, create_network_observer));
  handle->Pin(args->isolate());
  return handle;
}

// static
gin::ObjectTemplateBuilder UtilityProcessWrapper::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin_helper::EventEmitterMixin<
             UtilityProcessWrapper>::GetObjectTemplateBuilder(isolate)
      .SetMethod("postMessage", &UtilityProcessWrapper::PostMessage)
      .SetMethod("kill", &UtilityProcessWrapper::Kill)
      .SetProperty("pid", &UtilityProcessWrapper::GetOSProcessId);
}

const char* UtilityProcessWrapper::GetTypeName() {
  return "UtilityProcessWrapper";
}

}  // namespace api

}  // namespace electron

namespace {

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary dict(isolate, exports);
  dict.SetMethod("_fork", &electron::api::UtilityProcessWrapper::Create);
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_browser_utility_process, Initialize)
