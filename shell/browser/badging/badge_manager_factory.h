// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_BADGING_BADGE_MANAGER_FACTORY_H_
#define ELECTRON_SHELL_BROWSER_BADGING_BADGE_MANAGER_FACTORY_H_

#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

namespace base {
template <typename T>
class NoDestructor;
}

namespace badging {

class BadgeManager;

// Singleton that provides access to context specific BadgeManagers.
class BadgeManagerFactory : public BrowserContextKeyedServiceFactory {
 public:
  // Gets the BadgeManager for the specified context
  static BadgeManager* GetForBrowserContext(content::BrowserContext* context);

  // Returns the BadgeManagerFactory singleton.
  static BadgeManagerFactory* GetInstance();

  // disable copy
  BadgeManagerFactory(const BadgeManagerFactory&) = delete;
  BadgeManagerFactory& operator=(const BadgeManagerFactory&) = delete;

 private:
  friend base::NoDestructor<BadgeManagerFactory>;

  BadgeManagerFactory();
  ~BadgeManagerFactory() override;

  // BrowserContextKeyedServiceFactory
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;
};

}  // namespace badging

#endif  // ELECTRON_SHELL_BROWSER_BADGING_BADGE_MANAGER_FACTORY_H_
