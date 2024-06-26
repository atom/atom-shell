// Copyright (c) 2020 Samuel Maddock <sam@samuelmaddock.com>.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_web_frame_main.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/no_destructor.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"  // nogncheck
#include "content/public/browser/render_frame_host.h"
#include "content/public/common/isolated_world_ids.h"
#include "electron/shell/common/api/api.mojom.h"
#include "gin/object_template_builder.h"
#include "services/service_manager/public/cpp/interface_provider.h"
#include "shell/browser/api/message_port.h"
#include "shell/browser/browser.h"
#include "shell/browser/javascript_environment.h"
#include "shell/common/gin_converters/blink_converter.h"
#include "shell/common/gin_converters/frame_converter.h"
#include "shell/common/gin_converters/gurl_converter.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/error_thrower.h"
#include "shell/common/gin_helper/object_template_builder.h"
#include "shell/common/gin_helper/promise.h"
#include "shell/common/node_includes.h"
#include "shell/common/v8_value_serializer.h"

namespace gin {

template <>
struct Converter<blink::mojom::PageVisibilityState> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   blink::mojom::PageVisibilityState val) {
    std::string visibility;
    switch (val) {
      case blink::mojom::PageVisibilityState::kVisible:
        visibility = "visible";
        break;
      case blink::mojom::PageVisibilityState::kHidden:
      case blink::mojom::PageVisibilityState::kHiddenButPainting:
        visibility = "hidden";
        break;
    }
    return gin::ConvertToV8(isolate, visibility);
  }
};

}  // namespace gin

namespace electron::api {

typedef std::unordered_map<int, WebFrameMain*> WebFrameMainIdMap;

WebFrameMainIdMap& GetWebFrameMainMap() {
  static base::NoDestructor<WebFrameMainIdMap> instance;
  return *instance;
}

// static
WebFrameMain* WebFrameMain::FromFrameTreeNodeId(int frame_tree_node_id) {
  WebFrameMainIdMap& frame_map = GetWebFrameMainMap();
  auto iter = frame_map.find(frame_tree_node_id);
  auto* web_frame = iter == frame_map.end() ? nullptr : iter->second;
  return web_frame;
}

// static
WebFrameMain* WebFrameMain::FromRenderFrameHost(content::RenderFrameHost* rfh) {
  if (!rfh)
    return nullptr;

  // TODO(codebytere): remove after refactoring away from FrameTreeNodeId as map
  // key.
  auto* ftn =
      static_cast<content::RenderFrameHostImpl*>(rfh)->frame_tree_node();
  if (!ftn)
    return nullptr;

  return FromFrameTreeNodeId(rfh->GetFrameTreeNodeId());
}

gin::WrapperInfo WebFrameMain::kWrapperInfo = {gin::kEmbedderNativeGin};

WebFrameMain::WebFrameMain(content::RenderFrameHost* rfh)
    : frame_tree_node_id_(rfh->GetFrameTreeNodeId()), render_frame_(rfh) {
  GetWebFrameMainMap().emplace(frame_tree_node_id_, this);
}

WebFrameMain::~WebFrameMain() {
  Destroyed();
}

void WebFrameMain::Destroyed() {
  MarkRenderFrameDisposed();
  GetWebFrameMainMap().erase(frame_tree_node_id_);
  Unpin();
}

void WebFrameMain::MarkRenderFrameDisposed() {
  render_frame_ = nullptr;
  render_frame_disposed_ = true;
  TeardownMojoConnection();
}

void WebFrameMain::UpdateRenderFrameHost(content::RenderFrameHost* rfh) {
  // Should only be called when swapping frames.
  render_frame_disposed_ = false;
  render_frame_ = rfh;
  TeardownMojoConnection();
  MaybeSetupMojoConnection();
}

bool WebFrameMain::CheckRenderFrame() const {
  if (render_frame_disposed_) {
    v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    v8::HandleScope scope(isolate);
    gin_helper::ErrorThrower(isolate).ThrowError(
        "Render frame was disposed before WebFrameMain could be accessed");
    return false;
  }
  return true;
}

v8::Local<v8::Promise> WebFrameMain::ExecuteJavaScript(
    gin::Arguments* args,
    const std::u16string& code) {
  gin_helper::Promise<base::Value> promise(args->isolate());
  v8::Local<v8::Promise> handle = promise.GetHandle();

  // Optional userGesture parameter
  bool user_gesture;
  if (!args->PeekNext().IsEmpty()) {
    if (args->PeekNext()->IsBoolean()) {
      args->GetNext(&user_gesture);
    } else {
      args->ThrowTypeError("userGesture must be a boolean");
      return handle;
    }
  } else {
    user_gesture = false;
  }

  if (render_frame_disposed_) {
    promise.RejectWithErrorMessage(
        "Render frame was disposed before WebFrameMain could be accessed");
    return handle;
  }

  static_cast<content::RenderFrameHostImpl*>(render_frame_)
      ->ExecuteJavaScriptForTests(
          code, user_gesture, true /* resolve_promises */,
          content::ISOLATED_WORLD_ID_GLOBAL,
          base::BindOnce(
              [](gin_helper::Promise<base::Value> promise,
                 blink::mojom::JavaScriptExecutionResultType type,
                 base::Value value) {
                if (type ==
                    blink::mojom::JavaScriptExecutionResultType::kSuccess) {
                  promise.Resolve(value);
                } else {
                  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
                  v8::HandleScope scope(isolate);
                  promise.Reject(gin::ConvertToV8(isolate, value));
                }
              },
              std::move(promise)));

  return handle;
}

bool WebFrameMain::Reload() {
  if (!CheckRenderFrame())
    return false;
  return render_frame_->Reload();
}

void WebFrameMain::Send(v8::Isolate* isolate,
                        bool internal,
                        const std::string& channel,
                        v8::Local<v8::Value> args) {
  blink::CloneableMessage message;
  if (!gin::ConvertFromV8(isolate, args, &message)) {
    isolate->ThrowException(v8::Exception::Error(
        gin::StringToV8(isolate, "Failed to serialize arguments")));
    return;
  }

  if (!CheckRenderFrame())
    return;

  GetRendererApi()->Message(internal, channel, std::move(message));
}

const mojo::Remote<mojom::ElectronRenderer>& WebFrameMain::GetRendererApi() {
  MaybeSetupMojoConnection();
  return renderer_api_;
}

void WebFrameMain::MaybeSetupMojoConnection() {
  if (render_frame_disposed_) {
    // RFH may not be set yet if called between when a new RFH is created and
    // before it's been swapped with an old RFH.
    LOG(INFO) << "Attempt to setup WebFrameMain connection while render frame "
                 "is disposed";
    return;
  }

  if (!renderer_api_) {
    pending_receiver_ = renderer_api_.BindNewPipeAndPassReceiver();
    renderer_api_.set_disconnect_handler(base::BindOnce(
        &WebFrameMain::OnRendererConnectionError, weak_factory_.GetWeakPtr()));
  }

  DCHECK(render_frame_);

  // Wait for RenderFrame to be created in renderer before accessing remote.
  if (pending_receiver_ && render_frame_ &&
      render_frame_->IsRenderFrameLive()) {
    render_frame_->GetRemoteInterfaces()->GetInterface(
        std::move(pending_receiver_));
  }
}

void WebFrameMain::TeardownMojoConnection() {
  renderer_api_.reset();
  pending_receiver_.reset();
}

void WebFrameMain::OnRendererConnectionError() {
  TeardownMojoConnection();
}

void WebFrameMain::PostMessage(v8::Isolate* isolate,
                               const std::string& channel,
                               v8::Local<v8::Value> message_value,
                               std::optional<v8::Local<v8::Value>> transfer) {
  blink::TransferableMessage transferable_message;
  if (!electron::SerializeV8Value(isolate, message_value,
                                  &transferable_message)) {
    // SerializeV8Value sets an exception.
    return;
  }

  std::vector<gin::Handle<MessagePort>> wrapped_ports;
  if (transfer && !transfer.value()->IsUndefined()) {
    if (!gin::ConvertFromV8(isolate, *transfer, &wrapped_ports)) {
      isolate->ThrowException(v8::Exception::Error(
          gin::StringToV8(isolate, "Invalid value for transfer")));
      return;
    }
  }

  bool threw_exception = false;
  transferable_message.ports =
      MessagePort::DisentanglePorts(isolate, wrapped_ports, &threw_exception);
  if (threw_exception)
    return;

  if (!CheckRenderFrame())
    return;

  GetRendererApi()->ReceivePostMessage(channel,
                                       std::move(transferable_message));
}

int WebFrameMain::FrameTreeNodeID() const {
  return frame_tree_node_id_;
}

std::string WebFrameMain::Name() const {
  if (!CheckRenderFrame())
    return std::string();
  return render_frame_->GetFrameName();
}

base::ProcessId WebFrameMain::OSProcessID() const {
  if (!CheckRenderFrame())
    return -1;
  base::ProcessHandle process_handle =
      render_frame_->GetProcess()->GetProcess().Handle();
  return base::GetProcId(process_handle);
}

int WebFrameMain::ProcessID() const {
  if (!CheckRenderFrame())
    return -1;
  return render_frame_->GetProcess()->GetID();
}

int WebFrameMain::RoutingID() const {
  if (!CheckRenderFrame())
    return -1;
  return render_frame_->GetRoutingID();
}

GURL WebFrameMain::URL() const {
  if (!CheckRenderFrame())
    return GURL::EmptyGURL();
  return render_frame_->GetLastCommittedURL();
}

std::string WebFrameMain::Origin() const {
  if (!CheckRenderFrame())
    return std::string();
  return render_frame_->GetLastCommittedOrigin().Serialize();
}

blink::mojom::PageVisibilityState WebFrameMain::VisibilityState() const {
  if (!CheckRenderFrame())
    return blink::mojom::PageVisibilityState::kHidden;
  return render_frame_->GetVisibilityState();
}

content::RenderFrameHost* WebFrameMain::Top() const {
  if (!CheckRenderFrame())
    return nullptr;
  return render_frame_->GetMainFrame();
}

content::RenderFrameHost* WebFrameMain::Parent() const {
  if (!CheckRenderFrame())
    return nullptr;
  return render_frame_->GetParent();
}

std::vector<content::RenderFrameHost*> WebFrameMain::Frames() const {
  std::vector<content::RenderFrameHost*> frame_hosts;
  if (!CheckRenderFrame())
    return frame_hosts;

  render_frame_->ForEachRenderFrameHost(
      [&frame_hosts, this](content::RenderFrameHost* rfh) {
        if (rfh->GetParent() == render_frame_)
          frame_hosts.push_back(rfh);
      });

  return frame_hosts;
}

std::vector<content::RenderFrameHost*> WebFrameMain::FramesInSubtree() const {
  std::vector<content::RenderFrameHost*> frame_hosts;
  if (!CheckRenderFrame())
    return frame_hosts;

  render_frame_->ForEachRenderFrameHost(
      [&frame_hosts](content::RenderFrameHost* rfh) {
        frame_hosts.push_back(rfh);
      });

  return frame_hosts;
}

void WebFrameMain::DOMContentLoaded() {
  Emit("dom-ready");
}

// static
gin::Handle<WebFrameMain> WebFrameMain::New(v8::Isolate* isolate) {
  return gin::Handle<WebFrameMain>();
}

// static
gin::Handle<WebFrameMain> WebFrameMain::From(v8::Isolate* isolate,
                                             content::RenderFrameHost* rfh) {
  if (!rfh)
    return gin::Handle<WebFrameMain>();

  auto* web_frame = FromRenderFrameHost(rfh);
  if (web_frame)
    return gin::CreateHandle(isolate, web_frame);

  auto handle = gin::CreateHandle(isolate, new WebFrameMain(rfh));

  // Prevent garbage collection of frame until it has been deleted internally.
  handle->Pin(isolate);

  return handle;
}

// static
gin::Handle<WebFrameMain> WebFrameMain::FromOrNull(
    v8::Isolate* isolate,
    content::RenderFrameHost* rfh) {
  if (!rfh)
    return gin::Handle<WebFrameMain>();

  auto* web_frame = FromRenderFrameHost(rfh);
  if (!web_frame)
    return gin::Handle<WebFrameMain>();

  return gin::CreateHandle(isolate, web_frame);
}

// static
void WebFrameMain::FillObjectTemplate(v8::Isolate* isolate,
                                      v8::Local<v8::ObjectTemplate> templ) {
  gin_helper::ObjectTemplateBuilder(isolate, templ)
      .SetMethod("executeJavaScript", &WebFrameMain::ExecuteJavaScript)
      .SetMethod("reload", &WebFrameMain::Reload)
      .SetMethod("_send", &WebFrameMain::Send)
      .SetMethod("_postMessage", &WebFrameMain::PostMessage)
      .SetProperty("frameTreeNodeId", &WebFrameMain::FrameTreeNodeID)
      .SetProperty("name", &WebFrameMain::Name)
      .SetProperty("osProcessId", &WebFrameMain::OSProcessID)
      .SetProperty("processId", &WebFrameMain::ProcessID)
      .SetProperty("routingId", &WebFrameMain::RoutingID)
      .SetProperty("url", &WebFrameMain::URL)
      .SetProperty("origin", &WebFrameMain::Origin)
      .SetProperty("visibilityState", &WebFrameMain::VisibilityState)
      .SetProperty("top", &WebFrameMain::Top)
      .SetProperty("parent", &WebFrameMain::Parent)
      .SetProperty("frames", &WebFrameMain::Frames)
      .SetProperty("framesInSubtree", &WebFrameMain::FramesInSubtree)
      .Build();
}

const char* WebFrameMain::GetTypeName() {
  return GetClassName();
}

}  // namespace electron::api

namespace {

using electron::api::WebFrameMain;

v8::Local<v8::Value> FromID(gin_helper::ErrorThrower thrower,
                            int render_process_id,
                            int render_frame_id) {
  if (!electron::Browser::Get()->is_ready()) {
    thrower.ThrowError("WebFrameMain is available only after app ready");
    return v8::Null(thrower.isolate());
  }

  auto* rfh =
      content::RenderFrameHost::FromID(render_process_id, render_frame_id);

  return WebFrameMain::From(thrower.isolate(), rfh).ToV8();
}

v8::Local<v8::Value> FromIDOrNull(gin_helper::ErrorThrower thrower,
                                  int render_process_id,
                                  int render_frame_id) {
  if (!electron::Browser::Get()->is_ready()) {
    thrower.ThrowError("WebFrameMain is available only after app ready");
    return v8::Null(thrower.isolate());
  }

  auto* rfh =
      content::RenderFrameHost::FromID(render_process_id, render_frame_id);

  return WebFrameMain::FromOrNull(thrower.isolate(), rfh).ToV8();
}

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary dict(isolate, exports);
  dict.Set("WebFrameMain", WebFrameMain::GetConstructor(context));
  dict.SetMethod("fromId", &FromID);
  dict.SetMethod("fromIdOrNull", &FromIDOrNull);
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_browser_web_frame_main, Initialize)
