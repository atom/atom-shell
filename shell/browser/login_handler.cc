// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/login_handler.h"

#include <utility>

#include "base/task/sequenced_task_runner.h"
#include "gin/arguments.h"
#include "gin/dictionary.h"
#include "shell/browser/api/electron_api_app.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/javascript_environment.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/gurl_converter.h"
#include "shell/common/gin_converters/net_converter.h"
#include "shell/common/gin_converters/value_converter.h"

using content::BrowserThread;

namespace electron {

LoginHandler::LoginHandler(
    const net::AuthChallengeInfo& auth_info,
    content::WebContents* web_contents,
    bool is_main_frame,
    base::ProcessId process_id,
    const GURL& url,
    scoped_refptr<net::HttpResponseHeaders> response_headers,
    bool first_auth_attempt,
    LoginAuthRequiredCallback auth_required_callback)
    : auth_required_callback_(std::move(auth_required_callback)) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&LoginHandler::EmitEvent, weak_factory_.GetWeakPtr(),
                     auth_info, web_contents, is_main_frame, process_id, url,
                     response_headers, first_auth_attempt));
}

void LoginHandler::EmitEvent(
    net::AuthChallengeInfo auth_info,
    content::WebContents* web_contents,
    bool is_main_frame,
    base::ProcessId process_id,
    const GURL& url,
    scoped_refptr<net::HttpResponseHeaders> response_headers,
    bool first_auth_attempt) {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope scope(isolate);

  raw_ptr<api::WebContents> api_web_contents = nullptr;
  if (web_contents) {
    api_web_contents = api::WebContents::From(web_contents);
    if (!api_web_contents) {
      std::move(auth_required_callback_).Run(std::nullopt);
      return;
    }
  }

  auto details = gin::Dictionary::CreateEmpty(isolate);
  details.Set("url", url);
  details.Set("pid", process_id);

  // These parameters aren't documented, and I'm not sure that they're useful,
  // but we might as well stick 'em on the details object. If it turns out they
  // are useful, we can add them to the docs :)
  details.Set("isMainFrame", is_main_frame);
  details.Set("firstAuthAttempt", first_auth_attempt);
  details.Set("responseHeaders", response_headers.get());

  auto weak_this = weak_factory_.GetWeakPtr();
  bool default_prevented = false;
  if (api_web_contents) {
    default_prevented =
        api_web_contents->Emit("login", std::move(details), auth_info,
                               base::BindOnce(&LoginHandler::CallbackFromJS,
                                              weak_factory_.GetWeakPtr()));
  } else {
    default_prevented =
        api::App::Get()->Emit("login", nullptr, std::move(details), auth_info,
                              base::BindOnce(&LoginHandler::CallbackFromJS,
                                             weak_factory_.GetWeakPtr()));
  }
  // ⚠️ NB, if CallbackFromJS is called during Emit(), |this| will have been
  // deleted. Check the weak ptr before accessing any member variables to
  // prevent UAF.
  if (weak_this && !default_prevented && auth_required_callback_) {
    std::move(auth_required_callback_).Run(std::nullopt);
  }
}

LoginHandler::~LoginHandler() = default;

void LoginHandler::CallbackFromJS(gin::Arguments* args) {
  if (auth_required_callback_) {
    std::u16string username, password;
    if (!args->GetNext(&username) || !args->GetNext(&password)) {
      std::move(auth_required_callback_).Run(std::nullopt);
      return;
    }
    std::move(auth_required_callback_)
        .Run(net::AuthCredentials(username, password));
  }
}

}  // namespace electron
