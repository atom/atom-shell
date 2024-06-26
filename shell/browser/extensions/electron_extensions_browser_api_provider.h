// Copyright (c) 2019 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_EXTENSIONS_ELECTRON_EXTENSIONS_BROWSER_API_PROVIDER_H_
#define ELECTRON_SHELL_BROWSER_EXTENSIONS_ELECTRON_EXTENSIONS_BROWSER_API_PROVIDER_H_

#include "extensions/browser/extensions_browser_api_provider.h"

namespace extensions {

class ElectronExtensionsBrowserAPIProvider
    : public ExtensionsBrowserAPIProvider {
 public:
  ElectronExtensionsBrowserAPIProvider();
  ~ElectronExtensionsBrowserAPIProvider() override;

  // disable copy
  ElectronExtensionsBrowserAPIProvider(
      const ElectronExtensionsBrowserAPIProvider&) = delete;
  ElectronExtensionsBrowserAPIProvider& operator=(
      const ElectronExtensionsBrowserAPIProvider&) = delete;

  // ExtensionsBrowserAPIProvider
  void RegisterExtensionFunctions(ExtensionFunctionRegistry* registry) override;
};

}  // namespace extensions

#endif  // ELECTRON_SHELL_BROWSER_EXTENSIONS_ELECTRON_EXTENSIONS_BROWSER_API_PROVIDER_H_
