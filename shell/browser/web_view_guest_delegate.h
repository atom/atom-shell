// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_WEB_VIEW_GUEST_DELEGATE_H_
#define ELECTRON_SHELL_BROWSER_WEB_VIEW_GUEST_DELEGATE_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/browser_plugin_guest_delegate.h"
#include "shell/browser/web_contents_zoom_controller.h"
#include "shell/browser/web_contents_zoom_observer.h"

namespace electron {

namespace api {
class WebContents;
}

class WebViewGuestDelegate : public content::BrowserPluginGuestDelegate,
                             private WebContentsZoomObserver {
 public:
  WebViewGuestDelegate(content::WebContents* embedder,
                       api::WebContents* api_web_contents);
  ~WebViewGuestDelegate() override;

  // disable copy
  WebViewGuestDelegate(const WebViewGuestDelegate&) = delete;
  WebViewGuestDelegate& operator=(const WebViewGuestDelegate&) = delete;

  // Attach to the iframe.
  void AttachToIframe(content::WebContents* embedder_web_contents,
                      int embedder_frame_id);
  void WillDestroy();

 protected:
  // content::BrowserPluginGuestDelegate:
  content::WebContents* GetOwnerWebContents() final;
  std::unique_ptr<content::WebContents> CreateNewGuestWindow(
      const content::WebContents::CreateParams& create_params) final;
  base::WeakPtr<content::BrowserPluginGuestDelegate> GetGuestDelegateWeakPtr()
      final;

  // WebContentsZoomObserver:
  void OnZoomControllerDestroyed(
      WebContentsZoomController* zoom_controller) override;
  void OnZoomChanged(
      const WebContentsZoomController::ZoomChangedEventData& data) override;

 private:
  void ResetZoomController();

  // The WebContents that attaches this guest view.
  raw_ptr<content::WebContents> embedder_web_contents_ = nullptr;

  // The zoom controller of the embedder that is used
  // to subscribe for zoom changes.
  raw_ptr<WebContentsZoomController> embedder_zoom_controller_ = nullptr;

  raw_ptr<api::WebContents> api_web_contents_ = nullptr;

  base::WeakPtrFactory<WebViewGuestDelegate> weak_ptr_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_WEB_VIEW_GUEST_DELEGATE_H_
