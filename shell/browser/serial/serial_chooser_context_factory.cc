// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/browser/serial/serial_chooser_context_factory.h"

#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "shell/browser/electron_browser_context.h"
#include "shell/browser/serial/serial_chooser_context.h"

namespace electron {

SerialChooserContextFactory::SerialChooserContextFactory()
    : BrowserContextKeyedServiceFactory(
          "SerialChooserContext",
          BrowserContextDependencyManager::GetInstance()) {}

SerialChooserContextFactory::~SerialChooserContextFactory() = default;

KeyedService* SerialChooserContextFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  auto* browser_context =
      static_cast<electron::ElectronBrowserContext*>(context);
  return new SerialChooserContext(browser_context);
}

// static
SerialChooserContextFactory* SerialChooserContextFactory::GetInstance() {
  static base::NoDestructor<SerialChooserContextFactory> instance;
  return instance.get();
}

// static
SerialChooserContext* SerialChooserContextFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  return static_cast<SerialChooserContext*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

content::BrowserContext* SerialChooserContextFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return context;
}

}  // namespace electron
