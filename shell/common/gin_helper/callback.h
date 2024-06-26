// Copyright (c) 2019 GitHub, Inc. All rights reserved.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_COMMON_GIN_HELPER_CALLBACK_H_
#define ELECTRON_SHELL_COMMON_GIN_HELPER_CALLBACK_H_

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "gin/dictionary.h"
#include "shell/common/gin_converters/std_converter.h"
#include "shell/common/gin_helper/function_template.h"
#include "shell/common/gin_helper/locker.h"
#include "shell/common/gin_helper/microtasks_scope.h"
// Implements safe conversions between JS functions and base::RepeatingCallback.

namespace gin_helper {

template <typename T>
class RefCountedGlobal;

// Manages the V8 function with RAII.
class SafeV8Function {
 public:
  SafeV8Function(v8::Isolate* isolate, v8::Local<v8::Value> value);
  SafeV8Function(const SafeV8Function& other);
  ~SafeV8Function();

  bool IsAlive() const;
  v8::Local<v8::Function> NewHandle(v8::Isolate* isolate) const;

 private:
  scoped_refptr<RefCountedGlobal<v8::Function>> v8_function_;
};

// Helper to invoke a V8 function with C++ parameters.
template <typename Sig>
struct V8FunctionInvoker {};

template <typename... ArgTypes>
struct V8FunctionInvoker<v8::Local<v8::Value>(ArgTypes...)> {
  static v8::Local<v8::Value> Go(v8::Isolate* isolate,
                                 const SafeV8Function& function,
                                 ArgTypes... raw) {
    gin_helper::Locker locker(isolate);
    v8::EscapableHandleScope handle_scope(isolate);
    if (!function.IsAlive())
      return v8::Null(isolate);
    v8::Local<v8::Function> holder = function.NewHandle(isolate);
    v8::Local<v8::Context> context = holder->GetCreationContextChecked();
    gin_helper::MicrotasksScope microtasks_scope(
        isolate, context->GetMicrotaskQueue(), true);
    v8::Context::Scope context_scope(context);
    std::vector<v8::Local<v8::Value>> args{
        gin::ConvertToV8(isolate, std::forward<ArgTypes>(raw))...};
    v8::MaybeLocal<v8::Value> ret = holder->Call(
        context, holder, args.size(), args.empty() ? nullptr : &args.front());
    if (ret.IsEmpty())
      return v8::Undefined(isolate);
    else
      return handle_scope.Escape(ret.ToLocalChecked());
  }
};

template <typename... ArgTypes>
struct V8FunctionInvoker<void(ArgTypes...)> {
  static void Go(v8::Isolate* isolate,
                 const SafeV8Function& function,
                 ArgTypes... raw) {
    gin_helper::Locker locker(isolate);
    v8::HandleScope handle_scope(isolate);
    if (!function.IsAlive())
      return;
    v8::Local<v8::Function> holder = function.NewHandle(isolate);
    v8::Local<v8::Context> context = holder->GetCreationContextChecked();
    gin_helper::MicrotasksScope microtasks_scope(
        isolate, context->GetMicrotaskQueue(), true);
    v8::Context::Scope context_scope(context);
    std::vector<v8::Local<v8::Value>> args{
        gin::ConvertToV8(isolate, std::forward<ArgTypes>(raw))...};
    holder
        ->Call(context, holder, args.size(),
               args.empty() ? nullptr : &args.front())
        .IsEmpty();
  }
};

template <typename ReturnType, typename... ArgTypes>
struct V8FunctionInvoker<ReturnType(ArgTypes...)> {
  static ReturnType Go(v8::Isolate* isolate,
                       const SafeV8Function& function,
                       ArgTypes... raw) {
    gin_helper::Locker locker(isolate);
    v8::HandleScope handle_scope(isolate);
    ReturnType ret = ReturnType();
    if (!function.IsAlive())
      return ret;
    v8::Local<v8::Function> holder = function.NewHandle(isolate);
    v8::Local<v8::Context> context = holder->GetCreationContextChecked();
    gin_helper::MicrotasksScope microtasks_scope(
        isolate, context->GetMicrotaskQueue(), true);
    v8::Context::Scope context_scope(context);
    std::vector<v8::Local<v8::Value>> args{
        gin::ConvertToV8(isolate, std::forward<ArgTypes>(raw))...};
    v8::Local<v8::Value> result;
    auto maybe_result = holder->Call(context, holder, args.size(),
                                     args.empty() ? nullptr : &args.front());
    if (maybe_result.ToLocal(&result))
      gin::Converter<ReturnType>::FromV8(isolate, result, &ret);
    return ret;
  }
};

// Helper to pass a C++ function to JavaScript.
using Translater = base::RepeatingCallback<void(gin::Arguments* args)>;
v8::Local<v8::Value> CreateFunctionFromTranslater(v8::Isolate* isolate,
                                                  const Translater& translater,
                                                  bool one_time);
v8::Local<v8::Value> BindFunctionWith(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      v8::Local<v8::Function> func,
                                      v8::Local<v8::Value> arg1,
                                      v8::Local<v8::Value> arg2);

// Calls callback with Arguments.
template <typename Sig>
struct NativeFunctionInvoker {};

template <typename ReturnType, typename... ArgTypes>
struct NativeFunctionInvoker<ReturnType(ArgTypes...)> {
  static void Go(base::RepeatingCallback<ReturnType(ArgTypes...)> val,
                 gin::Arguments* args) {
    using Indices = std::index_sequence_for<ArgTypes...>;
    Invoker<Indices, ArgTypes...> invoker(args,
                                          {.holder_is_first_argument = false});
    if (invoker.IsOK())
      invoker.DispatchToCallback(val);
  }
};

// Convert a callback to V8 without the call number limitation, this can easily
// cause memory leaks so use it with caution.
template <typename Sig>
v8::Local<v8::Value> CallbackToV8Leaked(
    v8::Isolate* isolate,
    const base::RepeatingCallback<Sig>& val) {
  Translater translater =
      base::BindRepeating(&NativeFunctionInvoker<Sig>::Go, val);
  return CreateFunctionFromTranslater(isolate, translater, false);
}

}  // namespace gin_helper

#endif  // ELECTRON_SHELL_COMMON_GIN_HELPER_CALLBACK_H_
