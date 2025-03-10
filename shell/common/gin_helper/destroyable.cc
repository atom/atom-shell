// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/gin_helper/destroyable.h"

#include "gin/converter.h"
#include "shell/common/gin_helper/wrappable_base.h"
#include "v8/include/v8-function.h"

namespace gin_helper {

namespace {

v8::Eternal<v8::FunctionTemplate> destroy_func_;

v8::Eternal<v8::FunctionTemplate> is_destroyed_func_;

void DestroyFunc(const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::Local<v8::Object> holder = info.This();
  if (Destroyable::IsDestroyed(holder))
    return;

  // TODO(zcbenz): gin_helper::Wrappable will be removed.
  delete static_cast<gin_helper::WrappableBase*>(
      holder->GetAlignedPointerFromInternalField(0));
  holder->SetAlignedPointerInInternalField(0, nullptr);
}

void IsDestroyedFunc(const v8::FunctionCallbackInfo<v8::Value>& info) {
  info.GetReturnValue().Set(gin::ConvertToV8(
      info.GetIsolate(), Destroyable::IsDestroyed(info.This())));
}

}  // namespace

// static
bool Destroyable::IsDestroyed(v8::Local<v8::Object> object) {
  // An object is considered destroyed if it has no internal pointer or its
  // internal has been destroyed.
  return object->InternalFieldCount() == 0 ||
         object->GetAlignedPointerFromInternalField(0) == nullptr;
}

// static
void Destroyable::MakeDestroyable(v8::Isolate* isolate,
                                  v8::Local<v8::FunctionTemplate> prototype) {
  // Cache the FunctionTemplate of "destroy" and "isDestroyed".
  if (destroy_func_.IsEmpty()) {
    auto templ = v8::FunctionTemplate::New(isolate, DestroyFunc);
    templ->RemovePrototype();
    destroy_func_.Set(isolate, templ);
    templ = v8::FunctionTemplate::New(isolate, IsDestroyedFunc);
    templ->RemovePrototype();
    is_destroyed_func_.Set(isolate, templ);
  }

  auto proto_templ = prototype->PrototypeTemplate();
  proto_templ->Set(gin::StringToSymbol(isolate, "destroy"),
                   destroy_func_.Get(isolate));
  proto_templ->Set(gin::StringToSymbol(isolate, "isDestroyed"),
                   is_destroyed_func_.Get(isolate));
}

}  // namespace gin_helper
