// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_EXTENSIONS_ELECTRON_EXTENSION_SYSTEM_FACTORY_H_
#define ELECTRON_SHELL_BROWSER_EXTENSIONS_ELECTRON_EXTENSION_SYSTEM_FACTORY_H_

#include "extensions/browser/extension_system_provider.h"

namespace base {
template <typename T>
class NoDestructor;
}  // namespace base

namespace extensions {

// A factory that provides ElectronExtensionSystem.
class ElectronExtensionSystemFactory : public ExtensionSystemProvider {
 public:
  // ExtensionSystemProvider implementation:
  ExtensionSystem* GetForBrowserContext(
      content::BrowserContext* context) override;

  static ElectronExtensionSystemFactory* GetInstance();

  // disable copy
  ElectronExtensionSystemFactory(const ElectronExtensionSystemFactory&) =
      delete;
  ElectronExtensionSystemFactory& operator=(
      const ElectronExtensionSystemFactory&) = delete;

 private:
  friend base::NoDestructor<ElectronExtensionSystemFactory>;

  ElectronExtensionSystemFactory();
  ~ElectronExtensionSystemFactory() override;

  // BrowserContextKeyedServiceFactory implementation:
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace extensions

#endif  // ELECTRON_SHELL_BROWSER_EXTENSIONS_ELECTRON_EXTENSION_SYSTEM_FACTORY_H_
