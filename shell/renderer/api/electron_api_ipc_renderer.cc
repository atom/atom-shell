// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <string>

#include "base/values.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_frame_observer.h"
#include "gin/dictionary.h"
#include "gin/handle.h"
#include "gin/object_template_builder.h"
#include "gin/wrappable.h"
#include "services/service_manager/public/cpp/interface_provider.h"
#include "shell/common/api/api.mojom.h"
#include "shell/common/gin_converters/blink_converter.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/error_thrower.h"
#include "shell/common/gin_helper/function_template_extensions.h"
#include "shell/common/gin_helper/promise.h"
#include "shell/common/node_bindings.h"
#include "shell/common/node_includes.h"
#include "shell/common/v8_value_serializer.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_message_port_converter.h"

using blink::WebLocalFrame;
using content::RenderFrame;

namespace {

const char kIPCMethodCalledAfterContextReleasedError[] =
    "IPC method called after context was released";

RenderFrame* GetCurrentRenderFrame() {
  WebLocalFrame* frame = WebLocalFrame::FrameForCurrentContext();
  if (!frame)
    return nullptr;

  return RenderFrame::FromWebFrame(frame);
}

class IPCRenderer : public gin::Wrappable<IPCRenderer>,
                    private content::RenderFrameObserver {
 public:
  static gin::WrapperInfo kWrapperInfo;

  static gin::Handle<IPCRenderer> Create(v8::Isolate* isolate) {
    return gin::CreateHandle(isolate, new IPCRenderer(isolate));
  }

  explicit IPCRenderer(v8::Isolate* isolate)
      : content::RenderFrameObserver(GetCurrentRenderFrame()) {
    RenderFrame* render_frame = GetCurrentRenderFrame();
    DCHECK(render_frame);
    weak_context_ =
        v8::Global<v8::Context>(isolate, isolate->GetCurrentContext());
    weak_context_.SetWeak();

    render_frame->GetRemoteAssociatedInterfaces()->GetInterface(
        &electron_ipc_remote_);
  }

  void OnDestruct() override { electron_ipc_remote_.reset(); }

  void WillReleaseScriptContext(v8::Local<v8::Context> context,
                                int32_t world_id) override {
    if (weak_context_.IsEmpty() ||
        weak_context_.Get(context->GetIsolate()) == context)
      electron_ipc_remote_.reset();
  }

  // gin::Wrappable:
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override {
    return gin::Wrappable<IPCRenderer>::GetObjectTemplateBuilder(isolate)
        .SetMethod("send", &IPCRenderer::SendMessage)
        .SetMethod("sendSync", &IPCRenderer::SendSync)
        .SetMethod("sendToHost", &IPCRenderer::SendToHost)
        .SetMethod("invoke", &IPCRenderer::Invoke)
        .SetMethod("postMessage", &IPCRenderer::PostMessage);
  }

  const char* GetTypeName() override { return "IPCRenderer"; }

 private:
  void SendMessage(v8::Isolate* isolate,
                   gin_helper::ErrorThrower thrower,
                   bool internal,
                   const std::string& channel,
                   v8::Local<v8::Value> arguments) {
    if (!electron_ipc_remote_) {
      thrower.ThrowError(kIPCMethodCalledAfterContextReleasedError);
      return;
    }
    blink::CloneableMessage message;
    if (!electron::SerializeV8Value(isolate, arguments, &message)) {
      return;
    }
    electron_ipc_remote_->Message(internal, channel, std::move(message));
  }

  v8::Local<v8::Promise> Invoke(v8::Isolate* isolate,
                                gin_helper::ErrorThrower thrower,
                                bool internal,
                                const std::string& channel,
                                v8::Local<v8::Value> arguments) {
    if (!electron_ipc_remote_) {
      thrower.ThrowError(kIPCMethodCalledAfterContextReleasedError);
      return v8::Local<v8::Promise>();
    }
    blink::CloneableMessage message;
    if (!electron::SerializeV8Value(isolate, arguments, &message)) {
      return v8::Local<v8::Promise>();
    }
    gin_helper::Promise<blink::CloneableMessage> p(isolate);
    auto handle = p.GetHandle();

    electron_ipc_remote_->Invoke(
        internal, channel, std::move(message),
        base::BindOnce(
            [](gin_helper::Promise<blink::CloneableMessage> p,
               blink::CloneableMessage result) { p.Resolve(result); },
            std::move(p)));

    return handle;
  }

  void PostMessage(v8::Isolate* isolate,
                   gin_helper::ErrorThrower thrower,
                   const std::string& channel,
                   v8::Local<v8::Value> message_value,
                   std::optional<v8::Local<v8::Value>> transfer) {
    if (!electron_ipc_remote_) {
      thrower.ThrowError(kIPCMethodCalledAfterContextReleasedError);
      return;
    }
    blink::TransferableMessage transferable_message;
    if (!electron::SerializeV8Value(isolate, message_value,
                                    &transferable_message)) {
      // SerializeV8Value sets an exception.
      return;
    }

    std::vector<v8::Local<v8::Object>> transferables;
    if (transfer && !transfer.value()->IsUndefined()) {
      if (!gin::ConvertFromV8(isolate, *transfer, &transferables)) {
        thrower.ThrowTypeError("Invalid value for transfer");
        return;
      }
    }

    std::vector<blink::MessagePortChannel> ports;
    for (auto& transferable : transferables) {
      std::optional<blink::MessagePortChannel> port =
          blink::WebMessagePortConverter::
              DisentangleAndExtractMessagePortChannel(isolate, transferable);
      if (!port.has_value()) {
        thrower.ThrowTypeError("Invalid value for transfer");
        return;
      }
      ports.emplace_back(port.value());
    }

    transferable_message.ports = std::move(ports);
    electron_ipc_remote_->ReceivePostMessage(channel,
                                             std::move(transferable_message));
  }

  void SendToHost(v8::Isolate* isolate,
                  gin_helper::ErrorThrower thrower,
                  const std::string& channel,
                  v8::Local<v8::Value> arguments) {
    if (!electron_ipc_remote_) {
      thrower.ThrowError(kIPCMethodCalledAfterContextReleasedError);
      return;
    }
    blink::CloneableMessage message;
    if (!electron::SerializeV8Value(isolate, arguments, &message)) {
      return;
    }
    electron_ipc_remote_->MessageHost(channel, std::move(message));
  }

  v8::Local<v8::Value> SendSync(v8::Isolate* isolate,
                                gin_helper::ErrorThrower thrower,
                                bool internal,
                                const std::string& channel,
                                v8::Local<v8::Value> arguments) {
    if (!electron_ipc_remote_) {
      thrower.ThrowError(kIPCMethodCalledAfterContextReleasedError);
      return v8::Local<v8::Value>();
    }
    blink::CloneableMessage message;
    if (!electron::SerializeV8Value(isolate, arguments, &message)) {
      return v8::Local<v8::Value>();
    }

    blink::CloneableMessage result;
    electron_ipc_remote_->MessageSync(internal, channel, std::move(message),
                                      &result);
    return electron::DeserializeV8Value(isolate, result);
  }

  v8::Global<v8::Context> weak_context_;
  mojo::AssociatedRemote<electron::mojom::ElectronApiIPC> electron_ipc_remote_;
};

gin::WrapperInfo IPCRenderer::kWrapperInfo = {gin::kEmbedderNativeGin};

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  gin::Dictionary dict(context->GetIsolate(), exports);
  dict.Set("ipc", IPCRenderer::Create(context->GetIsolate()));
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_renderer_ipc, Initialize)
