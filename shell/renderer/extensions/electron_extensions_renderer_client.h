// Copyright (c) 2017 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_RENDERER_EXTENSIONS_ELECTRON_EXTENSIONS_RENDERER_CLIENT_H_
#define ELECTRON_SHELL_RENDERER_EXTENSIONS_ELECTRON_EXTENSIONS_RENDERER_CLIENT_H_

#include <memory>

#include "extensions/renderer/extensions_renderer_client.h"

namespace content {
class RenderFrame;
}

namespace extensions {
class Dispatcher;
}

namespace electron {

class ElectronExtensionsRendererClient
    : public extensions::ExtensionsRendererClient {
 public:
  ElectronExtensionsRendererClient();
  ~ElectronExtensionsRendererClient() override;

  // disable copy
  ElectronExtensionsRendererClient(const ElectronExtensionsRendererClient&) =
      delete;
  ElectronExtensionsRendererClient& operator=(
      const ElectronExtensionsRendererClient&) = delete;

  // extensions::ExtensionsRendererClient:
  bool IsIncognitoProcess() const override;
  int GetLowestIsolatedWorldId() const override;

  bool AllowPopup();

  void RunScriptsAtDocumentStart(content::RenderFrame* render_frame);
  void RunScriptsAtDocumentEnd(content::RenderFrame* render_frame);
  void RunScriptsAtDocumentIdle(content::RenderFrame* render_frame);
};

}  // namespace electron

#endif  // ELECTRON_SHELL_RENDERER_EXTENSIONS_ELECTRON_EXTENSIONS_RENDERER_CLIENT_H_
