// Copyright (c) 2020 Samuel Maddock <sam@samuelmaddock.com>.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_web_frame_main.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/lazy_instance.h"
#include "base/logging.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/public/browser/render_frame_host.h"
#include "gin/object_template_builder.h"
#include "shell/browser/browser.h"
#include "shell/common/gin_converters/frame_converter.h"
#include "shell/common/gin_converters/gurl_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/object_template_builder.h"
#include "shell/common/node_includes.h"

namespace electron {

namespace api {

typedef std::unordered_map<content::RenderFrameHost*, WebFrame*> RenderFrameMap;
base::LazyInstance<RenderFrameMap>::DestructorAtExit g_render_frame_map =
    LAZY_INSTANCE_INITIALIZER;

gin::WrapperInfo WebFrame::kWrapperInfo = {gin::kEmbedderNativeGin};

WebFrame::WebFrame(content::RenderFrameHost* rfh) : render_frame_(rfh) {
  g_render_frame_map.Get().emplace(rfh, this);
  LOG(INFO) << "Added WebFrame to map: " << GetFrameTreeNodeID();
}

WebFrame::~WebFrame() {
  g_render_frame_map.Get().erase(render_frame_);
  LOG(INFO) << "Removed WebFrame from map: " << GetFrameTreeNodeID();
}

void WebFrame::ReleaseRenderFrame() {
  render_frame_ = nullptr;
}

void WebFrame::ExecuteJavaScript(const base::string16& code,
                                 bool has_user_gesture) {
  if (has_user_gesture) {
    auto* ftn = content::FrameTreeNode::From(render_frame_);
    ftn->UpdateUserActivationState(
        blink::mojom::UserActivationUpdateType::kNotifyActivation,
        blink::mojom::UserActivationNotificationType::kTest);
  }

  render_frame_->ExecuteJavaScriptForTests(code, base::NullCallback());
}

bool WebFrame::Reload(gin::Arguments* args) {
  if (render_frame_ == nullptr) {
    args->isolate()->ThrowException(v8::Exception::Error(
        gin::StringToV8(args->isolate(),
                        "Render frame was torn down before WebFrame.reload "
                        "could be executed")));
    // return v8::Null(args->isolate());
    return false;
  }
  return render_frame_->Reload();
}

int WebFrame::GetFrameTreeNodeID() const {
  return render_frame_->GetFrameTreeNodeId();
}

int WebFrame::GetRoutingID() const {
  return render_frame_->GetRoutingID();
}

GURL WebFrame::GetURL() const {
  return render_frame_->GetLastCommittedURL();
}

content::RenderFrameHost* WebFrame::GetTop() {
  return render_frame_->GetMainFrame();
}

content::RenderFrameHost* WebFrame::GetParent() {
  return render_frame_->GetParent();
}

std::vector<content::RenderFrameHost*> WebFrame::GetChildren() {
  std::vector<content::RenderFrameHost*> frame_hosts;
  for (auto* rfh : render_frame_->GetFramesInSubtree()) {
    if (rfh != render_frame_)
      frame_hosts.push_back(rfh);
  }
  return frame_hosts;
}

// static
gin::Handle<WebFrame> WebFrame::From(v8::Isolate* isolate,
                                     content::RenderFrameHost* rfh) {
  auto frame_map = g_render_frame_map.Get();
  auto iter = frame_map.find(rfh);
  WebFrame* web_frame =
      iter == frame_map.end() ? new WebFrame(rfh) : iter->second;
  auto handle = gin::CreateHandle(isolate, web_frame);
  return handle;
}

// static
gin::Handle<WebFrame> WebFrame::FromID(v8::Isolate* isolate,
                                       int render_process_id,
                                       int render_frame_id) {
  auto* rfh =
      content::RenderFrameHost::FromID(render_process_id, render_frame_id);
  return From(isolate, rfh);
}

// static
void WebFrame::RenderFrameDeleted(content::RenderFrameHost* rfh) {
  auto frame_map = g_render_frame_map.Get();
  auto iter = frame_map.find(rfh);
  if (iter == frame_map.end())
    return;
  WebFrame* web_frame = iter->second;
  web_frame->ReleaseRenderFrame();
}

gin::ObjectTemplateBuilder WebFrame::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin::Wrappable<WebFrame>::GetObjectTemplateBuilder(isolate)
      .SetMethod("executeJavaScript", &WebFrame::ExecuteJavaScript)
      .SetMethod("reload", &WebFrame::Reload)
      .SetProperty("frameTreeNodeId", &WebFrame::GetFrameTreeNodeID)
      .SetProperty("routingId", &WebFrame::GetRoutingID)
      .SetProperty("url", &WebFrame::GetURL)
      .SetProperty("top", &WebFrame::GetTop)
      .SetProperty("parent", &WebFrame::GetParent)
      .SetProperty("frames", &WebFrame::GetChildren);
}

const char* WebFrame::GetTypeName() {
  return "WebFrame";
}

}  // namespace api

}  // namespace electron

namespace {

using electron::api::WebFrame;

v8::Local<v8::Value> FromID(gin_helper::ErrorThrower thrower,
                            int render_process_id,
                            int render_frame_id) {
  if (!electron::Browser::Get()->is_ready()) {
    thrower.ThrowError("WebFrame can only be received when app is ready");
    return v8::Null(thrower.isolate());
  }

  return WebFrame::FromID(thrower.isolate(), render_process_id, render_frame_id)
      .ToV8();
}

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary dict(isolate, exports);
  dict.SetMethod("fromId", &FromID);
}

}  // namespace

NODE_LINKED_MODULE_CONTEXT_AWARE(electron_browser_web_frame_main, Initialize)
