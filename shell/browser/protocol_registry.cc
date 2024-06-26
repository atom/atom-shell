// Copyright (c) 2020 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/protocol_registry.h"

#include "content/public/browser/web_contents.h"
#include "electron/fuses.h"
#include "shell/browser/electron_browser_context.h"
#include "shell/browser/net/asar/asar_url_loader_factory.h"

namespace electron {

// static
ProtocolRegistry* ProtocolRegistry::FromBrowserContext(
    content::BrowserContext* context) {
  return static_cast<ElectronBrowserContext*>(context)->protocol_registry();
}

ProtocolRegistry::ProtocolRegistry() = default;

ProtocolRegistry::~ProtocolRegistry() = default;

void ProtocolRegistry::RegisterURLLoaderFactories(
    content::ContentBrowserClient::NonNetworkURLLoaderFactoryMap* factories,
    bool allow_file_access) {
  if (electron::fuses::IsGrantFileProtocolExtraPrivilegesEnabled()) {
    auto file_factory = factories->find(url::kFileScheme);
    if (file_factory != factories->end()) {
      // If Chromium already allows file access then replace the url factory to
      // also loading asar files.
      file_factory->second = AsarURLLoaderFactory::Create();
    } else if (allow_file_access) {
      // Otherwise only allow file access when it is explicitly allowed.
      //
      // Note that Chromium may call |emplace| to create the default file
      // factory after this call, it won't override our asar factory, but if
      // asar support breaks in future, please check if Chromium has changed the
      // call.
      factories->emplace(url::kFileScheme, AsarURLLoaderFactory::Create());
    }
  }

  for (const auto& it : handlers_) {
    factories->emplace(it.first, ElectronURLLoaderFactory::Create(
                                     it.second.first, it.second.second));
  }
}

mojo::PendingRemote<network::mojom::URLLoaderFactory>
ProtocolRegistry::CreateNonNetworkNavigationURLLoaderFactory(
    const std::string& scheme) {
  if (scheme == url::kFileScheme) {
    if (electron::fuses::IsGrantFileProtocolExtraPrivilegesEnabled()) {
      return AsarURLLoaderFactory::Create();
    }
  } else {
    auto handler = handlers_.find(scheme);
    if (handler != handlers_.end()) {
      return ElectronURLLoaderFactory::Create(handler->second.first,
                                              handler->second.second);
    }
  }
  return {};
}

bool ProtocolRegistry::RegisterProtocol(ProtocolType type,
                                        const std::string& scheme,
                                        const ProtocolHandler& handler) {
  return handlers_.try_emplace(scheme, type, handler).second;
}

bool ProtocolRegistry::UnregisterProtocol(const std::string& scheme) {
  return handlers_.erase(scheme) != 0;
}

const HandlersMap::mapped_type* ProtocolRegistry::FindRegistered(
    const std::string& scheme) const {
  const auto& map = handlers_;
  const auto iter = map.find(scheme);
  return iter != std::end(map) ? &iter->second : nullptr;
}

bool ProtocolRegistry::InterceptProtocol(ProtocolType type,
                                         const std::string& scheme,
                                         const ProtocolHandler& handler) {
  return intercept_handlers_.try_emplace(scheme, type, handler).second;
}

bool ProtocolRegistry::UninterceptProtocol(const std::string& scheme) {
  return intercept_handlers_.erase(scheme) != 0;
}

const HandlersMap::mapped_type* ProtocolRegistry::FindIntercepted(
    const std::string& scheme) const {
  const auto& map = intercept_handlers_;
  const auto iter = map.find(scheme);
  return iter != std::end(map) ? &iter->second : nullptr;
}

}  // namespace electron
