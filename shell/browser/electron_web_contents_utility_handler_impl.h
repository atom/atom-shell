// Copyright (c) 2022 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_ELECTRON_WEB_CONTENTS_UTILITY_HANDLER_IMPL_H_
#define ELECTRON_SHELL_BROWSER_ELECTRON_WEB_CONTENTS_UTILITY_HANDLER_IMPL_H_

#include <vector>

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
class ElectronWebContentsUtilityHandlerImpl
    : public mojom::ElectronWebContentsUtility,
      private content::WebContentsObserver {
 public:
  explicit ElectronWebContentsUtilityHandlerImpl(
      content::RenderFrameHost* render_frame_host,
      mojo::PendingAssociatedReceiver<mojom::ElectronWebContentsUtility>
          receiver);

  static void Create(
      content::RenderFrameHost* frame_host,
      mojo::PendingAssociatedReceiver<mojom::ElectronWebContentsUtility>
          receiver);

  // disable copy
  ElectronWebContentsUtilityHandlerImpl(
      const ElectronWebContentsUtilityHandlerImpl&) = delete;
  ElectronWebContentsUtilityHandlerImpl& operator=(
      const ElectronWebContentsUtilityHandlerImpl&) = delete;

  // mojom::ElectronWebContentsUtility:
  void OnFirstNonEmptyLayout() override;
  void SetTemporaryZoomLevel(double level) override;
  void DoGetZoomLevel(DoGetZoomLevelCallback callback) override;

  base::WeakPtr<ElectronWebContentsUtilityHandlerImpl> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  ~ElectronWebContentsUtilityHandlerImpl() override;

  // content::WebContentsObserver:
  void WebContentsDestroyed() override;

  void OnConnectionError();

  content::RenderFrameHost* GetRenderFrameHost();

  content::GlobalRenderFrameHostId render_frame_host_id_;

  mojo::AssociatedReceiver<mojom::ElectronWebContentsUtility> receiver_{this};

  base::WeakPtrFactory<ElectronWebContentsUtilityHandlerImpl> weak_factory_{
      this};
};
}  // namespace electron
#endif  // ELECTRON_SHELL_BROWSER_ELECTRON_WEB_CONTENTS_UTILITY_HANDLER_IMPL_H_
