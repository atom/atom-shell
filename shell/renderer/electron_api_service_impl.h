// Copyright (c) 2019 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_RENDERER_ELECTRON_API_SERVICE_IMPL_H_
#define ELECTRON_SHELL_RENDERER_ELECTRON_API_SERVICE_IMPL_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_frame_observer.h"
#include "electron/buildflags/buildflags.h"
#include "electron/shell/common/api/api.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"

namespace electron {

class RendererClientBase;

class ElectronApiServiceImpl : public mojom::ElectronRenderer,
                               public content::RenderFrameObserver {
 public:
  ElectronApiServiceImpl(content::RenderFrame* render_frame,
                         RendererClientBase* renderer_client);
  ~ElectronApiServiceImpl() override;

  // disable copy
  ElectronApiServiceImpl(const ElectronApiServiceImpl&) = delete;
  ElectronApiServiceImpl& operator=(const ElectronApiServiceImpl&) = delete;

  void BindTo(mojo::PendingReceiver<mojom::ElectronRenderer> receiver);

  // mojom::ElectronRenderer
  void Message(bool internal,
               const std::string& channel,
               blink::CloneableMessage arguments) override;
  void ReceivePostMessage(const std::string& channel,
                          blink::TransferableMessage message) override;
  void TakeHeapSnapshot(mojo::ScopedHandle file,
                        TakeHeapSnapshotCallback callback) override;
  void ProcessPendingMessages();

  base::WeakPtr<ElectronApiServiceImpl> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  void OnInterfaceRequestForFrame(
      const std::string& interface_name,
      mojo::ScopedMessagePipeHandle* interface_pipe) override;

 private:
  // content::RenderFrameObserver
  void DidCreateDocumentElement() override;
  void OnDestruct() override;

  void OnConnectionError();

  // Whether the DOM document element has been created.
  bool document_created_ = false;
  service_manager::BinderRegistry registry_;

  mojo::PendingReceiver<mojom::ElectronRenderer> pending_receiver_;
  mojo::Receiver<mojom::ElectronRenderer> receiver_{this};

  RendererClientBase* renderer_client_;
  base::WeakPtrFactory<ElectronApiServiceImpl> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_RENDERER_ELECTRON_API_SERVICE_IMPL_H_
