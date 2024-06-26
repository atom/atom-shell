// Copyright (c) 2022 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_ELECTRON_API_IPC_HANDLER_IMPL_H_
#define ELECTRON_SHELL_BROWSER_ELECTRON_API_IPC_HANDLER_IMPL_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "electron/shell/common/api/api.mojom.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "shell/browser/api/electron_api_web_contents.h"

namespace content {
class RenderFrameHost;
}

namespace electron {
class ElectronApiIPCHandlerImpl : public mojom::ElectronApiIPC,
                                  private content::WebContentsObserver {
 public:
  explicit ElectronApiIPCHandlerImpl(
      content::RenderFrameHost* render_frame_host,
      mojo::PendingAssociatedReceiver<mojom::ElectronApiIPC> receiver);

  static void Create(
      content::RenderFrameHost* frame_host,
      mojo::PendingAssociatedReceiver<mojom::ElectronApiIPC> receiver);

  // disable copy
  ElectronApiIPCHandlerImpl(const ElectronApiIPCHandlerImpl&) = delete;
  ElectronApiIPCHandlerImpl& operator=(const ElectronApiIPCHandlerImpl&) =
      delete;

  // mojom::ElectronApiIPC:
  void Message(bool internal,
               const std::string& channel,
               blink::CloneableMessage arguments) override;
  void Invoke(bool internal,
              const std::string& channel,
              blink::CloneableMessage arguments,
              InvokeCallback callback) override;
  void ReceivePostMessage(const std::string& channel,
                          blink::TransferableMessage message) override;
  void MessageSync(bool internal,
                   const std::string& channel,
                   blink::CloneableMessage arguments,
                   MessageSyncCallback callback) override;
  void MessageHost(const std::string& channel,
                   blink::CloneableMessage arguments) override;

  base::WeakPtr<ElectronApiIPCHandlerImpl> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  ~ElectronApiIPCHandlerImpl() override;

  // content::WebContentsObserver:
  void WebContentsDestroyed() override;

  void OnConnectionError();

  content::RenderFrameHost* GetRenderFrameHost();

  content::GlobalRenderFrameHostId render_frame_host_id_;

  mojo::AssociatedReceiver<mojom::ElectronApiIPC> receiver_{this};

  base::WeakPtrFactory<ElectronApiIPCHandlerImpl> weak_factory_{this};
};
}  // namespace electron
#endif  // ELECTRON_SHELL_BROWSER_ELECTRON_API_IPC_HANDLER_IMPL_H_
