// Copyright (c) 2019 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/extensions/electron_extensions_api_client.h"

#include <memory>
#include <string>

#include "electron/buildflags/buildflags.h"
#include "extensions/browser/guest_view/extensions_guest_view_manager_delegate.h"
#include "extensions/browser/guest_view/mime_handler_view/mime_handler_view_guest_delegate.h"
#include "printing/buildflags/buildflags.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/extensions/api/management/electron_management_api_delegate.h"
#include "shell/browser/extensions/electron_extension_web_contents_observer.h"
#include "shell/browser/extensions/electron_messaging_delegate.h"
#include "v8/include/v8.h"

#if BUILDFLAG(ENABLE_PRINTING)
#include "components/printing/browser/print_manager_utils.h"
#include "shell/browser/printing/print_view_manager_electron.h"
#endif

#if BUILDFLAG(ENABLE_PDF_VIEWER)
#include "components/pdf/browser/pdf_document_helper.h"  // nogncheck
#include "shell/browser/electron_pdf_document_helper_client.h"
#endif

namespace extensions {

class ElectronGuestViewManagerDelegate
    : public ExtensionsGuestViewManagerDelegate {
 public:
  ElectronGuestViewManagerDelegate() : ExtensionsGuestViewManagerDelegate() {}
  ~ElectronGuestViewManagerDelegate() override = default;

  // disable copy
  ElectronGuestViewManagerDelegate(const ElectronGuestViewManagerDelegate&) =
      delete;
  ElectronGuestViewManagerDelegate& operator=(
      const ElectronGuestViewManagerDelegate&) = delete;

  // GuestViewManagerDelegate:
  void OnGuestAdded(content::WebContents* guest_web_contents) const override {
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope scope(isolate);
    electron::api::WebContents::FromOrCreate(isolate, guest_web_contents);
  }
};

class ElectronMimeHandlerViewGuestDelegate
    : public MimeHandlerViewGuestDelegate {
 public:
  ElectronMimeHandlerViewGuestDelegate() = default;
  ~ElectronMimeHandlerViewGuestDelegate() override = default;

  // disable copy
  ElectronMimeHandlerViewGuestDelegate(
      const ElectronMimeHandlerViewGuestDelegate&) = delete;
  ElectronMimeHandlerViewGuestDelegate& operator=(
      const ElectronMimeHandlerViewGuestDelegate&) = delete;

  // MimeHandlerViewGuestDelegate.
  bool HandleContextMenu(content::RenderFrameHost& render_frame_host,
                         const content::ContextMenuParams& params) override {
    auto* web_contents =
        content::WebContents::FromRenderFrameHost(&render_frame_host);
    if (!web_contents)
      return true;

    electron::api::WebContents* api_web_contents =
        electron::api::WebContents::From(
            web_contents->GetOutermostWebContents());
    if (api_web_contents)
      api_web_contents->HandleContextMenu(render_frame_host, params);
    return true;
  }

  void RecordLoadMetric(bool in_main_frame,
                        const std::string& mime_type,
                        content::BrowserContext* browser_context) override {}
};

ElectronExtensionsAPIClient::ElectronExtensionsAPIClient() = default;
ElectronExtensionsAPIClient::~ElectronExtensionsAPIClient() = default;

MessagingDelegate* ElectronExtensionsAPIClient::GetMessagingDelegate() {
  if (!messaging_delegate_)
    messaging_delegate_ = std::make_unique<ElectronMessagingDelegate>();
  return messaging_delegate_.get();
}

void ElectronExtensionsAPIClient::AttachWebContentsHelpers(
    content::WebContents* web_contents) const {
#if BUILDFLAG(ENABLE_PRINTING)
  electron::PrintViewManagerElectron::CreateForWebContents(web_contents);
#endif

  extensions::ElectronExtensionWebContentsObserver::CreateForWebContents(
      web_contents);
}

ManagementAPIDelegate*
ElectronExtensionsAPIClient::CreateManagementAPIDelegate() const {
  return new ElectronManagementAPIDelegate;
}

std::unique_ptr<MimeHandlerViewGuestDelegate>
ElectronExtensionsAPIClient::CreateMimeHandlerViewGuestDelegate(
    MimeHandlerViewGuest* guest) const {
  return std::make_unique<ElectronMimeHandlerViewGuestDelegate>();
}

std::unique_ptr<guest_view::GuestViewManagerDelegate>
ElectronExtensionsAPIClient::CreateGuestViewManagerDelegate() const {
  return std::make_unique<ElectronGuestViewManagerDelegate>();
}

}  // namespace extensions
