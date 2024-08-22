// Copyright (c) 2022 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/services/node/node_service.h"

#include <utility>

#include "base/command_line.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "services/network/public/cpp/wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/host_resolver.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "shell/browser/javascript_environment.h"
#include "shell/common/api/electron_bindings.h"
#include "shell/common/gin_converters/file_path_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/node_bindings.h"
#include "shell/common/node_includes.h"
#include "shell/services/node/parent_port.h"

namespace electron {

URLLoaderBundle::URLLoaderBundle() = default;

URLLoaderBundle::~URLLoaderBundle() = default;

URLLoaderBundle* URLLoaderBundle::GetInstance() {
  static base::NoDestructor<URLLoaderBundle> instance;
  return instance.get();
}

void URLLoaderBundle::SetURLLoaderFactory(
    mojo::PendingRemote<network::mojom::URLLoaderFactory> pending_factory,
    mojo::Remote<network::mojom::HostResolver> host_resolver,
    bool use_network_observer_from_url_loader_factory) {
  factory_ = network::SharedURLLoaderFactory::Create(
      std::make_unique<network::WrapperPendingSharedURLLoaderFactory>(
          std::move(pending_factory)));
  host_resolver_ = std::move(host_resolver);
  should_use_network_observer_from_url_loader_factory_ =
      use_network_observer_from_url_loader_factory;
}

scoped_refptr<network::SharedURLLoaderFactory>
URLLoaderBundle::GetSharedURLLoaderFactory() {
  return factory_;
}

network::mojom::HostResolver* URLLoaderBundle::GetHostResolver() {
  DCHECK(host_resolver_);
  return host_resolver_.get();
}

bool URLLoaderBundle::ShouldUseNetworkObserverfromURLLoaderFactory() const {
  return should_use_network_observer_from_url_loader_factory_;
}

NodeService::NodeService(
    mojo::PendingReceiver<node::mojom::NodeService> receiver)
    : node_bindings_{NodeBindings::Create(
          NodeBindings::BrowserEnvironment::kUtility)},
      electron_bindings_{
          std::make_unique<ElectronBindings>(node_bindings_->uv_loop())} {
  if (receiver.is_valid())
    receiver_.Bind(std::move(receiver));
}

NodeService::~NodeService() {
  if (!node_env_stopped_) {
    node_env_->set_trace_sync_io(false);
    js_env_->DestroyMicrotasksRunner();
    node::Stop(node_env_.get(), node::StopFlags::kDoNotTerminateIsolate);
  }
}

void NodeService::Initialize(node::mojom::NodeServiceParamsPtr params) {
  if (NodeBindings::IsInitialized())
    return;

  ParentPort::GetInstance()->Initialize(std::move(params->port));

  URLLoaderBundle::GetInstance()->SetURLLoaderFactory(
      std::move(params->url_loader_factory),
      mojo::Remote(std::move(params->host_resolver)),
      params->use_network_observer_from_url_loader_factory);

  js_env_ = std::make_unique<JavascriptEnvironment>(node_bindings_->uv_loop());

  v8::HandleScope scope(js_env_->isolate());

  node_bindings_->Initialize(js_env_->isolate()->GetCurrentContext());

  // Append program path for process.argv0
  auto program = base::CommandLine::ForCurrentProcess()->GetProgram();
#if defined(OS_WIN)
  params->args.insert(params->args.begin(), base::WideToUTF8(program.value()));
#else
  params->args.insert(params->args.begin(), program.value());
#endif

  // Create the global environment.
  node_env_ = node_bindings_->CreateEnvironment(
      js_env_->isolate()->GetCurrentContext(), js_env_->platform(),
      params->args, params->exec_args);

  node::SetProcessExitHandler(
      node_env_.get(), [this](node::Environment* env, int exit_code) {
        // Destroy node platform.
        env->set_trace_sync_io(false);
        js_env_->DestroyMicrotasksRunner();
        node::Stop(env, node::StopFlags::kDoNotTerminateIsolate);
        node_env_stopped_ = true;
        receiver_.ResetWithReason(exit_code, "process_exit_termination");
      });

  node_env_->set_trace_sync_io(node_env_->options()->trace_sync_io);

  // Add Electron extended APIs.
  electron_bindings_->BindTo(node_env_->isolate(), node_env_->process_object());

  // Add entry script to process object.
  gin_helper::Dictionary process(node_env_->isolate(),
                                 node_env_->process_object());
  process.SetHidden("_serviceStartupScript", params->script);

  // Setup microtask runner.
  js_env_->CreateMicrotasksRunner();

  // Wrap the uv loop with global env.
  node_bindings_->set_uv_env(node_env_.get());

  // LoadEnvironment should be called after setting up
  // JavaScriptEnvironment including the microtask runner
  // since this call will start compilation and execution
  // of the entry script. If there is an uncaught exception
  // the exit handler set above will be triggered and it expects
  // both Node Env and JavaScriptEnvironment are setup to perform
  // a clean shutdown of this process.
  node_bindings_->LoadEnvironment(node_env_.get());

  // Run entry script.
  node_bindings_->PrepareEmbedThread();
  node_bindings_->StartPolling();
}

}  // namespace electron
