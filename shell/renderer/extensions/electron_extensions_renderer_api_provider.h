// Copyright (c) 2017 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_RENDERER_EXTENSIONS_ELECTRON_EXTENSIONS_RENDERER_API_PROVIDER_H_
#define ELECTRON_SHELL_RENDERER_EXTENSIONS_ELECTRON_EXTENSIONS_RENDERER_API_PROVIDER_H_

#include <memory>

#include "extensions/renderer/extensions_renderer_api_provider.h"

namespace electron {

class ElectronExtensionsRendererAPIProvider
    : public extensions::ExtensionsRendererAPIProvider {
 public:
  ElectronExtensionsRendererAPIProvider() = default;
  ElectronExtensionsRendererAPIProvider(
      const ElectronExtensionsRendererAPIProvider&) = delete;
  ElectronExtensionsRendererAPIProvider& operator=(
      const ElectronExtensionsRendererAPIProvider&) = delete;
  ~ElectronExtensionsRendererAPIProvider() override = default;

  // ExtensionsRendererAPIProvider:
  void RegisterNativeHandlers(
      extensions::ModuleSystem* module_system,
      extensions::NativeExtensionBindingsSystem* bindings_system,
      extensions::V8SchemaRegistry* v8_schema_registry,
      extensions::ScriptContext* context) const override;
  void AddBindingsSystemHooks(extensions::Dispatcher* dispatcher,
                              extensions::NativeExtensionBindingsSystem*
                                  bindings_system) const override;
  void PopulateSourceMap(
      extensions::ResourceBundleSourceMap* source_map) const override;
  void EnableCustomElementAllowlist() const override;
  void RequireWebViewModules(extensions::ScriptContext* context) const override;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_RENDERER_EXTENSIONS_ELECTRON_EXTENSIONS_RENDERER_API_PROVIDER_H_
