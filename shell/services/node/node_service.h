// Copyright (c) 2022 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_SERVICES_NODE_NODE_SERVICE_H_
#define ELECTRON_SHELL_SERVICES_NODE_NODE_SERVICE_H_

#include <memory>

#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/mojom/host_resolver.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom-forward.h"
#include "shell/services/node/public/mojom/node_service.mojom.h"

namespace node {

class Environment;

}  // namespace node

namespace electron {

class ElectronBindings;
class JavascriptEnvironment;
class NodeBindings;

class URLLoaderBundle {
 public:
  URLLoaderBundle();
  ~URLLoaderBundle();

  URLLoaderBundle(const URLLoaderBundle&) = delete;
  URLLoaderBundle& operator=(const URLLoaderBundle&) = delete;

  static URLLoaderBundle* GetInstance();
  void SetURLLoaderFactory(
      mojo::PendingRemote<network::mojom::URLLoaderFactory> factory,
      mojo::Remote<network::mojom::HostResolver> host_resolver,
      bool use_network_observer_from_url_loader_factory);
  scoped_refptr<network::SharedURLLoaderFactory> GetSharedURLLoaderFactory();
  network::mojom::HostResolver* GetHostResolver();
  bool ShouldUseNetworkObserverfromURLLoaderFactory() const;

 private:
  scoped_refptr<network::SharedURLLoaderFactory> factory_;
  mojo::Remote<network::mojom::HostResolver> host_resolver_;
  bool should_use_network_observer_from_url_loader_factory_ = false;
};

class NodeService : public node::mojom::NodeService {
 public:
  explicit NodeService(
      mojo::PendingReceiver<node::mojom::NodeService> receiver);
  ~NodeService() override;

  NodeService(const NodeService&) = delete;
  NodeService& operator=(const NodeService&) = delete;

  // mojom::NodeService implementation:
  void Initialize(node::mojom::NodeServiceParamsPtr params) override;

 private:
  // This needs to be initialized first so that it can be destroyed last
  // after the node::Environment is destroyed. This ensures that if
  // there are crashes in the node::Environment destructor, they
  // will be propagated to the exit handler.
  mojo::Receiver<node::mojom::NodeService> receiver_{this};

  bool node_env_stopped_ = false;

  const std::unique_ptr<NodeBindings> node_bindings_;

  // depends-on: node_bindings_'s uv_loop
  const std::unique_ptr<ElectronBindings> electron_bindings_;

  // depends-on: node_bindings_'s uv_loop
  std::unique_ptr<JavascriptEnvironment> js_env_;

  // depends-on: js_env_'s isolate
  std::shared_ptr<node::Environment> node_env_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_SERVICES_NODE_NODE_SERVICE_H_
