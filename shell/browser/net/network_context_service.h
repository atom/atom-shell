// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef SHELL_BROWSER_NET_NETWORK_CONTEXT_SERVICE_H_
#define SHELL_BROWSER_NET_NETWORK_CONTEXT_SERVICE_H_

#include "base/files/file_path.h"
#include "chrome/browser/net/proxy_config_monitor.h"
#include "components/keyed_service/core/keyed_service.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "shell/browser/atom_browser_context.h"

namespace atom {

// KeyedService that initializes and provides access to the NetworkContexts for
// a BrowserContext.
class NetworkContextService : public KeyedService {
 public:
  explicit NetworkContextService(content::BrowserContext* context);
  ~NetworkContextService() override;

  NetworkContextService(const NetworkContextService&) = delete;
  NetworkContextService& operator=(const NetworkContextService&) = delete;

  // Creates a NetworkContext for the BrowserContext.
  network::mojom::NetworkContextPtr CreateNetworkContext();

 private:
  // Creates parameters for the NetworkContext.
  network::mojom::NetworkContextParamsPtr CreateNetworkContextParams(
      bool in_memory,
      const base::FilePath& path);

  AtomBrowserContext* browser_context_;
  ProxyConfigMonitor proxy_config_monitor_;
};

}  // namespace atom

#endif  // SHELL_BROWSER_NET_NETWORK_CONTEXT_SERVICE_H_
