// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_SERIAL_SERIAL_CHOOSER_CONTEXT_FACTORY_H_
#define ELECTRON_SHELL_BROWSER_SERIAL_SERIAL_CHOOSER_CONTEXT_FACTORY_H_

#include "components/keyed_service/content/browser_context_keyed_service_factory.h"
#include "shell/browser/serial/serial_chooser_context.h"

namespace base {
template <typename T>
class NoDestructor;
}  // namespace base

namespace electron {

class SerialChooserContext;

class SerialChooserContextFactory : public BrowserContextKeyedServiceFactory {
 public:
  static SerialChooserContext* GetForBrowserContext(
      content::BrowserContext* context);
  static SerialChooserContextFactory* GetInstance();

 private:
  friend base::NoDestructor<SerialChooserContextFactory>;

  SerialChooserContextFactory();
  ~SerialChooserContextFactory() override;

  // disable copy
  SerialChooserContextFactory(const SerialChooserContextFactory&) = delete;
  SerialChooserContextFactory& operator=(const SerialChooserContextFactory&) =
      delete;

  // BrowserContextKeyedServiceFactory methods:
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_SERIAL_SERIAL_CHOOSER_CONTEXT_FACTORY_H_
