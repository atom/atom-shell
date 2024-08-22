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

ElectronExtensionsRendererClient::~ElectronExtensionsRendererClient() = default;

bool ElectronExtensionsRendererClient::IsIncognitoProcess() const {
  // app_shell doesn't support off-the-record contexts.
  return false;
}

int ElectronExtensionsRendererClient::GetLowestIsolatedWorldId() const {
  return WorldIDs::ISOLATED_WORLD_ID_EXTENSIONS;
}

bool ElectronExtensionsRendererClient::AllowPopup() {
  // TODO(samuelmaddock):
  return false;
}

void ElectronExtensionsRendererClient::RunScriptsAtDocumentStart(
    content::RenderFrame* render_frame) {
  dispatcher()->RunScriptsAtDocumentStart(render_frame);
}

void ElectronExtensionsRendererClient::RunScriptsAtDocumentEnd(
    content::RenderFrame* render_frame) {
  dispatcher()->RunScriptsAtDocumentEnd(render_frame);
}

void ElectronExtensionsRendererClient::RunScriptsAtDocumentIdle(
    content::RenderFrame* render_frame) {
  dispatcher()->RunScriptsAtDocumentIdle(render_frame);
}

}  // namespace electron
