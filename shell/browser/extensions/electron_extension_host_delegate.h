// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_EXTENSIONS_ELECTRON_EXTENSION_HOST_DELEGATE_H_
#define ELECTRON_SHELL_BROWSER_EXTENSIONS_ELECTRON_EXTENSION_HOST_DELEGATE_H_

#include <memory>
#include <string>

#include "extensions/browser/extension_host_delegate.h"

namespace extensions {

// A minimal ExtensionHostDelegate.
class ElectronExtensionHostDelegate : public ExtensionHostDelegate {
 public:
  ElectronExtensionHostDelegate();
  ~ElectronExtensionHostDelegate() override;

  // disable copy
  ElectronExtensionHostDelegate(const ElectronExtensionHostDelegate&) = delete;
  ElectronExtensionHostDelegate& operator=(
      const ElectronExtensionHostDelegate&) = delete;

  // ExtensionHostDelegate implementation.
  void OnExtensionHostCreated(content::WebContents* web_contents) override;
  void OnMainFrameCreatedForBackgroundPage(ExtensionHost* host) override {}
  content::JavaScriptDialogManager* GetJavaScriptDialogManager() override;
  void CreateTab(std::unique_ptr<content::WebContents> web_contents,
                 const std::string& extension_id,
                 WindowOpenDisposition disposition,
                 const blink::mojom::WindowFeatures& window_features,
                 bool user_gesture) override;
  void ProcessMediaAccessRequest(content::WebContents* web_contents,
                                 const content::MediaStreamRequest& request,
                                 content::MediaResponseCallback callback,
                                 const Extension* extension) override;
  bool CheckMediaAccessPermission(content::RenderFrameHost* render_frame_host,
                                  const url::Origin& security_origin,
                                  blink::mojom::MediaStreamType type,
                                  const Extension* extension) override;
  content::PictureInPictureResult EnterPictureInPicture(
      content::WebContents* web_contents) override;
  void ExitPictureInPicture() override;
};

}  // namespace extensions

#endif  // ELECTRON_SHELL_BROWSER_EXTENSIONS_ELECTRON_EXTENSION_HOST_DELEGATE_H_
