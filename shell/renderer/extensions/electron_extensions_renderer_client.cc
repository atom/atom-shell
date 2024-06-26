// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/renderer/extensions/electron_extensions_renderer_client.h"

#include <string>

#include "content/public/renderer/render_thread.h"
#include "extensions/common/constants.h"
#include "extensions/common/manifest_handlers/background_info.h"
#include "extensions/renderer/dispatcher.h"
#include "shell/common/world_ids.h"

namespace electron {

ElectronExtensionsRendererClient::ElectronExtensionsRendererClient() {}

void ElectronExtensionsRendererClient::RenderThreadStarted() {
  dispatcher_ =
      std::make_unique<extensions::Dispatcher>(std::move(api_providers_));
  dispatcher_->OnRenderThreadStarted(content::RenderThread::Get());
}

ElectronExtensionsRendererClient::~ElectronExtensionsRendererClient() = default;

bool ElectronExtensionsRendererClient::IsIncognitoProcess() const {
  // app_shell doesn't support off-the-record contexts.
  return false;
}

int ElectronExtensionsRendererClient::GetLowestIsolatedWorldId() const {
  return WorldIDs::ISOLATED_WORLD_ID_EXTENSIONS;
}

extensions::Dispatcher* ElectronExtensionsRendererClient::GetDispatcher() {
  return dispatcher_.get();
}

bool ElectronExtensionsRendererClient::
    ExtensionAPIEnabledForServiceWorkerScript(const GURL& scope,
                                              const GURL& script_url) const {
  if (!script_url.SchemeIs(extensions::kExtensionScheme))
    return false;

  const extensions::Extension* extension =
      extensions::RendererExtensionRegistry::Get()->GetExtensionOrAppByURL(
          script_url);

  if (!extension ||
      !extensions::BackgroundInfo::IsServiceWorkerBased(extension))
    return false;

  if (scope != extension->url())
    return false;

  const std::string& sw_script =
      extensions::BackgroundInfo::GetBackgroundServiceWorkerScript(extension);

  return extension->GetResourceURL(sw_script) == script_url;
}

bool ElectronExtensionsRendererClient::AllowPopup() {
  // TODO(samuelmaddock):
  return false;
}

void ElectronExtensionsRendererClient::RunScriptsAtDocumentStart(
    content::RenderFrame* render_frame) {
  dispatcher_->RunScriptsAtDocumentStart(render_frame);
}

void ElectronExtensionsRendererClient::RunScriptsAtDocumentEnd(
    content::RenderFrame* render_frame) {
  dispatcher_->RunScriptsAtDocumentEnd(render_frame);
}

void ElectronExtensionsRendererClient::RunScriptsAtDocumentIdle(
    content::RenderFrame* render_frame) {
  dispatcher_->RunScriptsAtDocumentIdle(render_frame);
}

}  // namespace electron
