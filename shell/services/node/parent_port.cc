// Copyright (c) 2022 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/services/node/parent_port.h"

#include <utility>

#include "base/no_destructor.h"
#include "gin/data_object_builder.h"
#include "gin/handle.h"
#include "shell/browser/api/message_port.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/event_emitter_caller.h"
#include "shell/common/node_includes.h"
#include "shell/common/v8_value_serializer.h"
#include "third_party/blink/public/common/messaging/transferable_message_mojom_traits.h"

namespace electron {

gin::WrapperInfo ParentPort::kWrapperInfo = {gin::kEmbedderNativeGin};

ParentPort* ParentPort::GetInstance() {
  static base::NoDestructor<ParentPort> instance;
  return instance.get();
}

ParentPort::ParentPort() = default;
ParentPort::~ParentPort() = default;

void ParentPort::Initialize(blink::MessagePortDescriptor port) {
  port_ = std::move(port);
  connector_ = std::make_unique<mojo::Connector>(
      port_.TakeHandleToEntangleWithEmbedder(),
      mojo::Connector::SINGLE_THREADED_SEND,
      base::SingleThreadTaskRunner::GetCurrentDefault());
  connector_->PauseIncomingMethodCallProcessing();
  connector_->set_incoming_receiver(this);
  connector_->set_connection_error_handler(
      base::BindOnce(&ParentPort::Close, base::Unretained(this)));
}

void ParentPort::PostMessage(v8::Local<v8::Value> message_value) {
  if (!connector_closed_ && connector_ && connector_->is_valid()) {
    v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    blink::TransferableMessage transferable_message;

    if (!electron::SerializeV8Value(isolate, message_value,
                                    &transferable_message)) {
      // SerializeV8Value sets an exception.
      return;
    }

    mojo::Message mojo_message =
        blink::mojom::TransferableMessage::WrapAsMessage(
            std::move(transferable_message));
    connector_->Accept(&mojo_message);
  }
}

void ParentPort::Close() {
  if (!connector_closed_ && connector_->is_valid()) {
    port_.GiveDisentangledHandle(connector_->PassMessagePipe());
    connector_ = nullptr;
    port_.Reset();
    connector_closed_ = true;
  }
}

void ParentPort::Start() {
  if (!connector_closed_ && connector_ && connector_->is_valid()) {
    connector_->ResumeIncomingMethodCallProcessing();
  }
}

void ParentPort::Pause() {
  if (!connector_closed_ && connector_ && connector_->is_valid()) {
    connector_->PauseIncomingMethodCallProcessing();
  }
}

bool ParentPort::Accept(mojo::Message* mojo_message) {
  blink::TransferableMessage message;
  if (!blink::mojom::TransferableMessage::DeserializeFromMessage(
          std::move(*mojo_message), &message)) {
    return false;
  }

  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  auto wrapped_ports =
      MessagePort::EntanglePorts(isolate, std::move(message.ports));
  v8::Local<v8::Value> message_value =
      electron::DeserializeV8Value(isolate, message);
  v8::Local<v8::Object> self;
  if (!GetWrapper(isolate).ToLocal(&self))
    return false;
  auto event = gin::DataObjectBuilder(isolate)
                   .Set("data", message_value)
                   .Set("ports", wrapped_ports)
                   .Build();
  gin_helper::EmitEvent(isolate, self, "message", event);
  return true;
}

// static
gin::Handle<ParentPort> ParentPort::Create(v8::Isolate* isolate) {
  return gin::CreateHandle(isolate, ParentPort::GetInstance());
}

// static
gin::ObjectTemplateBuilder ParentPort::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin::Wrappable<ParentPort>::GetObjectTemplateBuilder(isolate)
      .SetMethod("postMessage", &ParentPort::PostMessage)
      .SetMethod("start", &ParentPort::Start)
      .SetMethod("pause", &ParentPort::Pause);
}

const char* ParentPort::GetTypeName() {
  return "ParentPort";
}

}  // namespace electron

namespace {

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary dict(isolate, exports);
  dict.SetMethod("createParentPort", &electron::ParentPort::Create);
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_utility_parent_port, Initialize)
