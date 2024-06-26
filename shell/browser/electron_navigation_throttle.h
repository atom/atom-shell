// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_ELECTRON_NAVIGATION_THROTTLE_H_
#define ELECTRON_SHELL_BROWSER_ELECTRON_NAVIGATION_THROTTLE_H_

#include "content/public/browser/navigation_throttle.h"

namespace electron {

class ElectronNavigationThrottle : public content::NavigationThrottle {
 public:
  explicit ElectronNavigationThrottle(content::NavigationHandle* handle);
  ~ElectronNavigationThrottle() override;

  // disable copy
  ElectronNavigationThrottle(const ElectronNavigationThrottle&) = delete;
  ElectronNavigationThrottle& operator=(const ElectronNavigationThrottle&) =
      delete;

  // content::NavigationThrottle
  ElectronNavigationThrottle::ThrottleCheckResult WillStartRequest() override;
  ElectronNavigationThrottle::ThrottleCheckResult WillRedirectRequest()
      override;

  const char* GetNameForLogging() override;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_ELECTRON_NAVIGATION_THROTTLE_H_
