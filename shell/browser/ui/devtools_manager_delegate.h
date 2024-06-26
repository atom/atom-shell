// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#ifndef ELECTRON_SHELL_BROWSER_UI_DEVTOOLS_MANAGER_DELEGATE_H_
#define ELECTRON_SHELL_BROWSER_UI_DEVTOOLS_MANAGER_DELEGATE_H_

#include <string>

#include "base/compiler_specific.h"
#include "content/public/browser/devtools_manager_delegate.h"

namespace content {
class BrowserContext;
}

namespace electron {

class DevToolsManagerDelegate : public content::DevToolsManagerDelegate {
 public:
  static void StartHttpHandler();

  DevToolsManagerDelegate();
  ~DevToolsManagerDelegate() override;

  // disable copy
  DevToolsManagerDelegate(const DevToolsManagerDelegate&) = delete;
  DevToolsManagerDelegate& operator=(const DevToolsManagerDelegate&) = delete;

  // DevToolsManagerDelegate implementation.
  void Inspect(content::DevToolsAgentHost* agent_host) override;
  void HandleCommand(content::DevToolsAgentHostClientChannel* channel,
                     base::span<const uint8_t> message,
                     NotHandledCallback callback) override;
  scoped_refptr<content::DevToolsAgentHost> CreateNewTarget(
      const GURL& url,
      TargetType target_type) override;
  std::string GetDiscoveryPageHTML() override;
  bool HasBundledFrontendResources() override;
  content::BrowserContext* GetDefaultBrowserContext() override;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_DEVTOOLS_MANAGER_DELEGATE_H_
