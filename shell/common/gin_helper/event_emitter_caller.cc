// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/gin_helper/event_emitter_caller.h"

#include "shell/common/gin_helper/microtasks_scope.h"
#include "shell/common/node_includes.h"

namespace gin_helper::internal {

v8::Local<v8::Value> CallMethodWithArgs(v8::Isolate* isolate,
                                        v8::Local<v8::Object> obj,
                                        const char* method,
                                        ValueVector* args) {
  // An active node::Environment is required for node::MakeCallback.
  std::unique_ptr<node::CallbackScope> callback_scope;
  if (node::Environment::GetCurrent(isolate)) {
    v8::HandleScope handle_scope(isolate);
    callback_scope = std::make_unique<node::CallbackScope>(
        isolate, v8::Object::New(isolate), node::async_context{0, 0});
  } else {
    return v8::Boolean::New(isolate, false);
  }

  // Perform microtask checkpoint after running JavaScript.
  gin_helper::MicrotasksScope microtasks_scope(
      isolate, obj->GetCreationContextChecked()->GetMicrotaskQueue(), true);

  // node::MakeCallback will also run pending tasks in Node.js.
  v8::MaybeLocal<v8::Value> ret = node::MakeCallback(
      isolate, obj, method, args->size(), args->data(), {0, 0});

  // If the JS function throws an exception (doesn't return a value) the result
  // of MakeCallback will be empty and therefore ToLocal will be false, in this
  // case we need to return "false" as that indicates that the event emitter did
  // not handle the event
  v8::Local<v8::Value> localRet;
  if (ret.ToLocal(&localRet))
    return localRet;

  return v8::Boolean::New(isolate, false);
}

}  // namespace gin_helper::internal
