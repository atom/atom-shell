// Copyright (c) 2022 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_USB_USB_CHOOSER_CONTEXT_FACTORY_H_
#define ELECTRON_SHELL_BROWSER_USB_USB_CHOOSER_CONTEXT_FACTORY_H_

#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

namespace base {
template <typename T>
class NoDestructor;
}  // namespace base

namespace electron {

class UsbChooserContext;

class UsbChooserContextFactory : public BrowserContextKeyedServiceFactory {
 public:
  static UsbChooserContext* GetForBrowserContext(
      content::BrowserContext* context);
  static UsbChooserContext* GetForBrowserContextIfExists(
      content::BrowserContext* context);
  static UsbChooserContextFactory* GetInstance();

  UsbChooserContextFactory(const UsbChooserContextFactory&) = delete;
  UsbChooserContextFactory& operator=(const UsbChooserContextFactory&) = delete;

 private:
  friend base::NoDestructor<UsbChooserContextFactory>;

  UsbChooserContextFactory();
  ~UsbChooserContextFactory() override;

  // BrowserContextKeyedServiceFactory methods:
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* profile) const override;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_USB_USB_CHOOSER_CONTEXT_FACTORY_H_
