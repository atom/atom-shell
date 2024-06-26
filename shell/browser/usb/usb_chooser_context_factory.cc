// Copyright (c) 2022 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/usb/usb_chooser_context_factory.h"

#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "shell/browser/electron_browser_context.h"
#include "shell/browser/usb/usb_chooser_context.h"

namespace electron {

UsbChooserContextFactory::UsbChooserContextFactory()
    : BrowserContextKeyedServiceFactory(
          "UsbChooserContext",
          BrowserContextDependencyManager::GetInstance()) {}

UsbChooserContextFactory::~UsbChooserContextFactory() = default;

KeyedService* UsbChooserContextFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  auto* browser_context =
      static_cast<electron::ElectronBrowserContext*>(context);
  return new UsbChooserContext(browser_context);
}

// static
UsbChooserContextFactory* UsbChooserContextFactory::GetInstance() {
  static base::NoDestructor<UsbChooserContextFactory> instance;
  return instance.get();
}

// static
UsbChooserContext* UsbChooserContextFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  return static_cast<UsbChooserContext*>(
      GetInstance()->GetServiceForBrowserContext(context, /*create=*/true));
}

UsbChooserContext* UsbChooserContextFactory::GetForBrowserContextIfExists(
    content::BrowserContext* context) {
  return static_cast<UsbChooserContext*>(
      GetInstance()->GetServiceForBrowserContext(context, /*create=*/false));
}

}  // namespace electron
