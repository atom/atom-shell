// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHELL_BROWSER_ELECTRON_PLUGIN_INFO_HOST_IMPL_H_
#define SHELL_BROWSER_ELECTRON_PLUGIN_INFO_HOST_IMPL_H_

#include <string>
#include <vector>

#include "shell/common/plugin.mojom.h"

class GURL;

namespace content {
struct WebPluginInfo;
}  // namespace content

namespace url {
class Origin;
}

namespace electron {

// Implements ElectronPluginInfoHost interface.
class ElectronPluginInfoHostImpl : public mojom::ElectronPluginInfoHost {
 public:
  struct GetPluginInfo_Params;

  ElectronPluginInfoHostImpl();

  ElectronPluginInfoHostImpl(const ElectronPluginInfoHostImpl&) = delete;
  ElectronPluginInfoHostImpl& operator=(const ElectronPluginInfoHostImpl&) =
      delete;

  ~ElectronPluginInfoHostImpl() override;

  // mojom::ElectronPluginInfoHost
  void GetPluginInfo(const GURL& url,
                     const url::Origin& origin,
                     const std::string& mime_type,
                     GetPluginInfoCallback callback) override;

 private:
  // |params| wraps the parameters passed to |OnGetPluginInfo|, because
  // |base::Bind| doesn't support the required arity <http://crbug.com/98542>.
  void PluginsLoaded(const GetPluginInfo_Params& params,
                     GetPluginInfoCallback callback,
                     const std::vector<content::WebPluginInfo>& plugins);

  base::WeakPtrFactory<ElectronPluginInfoHostImpl> weak_factory_{this};
};

}  // namespace electron

#endif  // SHELL_BROWSER_ELECTRON_PLUGIN_INFO_HOST_IMPL_H_
