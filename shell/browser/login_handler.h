// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_LOGIN_HANDLER_H_
#define ELECTRON_SHELL_BROWSER_LOGIN_HANDLER_H_

#include "base/values.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/login_delegate.h"
#include "content/public/browser/web_contents_observer.h"

namespace content {
class WebContents;
}

namespace gin {
class Arguments;
}

namespace electron {

// Handles HTTP basic auth.
class LoginHandler : public content::LoginDelegate,
                     private content::WebContentsObserver {
 public:
  LoginHandler(const net::AuthChallengeInfo& auth_info,
               content::WebContents* web_contents,
               bool is_main_frame,
               const GURL& url,
               scoped_refptr<net::HttpResponseHeaders> response_headers,
               bool first_auth_attempt,
               LoginAuthRequiredCallback auth_required_callback);
  ~LoginHandler() override;

  // disable copy
  LoginHandler(const LoginHandler&) = delete;
  LoginHandler& operator=(const LoginHandler&) = delete;

 private:
  void EmitEvent(net::AuthChallengeInfo auth_info,
                 bool is_main_frame,
                 const GURL& url,
                 scoped_refptr<net::HttpResponseHeaders> response_headers,
                 bool first_auth_attempt);
  void CallbackFromJS(gin::Arguments* args);

  LoginAuthRequiredCallback auth_required_callback_;

  base::WeakPtrFactory<LoginHandler> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_LOGIN_HANDLER_H_
