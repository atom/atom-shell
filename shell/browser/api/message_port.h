// Copyright (c) 2020 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_MESSAGE_PORT_H_
#define ELECTRON_SHELL_BROWSER_API_MESSAGE_PORT_H_

#include <memory>
#include <vector>

#include "gin/wrappable.h"
#include "mojo/public/cpp/bindings/connector.h"
#include "mojo/public/cpp/bindings/message.h"
#include "shell/common/gin_helper/cleaned_up_at_exit.h"
#include "third_party/blink/public/common/messaging/message_port_channel.h"
#include "third_party/blink/public/common/messaging/message_port_descriptor.h"

namespace gin {
class Arguments;
template <typename T>
class Handle;
}  // namespace gin

namespace electron {

// A non-blink version of blink::MessagePort.
class MessagePort : public gin::Wrappable<MessagePort>,
                    public gin_helper::CleanedUpAtExit,
                    public mojo::MessageReceiver {
 public:
  ~MessagePort() override;
  static gin::Handle<MessagePort> Create(v8::Isolate* isolate);

  void PostMessage(gin::Arguments* args);
  void Start();
  void Close();

  void Entangle(blink::MessagePortDescriptor port);
  void Entangle(blink::MessagePortChannel channel);

  blink::MessagePortChannel Disentangle();

  bool IsEntangled() const { return !closed_ && !IsNeutered(); }
  bool IsNeutered() const { return !connector_ || !connector_->is_valid(); }

  static std::vector<gin::Handle<MessagePort>> EntanglePorts(
      v8::Isolate* isolate,
      std::vector<blink::MessagePortChannel> channels);

  static std::vector<blink::MessagePortChannel> DisentanglePorts(
      v8::Isolate* isolate,
      const std::vector<gin::Handle<MessagePort>>& ports,
      bool* threw_exception);

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

 private:
  MessagePort();

  // The blink version of MessagePort uses the very nice "ActiveScriptWrapper"
  // class, which keeps the object alive through the V8 embedder hooks into the
  // GC lifecycle: see
  // https://source.chromium.org/chromium/chromium/src/+/main:third_party/blink/renderer/platform/heap/thread_state.cc;l=258;drc=b892cf58e162a8f66cd76d7472f129fe0fb6a7d1
  // We do not have that luxury, so we brutishly use v8::Global to accomplish
  // something similar. Critically, whenever the value of
  // "HasPendingActivity()" changes, we must call Pin() or Unpin() as
  // appropriate.
  bool HasPendingActivity() const;
  void Pin();
  void Unpin();

  // mojo::MessageReceiver
  bool Accept(mojo::Message* mojo_message) override;

  std::unique_ptr<mojo::Connector> connector_;
  bool started_ = false;
  bool closed_ = false;

  v8::Global<v8::Value> pinned_;

  // The internal port owned by this class. The handle itself is moved into the
  // |connector_| while entangled.
  blink::MessagePortDescriptor port_;

  base::WeakPtrFactory<MessagePort> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_API_MESSAGE_PORT_H_
