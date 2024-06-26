// Copyright (c) 2022 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_WEBAUTHN_ELECTRON_AUTHENTICATOR_REQUEST_DELEGATE_H_
#define ELECTRON_SHELL_BROWSER_WEBAUTHN_ELECTRON_AUTHENTICATOR_REQUEST_DELEGATE_H_

#include "content/public/browser/authenticator_request_client_delegate.h"

namespace electron {

// Modified from chrome/browser/webauthn/chrome_authenticator_request_delegate.h
class ElectronWebAuthenticationDelegate
    : public content::WebAuthenticationDelegate {
 public:
  ~ElectronWebAuthenticationDelegate() override;

  // content::WebAuthenticationDelegate
  bool SupportsResidentKeys(
      content::RenderFrameHost* render_frame_host) override;
};

}  // namespace electron
#endif  // ELECTRON_SHELL_BROWSER_WEBAUTHN_ELECTRON_AUTHENTICATOR_REQUEST_DELEGATE_H_
