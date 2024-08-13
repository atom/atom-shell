// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/browser/electron_plugin_info_host_impl.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/common/content_constants.h"
#include "url/gurl.h"
#include "url/origin.h"

using content::PluginService;
using content::WebPluginInfo;

namespace electron {

ElectronPluginInfoHostImpl::ElectronPluginInfoHostImpl() = default;

ElectronPluginInfoHostImpl::~ElectronPluginInfoHostImpl() = default;

struct ElectronPluginInfoHostImpl::GetPluginInfo_Params {
  GURL url;
  url::Origin main_frame_origin;
  std::string mime_type;
};

void ElectronPluginInfoHostImpl::GetPluginInfo(const GURL& url,
                                               const url::Origin& origin,
                                               const std::string& mime_type,
                                               GetPluginInfoCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  GetPluginInfo_Params params = {url, origin, mime_type};
  PluginService::GetInstance()->GetPlugins(
      base::BindOnce(&ElectronPluginInfoHostImpl::PluginsLoaded,
                     weak_factory_.GetWeakPtr(), params, std::move(callback)));
}

void ElectronPluginInfoHostImpl::PluginsLoaded(
    const GetPluginInfo_Params& params,
    GetPluginInfoCallback callback,
    const std::vector<WebPluginInfo>& plugins) {
  mojom::PluginInfoPtr output = mojom::PluginInfo::New();
  std::vector<WebPluginInfo> matching_plugins;
  std::vector<std::string> mime_types;
  PluginService::GetInstance()->GetPluginInfoArray(
      params.url, params.mime_type, true, &matching_plugins, &mime_types);
  if (!matching_plugins.empty()) {
    output->plugin = matching_plugins[0];
    output->actual_mime_type = mime_types[0];
  }

  std::move(callback).Run(std::move(output));
}

}  // namespace electron
