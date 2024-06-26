// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/browser/extensions/electron_extension_system_factory.h"

#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "extensions/browser/extension_prefs_factory.h"
#include "extensions/browser/extension_registry_factory.h"
#include "shell/browser/extensions/electron_extension_system.h"

using content::BrowserContext;

namespace extensions {

ExtensionSystem* ElectronExtensionSystemFactory::GetForBrowserContext(
    BrowserContext* context) {
  return static_cast<ElectronExtensionSystem*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

// static
ElectronExtensionSystemFactory* ElectronExtensionSystemFactory::GetInstance() {
  static base::NoDestructor<ElectronExtensionSystemFactory> instance;
  return instance.get();
}

ElectronExtensionSystemFactory::ElectronExtensionSystemFactory()
    : ExtensionSystemProvider("ElectronExtensionSystem",
                              BrowserContextDependencyManager::GetInstance()) {
  DependsOn(ExtensionPrefsFactory::GetInstance());
  DependsOn(ExtensionRegistryFactory::GetInstance());
}

ElectronExtensionSystemFactory::~ElectronExtensionSystemFactory() = default;

KeyedService* ElectronExtensionSystemFactory::BuildServiceInstanceFor(
    BrowserContext* context) const {
  return new ElectronExtensionSystem(context);
}

BrowserContext* ElectronExtensionSystemFactory::GetBrowserContextToUse(
    BrowserContext* context) const {
  // Use a separate instance for incognito.
  return context;
}

bool ElectronExtensionSystemFactory::ServiceIsCreatedWithBrowserContext()
    const {
  return true;
}

}  // namespace extensions
