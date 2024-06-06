// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_NET_SYSTEM_NETWORK_CONTEXT_MANAGER_H_
#define ELECTRON_SHELL_BROWSER_NET_SYSTEM_NETWORK_CONTEXT_MANAGER_H_

#include <optional>

#include "base/memory/ref_counted.h"
#include "chrome/browser/net/proxy_config_monitor.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "sandbox/policy/features.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/network_service.mojom.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"

namespace electron {
network::mojom::HttpAuthDynamicParamsPtr CreateHttpAuthDynamicParams();
}

// Responsible for creating and managing access to the system NetworkContext.
// Lives on the UI thread. The NetworkContext this owns is intended for requests
// not associated with a session. It stores no data on disk, and has no HTTP
// cache, but it does have ephemeral cookie and channel ID stores.
//
// This class is also responsible for configuring global NetworkService state.
//
// The "system" NetworkContext will either share a URLRequestContext with
// IOThread's SystemURLRequestContext and be part of IOThread's NetworkService
// (If the network service is disabled) or be an independent NetworkContext
// using the actual network service.
class SystemNetworkContextManager {
 public:
  ~SystemNetworkContextManager();

  // disable copy
  SystemNetworkContextManager(const SystemNetworkContextManager&) = delete;
  SystemNetworkContextManager& operator=(const SystemNetworkContextManager&) =
      delete;

  // Checks if the global SystemNetworkContextManager has been created.
  static bool HasInstance();

  // Creates the global instance of SystemNetworkContextManager. If an
  // instance already exists, this will cause a DCHECK failure.
  static SystemNetworkContextManager* CreateInstance(PrefService* pref_service);

  // Gets the global SystemNetworkContextManager instance. If it has not been
  // created yet, NetworkService is called, which will cause the
  // SystemNetworkContextManager to be created.
  static SystemNetworkContextManager* GetInstance();

  // Destroys the global SystemNetworkContextManager instance.
  static void DeleteInstance();

  // c.f.
  // https://source.chromium.org/chromium/chromium/src/+/main:chrome/browser/net/system_network_context_manager.cc;l=730-740;drc=15a616c8043551a7cb22c4f73a88e83afb94631c;bpv=1;bpt=1
  // Returns whether the network sandbox is enabled. This depends on  policy but
  // also feature status from sandbox. Called before there is an instance of
  // SystemNetworkContextManager.
  static bool IsNetworkSandboxEnabled();

  // Configures default set of parameters for configuring the network context.
  void ConfigureDefaultNetworkContextParams(
      network::mojom::NetworkContextParams* network_context_params);

  // Same as ConfigureDefaultNetworkContextParams() but returns a newly
  // allocated network::mojom::NetworkContextParams with the
  // CertVerifierCreationParams already placed into the NetworkContextParams.
  network::mojom::NetworkContextParamsPtr CreateDefaultNetworkContextParams();

  // Returns the System NetworkContext. May only be called after SetUp(). Does
  // any initialization of the NetworkService that may be needed when first
  // called.
  network::mojom::NetworkContext* GetContext();

  // Returns a URLLoaderFactory owned by the SystemNetworkContextManager that is
  // backed by the SystemNetworkContext. Allows sharing of the URLLoaderFactory.
  // Prefer this to creating a new one.  Call Clone() on the value returned by
  // this method to get a URLLoaderFactory that can be used on other threads.
  network::mojom::URLLoaderFactory* GetURLLoaderFactory();

  // Returns a SharedURLLoaderFactory owned by the SystemNetworkContextManager
  // that is backed by the SystemNetworkContext.
  scoped_refptr<network::SharedURLLoaderFactory> GetSharedURLLoaderFactory();

  // Called when content creates a NetworkService. Creates the
  // SystemNetworkContext, if the network service is enabled.
  void OnNetworkServiceCreated(network::mojom::NetworkService* network_service);

 private:
  class URLLoaderFactoryForSystem;

  explicit SystemNetworkContextManager(PrefService* pref_service);

  // Creates parameters for the NetworkContext. May only be called once, since
  // it initializes some class members.
  network::mojom::NetworkContextParamsPtr CreateNetworkContextParams();

  ProxyConfigMonitor proxy_config_monitor_;

  // NetworkContext using the network service, if the network service is
  // enabled. nullptr, otherwise.
  mojo::Remote<network::mojom::NetworkContext> network_context_;

  // URLLoaderFactory backed by the NetworkContext returned by GetContext(), so
  // consumers don't all need to create their own factory.
  scoped_refptr<URLLoaderFactoryForSystem> shared_url_loader_factory_;
  mojo::Remote<network::mojom::URLLoaderFactory> url_loader_factory_;
};

#endif  // ELECTRON_SHELL_BROWSER_NET_SYSTEM_NETWORK_CONTEXT_MANAGER_H_
