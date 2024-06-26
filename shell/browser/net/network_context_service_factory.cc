// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/net/network_context_service_factory.h"

#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "shell/browser/net/network_context_service.h"

namespace electron {

NetworkContextService* NetworkContextServiceFactory::GetForContext(
    content::BrowserContext* browser_context) {
  return static_cast<NetworkContextService*>(
      GetInstance()->GetServiceForBrowserContext(browser_context, true));
}

NetworkContextServiceFactory* NetworkContextServiceFactory::GetInstance() {
  static base::NoDestructor<NetworkContextServiceFactory> instance;
  return instance.get();
}

NetworkContextServiceFactory::NetworkContextServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "ElectronNetworkContextService",
          BrowserContextDependencyManager::GetInstance()) {}

NetworkContextServiceFactory::~NetworkContextServiceFactory() = default;

KeyedService* NetworkContextServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  return new NetworkContextService(
      static_cast<ElectronBrowserContext*>(context));
}

content::BrowserContext* NetworkContextServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  // Create separate service for temporary sessions.
  return context;
}

}  // namespace electron
