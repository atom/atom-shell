// Copyright (c) 2017 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_RENDERER_ELECTRON_RENDER_FRAME_OBSERVER_H_
#define ELECTRON_SHELL_RENDERER_ELECTRON_RENDER_FRAME_OBSERVER_H_

#include <string>

#include "content/public/renderer/render_frame_observer.h"
#include "ipc/ipc_platform_file.h"
#include "third_party/blink/public/web/web_local_frame.h"

namespace electron {

class RendererClientBase;

// Helper class to forward the messages to the client.
class ElectronRenderFrameObserver : private content::RenderFrameObserver {
 public:
  ElectronRenderFrameObserver(content::RenderFrame* frame,
                              RendererClientBase* renderer_client);

  // disable copy
  ElectronRenderFrameObserver(const ElectronRenderFrameObserver&) = delete;
  ElectronRenderFrameObserver& operator=(const ElectronRenderFrameObserver&) =
      delete;

 private:
  // content::RenderFrameObserver:
  void DidClearWindowObject() override;
  void DidInstallConditionalFeatures(v8::Handle<v8::Context> context,
                                     int world_id) override;
  void WillReleaseScriptContext(v8::Local<v8::Context> context,
                                int world_id) override;
  void OnDestruct() override;
  void DidMeaningfulLayout(blink::WebMeaningfulLayout layout_type) override;

  [[nodiscard]] bool ShouldNotifyClient(int world_id) const;

  void CreateIsolatedWorldContext();
  void OnTakeHeapSnapshot(IPC::PlatformFileForTransit file_handle,
                          const std::string& channel);

  bool has_delayed_node_initialization_ = false;
  content::RenderFrame* render_frame_;
  RendererClientBase* renderer_client_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_RENDERER_ELECTRON_RENDER_FRAME_OBSERVER_H_
