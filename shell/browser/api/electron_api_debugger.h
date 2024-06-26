// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_DEBUGGER_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_DEBUGGER_H_

#include <map>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/browser/web_contents_observer.h"
#include "gin/arguments.h"
#include "gin/handle.h"
#include "gin/wrappable.h"
#include "shell/browser/event_emitter_mixin.h"
#include "shell/common/gin_helper/promise.h"

namespace content {
class DevToolsAgentHost;
class WebContents;
}  // namespace content

namespace electron::api {

class Debugger : public gin::Wrappable<Debugger>,
                 public gin_helper::EventEmitterMixin<Debugger>,
                 public content::DevToolsAgentHostClient,
                 private content::WebContentsObserver {
 public:
  static gin::Handle<Debugger> Create(v8::Isolate* isolate,
                                      content::WebContents* web_contents);

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  // disable copy
  Debugger(const Debugger&) = delete;
  Debugger& operator=(const Debugger&) = delete;

 protected:
  Debugger(v8::Isolate* isolate, content::WebContents* web_contents);
  ~Debugger() override;

  // content::DevToolsAgentHostClient:
  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override;
  void DispatchProtocolMessage(content::DevToolsAgentHost* agent_host,
                               base::span<const uint8_t> message) override;

  // content::WebContentsObserver:
  void RenderFrameHostChanged(content::RenderFrameHost* old_rfh,
                              content::RenderFrameHost* new_rfh) override;

 private:
  using PendingRequestMap =
      std::map<int, gin_helper::Promise<base::Value::Dict>>;

  void Attach(gin::Arguments* args);
  bool IsAttached();
  void Detach();
  v8::Local<v8::Promise> SendCommand(gin::Arguments* args);
  void ClearPendingRequests();

  raw_ptr<content::WebContents> web_contents_;  // Weak Reference.
  scoped_refptr<content::DevToolsAgentHost> agent_host_;

  PendingRequestMap pending_requests_;
  int previous_request_id_ = 0;
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_DEBUGGER_H_
