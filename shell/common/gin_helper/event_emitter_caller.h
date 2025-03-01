// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_COMMON_GIN_HELPER_EVENT_EMITTER_CALLER_H_
#define ELECTRON_SHELL_COMMON_GIN_HELPER_EVENT_EMITTER_CALLER_H_

#include <array>
#include <utility>

#include "base/containers/span.h"
#include "gin/converter.h"
#include "gin/wrappable.h"
#include "shell/common/gin_converters/std_converter.h"  // for ConvertToV8(iso, &&)

namespace gin_helper {

namespace internal {

v8::Local<v8::Value> CallMethodWithArgs(v8::Isolate* isolate,
                                        v8::Local<v8::Object> obj,
                                        const char* method,
                                        base::span<v8::Local<v8::Value>> args);

}  // namespace internal

// obj.emit(name, args...);
// The caller is responsible of allocating a HandleScope.
template <typename... Args>
v8::Local<v8::Value> EmitEvent(v8::Isolate* isolate,
                               v8::Local<v8::Object> obj,
                               const std::string_view name,
                               Args&&... args) {
  v8::EscapableHandleScope scope{isolate};
  std::array<v8::Local<v8::Value>, 1U + sizeof...(args)> converted_args = {
      gin::StringToV8(isolate, name),
      gin::ConvertToV8(isolate, std::forward<Args>(args))...,
  };
  return scope.Escape(
      internal::CallMethodWithArgs(isolate, obj, "emit", converted_args));
}

// obj.custom_emit(args...)
template <typename... Args>
v8::Local<v8::Value> CustomEmit(v8::Isolate* isolate,
                                v8::Local<v8::Object> object,
                                const char* custom_emit,
                                Args&&... args) {
  v8::EscapableHandleScope scope{isolate};
  std::array<v8::Local<v8::Value>, sizeof...(args)> converted_args = {
      gin::ConvertToV8(isolate, std::forward<Args>(args))...,
  };
  return scope.Escape(internal::CallMethodWithArgs(isolate, object, custom_emit,
                                                   converted_args));
}

template <typename T, typename... Args>
v8::Local<v8::Value> CallMethod(v8::Isolate* isolate,
                                gin::Wrappable<T>* object,
                                const char* method_name,
                                Args&&... args) {
  v8::EscapableHandleScope scope(isolate);
  v8::Local<v8::Object> v8_object;
  if (object->GetWrapper(isolate).ToLocal(&v8_object))
    return scope.Escape(CustomEmit(isolate, v8_object, method_name,
                                   std::forward<Args>(args)...));
  else
    return {};
}

template <typename T, typename... Args>
v8::Local<v8::Value> CallMethod(gin::Wrappable<T>* object,
                                const char* method_name,
                                Args&&... args) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  return CallMethod(isolate, object, method_name, std::forward<Args>(args)...);
}

}  // namespace gin_helper

#endif  // ELECTRON_SHELL_COMMON_GIN_HELPER_EVENT_EMITTER_CALLER_H_
