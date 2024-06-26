// Copyright (c) 2022 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/electron_web_contents_utility_handler_impl.h"

#include <utility>

#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"

namespace electron {
ElectronWebContentsUtilityHandlerImpl::ElectronWebContentsUtilityHandlerImpl(
    content::RenderFrameHost* frame_host,
    mojo::PendingAssociatedReceiver<mojom::ElectronWebContentsUtility> receiver)
    : render_frame_host_id_(frame_host->GetGlobalId()) {
  content::WebContents* web_contents =
      content::WebContents::FromRenderFrameHost(frame_host);
  DCHECK(web_contents);
  content::WebContentsObserver::Observe(web_contents);

  receiver_.Bind(std::move(receiver));
  receiver_.set_disconnect_handler(base::BindOnce(
      &ElectronWebContentsUtilityHandlerImpl::OnConnectionError, GetWeakPtr()));
}

ElectronWebContentsUtilityHandlerImpl::
    ~ElectronWebContentsUtilityHandlerImpl() = default;

void ElectronWebContentsUtilityHandlerImpl::WebContentsDestroyed() {
  delete this;
}

void ElectronWebContentsUtilityHandlerImpl::OnConnectionError() {
  delete this;
}

void ElectronWebContentsUtilityHandlerImpl::OnFirstNonEmptyLayout() {
  api::WebContents* api_web_contents = api::WebContents::From(web_contents());
  if (api_web_contents) {
    api_web_contents->OnFirstNonEmptyLayout(GetRenderFrameHost());
  }
}

void ElectronWebContentsUtilityHandlerImpl::SetTemporaryZoomLevel(
    double level) {
  api::WebContents* api_web_contents = api::WebContents::From(web_contents());
  if (api_web_contents) {
    api_web_contents->SetTemporaryZoomLevel(level);
  }
}

void ElectronWebContentsUtilityHandlerImpl::DoGetZoomLevel(
    DoGetZoomLevelCallback callback) {
  api::WebContents* api_web_contents = api::WebContents::From(web_contents());
  if (api_web_contents) {
    api_web_contents->DoGetZoomLevel(std::move(callback));
  }
}

content::RenderFrameHost*
ElectronWebContentsUtilityHandlerImpl::GetRenderFrameHost() {
  return content::RenderFrameHost::FromID(render_frame_host_id_);
}

// static
void ElectronWebContentsUtilityHandlerImpl::Create(
    content::RenderFrameHost* frame_host,
    mojo::PendingAssociatedReceiver<mojom::ElectronWebContentsUtility>
        receiver) {
  new ElectronWebContentsUtilityHandlerImpl(frame_host, std::move(receiver));
}
}  // namespace electron
