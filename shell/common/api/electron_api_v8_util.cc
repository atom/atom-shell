// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <iterator>
#include <utility>

#include "base/run_loop.h"
#include "electron/buildflags/buildflags.h"
#include "shell/common/api/electron_api_key_weak_map.h"
#include "shell/common/gin_converters/content_converter.h"
#include "shell/common/gin_converters/gurl_converter.h"
#include "shell/common/gin_converters/std_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/node_includes.h"
#include "url/origin.h"
#include "v8/include/v8-profiler.h"

namespace gin {

template <typename Type1, typename Type2>
struct Converter<std::pair<Type1, Type2>> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     std::pair<Type1, Type2>* out) {
    if (!val->IsArray())
      return false;

    v8::Local<v8::Array> array = val.As<v8::Array>();
    if (array->Length() != 2)
      return false;

    auto context = isolate->GetCurrentContext();
    return Converter<Type1>::FromV8(
               isolate, array->Get(context, 0).ToLocalChecked(), &out->first) &&
           Converter<Type2>::FromV8(
               isolate, array->Get(context, 1).ToLocalChecked(), &out->second);
  }
};

}  // namespace gin

namespace {

v8::Local<v8::Value> GetHiddenValue(v8::Isolate* isolate,
                                    v8::Local<v8::Object> object,
                                    v8::Local<v8::String> key) {
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Private> privateKey = v8::Private::ForApi(isolate, key);
  v8::Local<v8::Value> value;
  v8::Maybe<bool> result = object->HasPrivate(context, privateKey);
  if (!(result.IsJust() && result.FromJust()))
    return v8::Local<v8::Value>();
  if (object->GetPrivate(context, privateKey).ToLocal(&value))
    return value;
  return v8::Local<v8::Value>();
}

void SetHiddenValue(v8::Isolate* isolate,
                    v8::Local<v8::Object> object,
                    v8::Local<v8::String> key,
                    v8::Local<v8::Value> value) {
  if (value.IsEmpty())
    return;
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Private> privateKey = v8::Private::ForApi(isolate, key);
  object->SetPrivate(context, privateKey, value);
}

void DeleteHiddenValue(v8::Isolate* isolate,
                       v8::Local<v8::Object> object,
                       v8::Local<v8::String> key) {
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Private> privateKey = v8::Private::ForApi(isolate, key);
  // Actually deleting the value would make force the object into
  // dictionary mode which is unnecessarily slow. Instead, we replace
  // the hidden value with "undefined".
  object->SetPrivate(context, privateKey, v8::Undefined(isolate));
}

int32_t GetObjectHash(v8::Local<v8::Object> object) {
  return object->GetIdentityHash();
}

void TakeHeapSnapshot(v8::Isolate* isolate) {
  isolate->GetHeapProfiler()->TakeHeapSnapshot();
}

void RequestGarbageCollectionForTesting(v8::Isolate* isolate) {
  isolate->RequestGarbageCollectionForTesting(
      v8::Isolate::GarbageCollectionType::kFullGarbageCollection);
}

// This causes a fatal error by creating a circular extension dependency.
void TriggerFatalErrorForTesting(v8::Isolate* isolate) {
  static const char* aDeps[] = {"B"};
  v8::RegisterExtension(
      std::make_unique<v8::Extension>("A", "", std::size(aDeps), aDeps));
  static const char* bDeps[] = {"A"};
  v8::RegisterExtension(
      std::make_unique<v8::Extension>("B", "", std::size(aDeps), bDeps));
  v8::ExtensionConfiguration config(1, bDeps);
  v8::Context::New(isolate, &config);
}

void RunUntilIdle() {
  base::RunLoop().RunUntilIdle();
}

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  gin_helper::Dictionary dict(context->GetIsolate(), exports);
  dict.SetMethod("getHiddenValue", &GetHiddenValue);
  dict.SetMethod("setHiddenValue", &SetHiddenValue);
  dict.SetMethod("deleteHiddenValue", &DeleteHiddenValue);
  dict.SetMethod("getObjectHash", &GetObjectHash);
  dict.SetMethod("takeHeapSnapshot", &TakeHeapSnapshot);
  dict.SetMethod("requestGarbageCollectionForTesting",
                 &RequestGarbageCollectionForTesting);
  dict.SetMethod("triggerFatalErrorForTesting", &TriggerFatalErrorForTesting);
  dict.SetMethod("runUntilIdle", &RunUntilIdle);
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_common_v8_util, Initialize)
