// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.

#ifndef ELECTRON_SHELL_COMMON_GIN_HELPER_FUNCTION_TEMPLATE_H_
#define ELECTRON_SHELL_COMMON_GIN_HELPER_FUNCTION_TEMPLATE_H_

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "gin/arguments.h"
#include "gin/per_isolate_data.h"
#include "shell/common/gin_helper/arguments.h"
#include "shell/common/gin_helper/destroyable.h"
#include "shell/common/gin_helper/error_thrower.h"
#include "shell/common/gin_helper/microtasks_scope.h"

// This file is forked from gin/function_template.h with 2 differences:
// 1. Support for additional types of arguments.
// 2. Support for warning using destroyed objects.
//
// TODO(zcbenz): We should seek to remove this file after removing native_mate.

namespace gin_helper {

struct InvokerOptions {
  bool holder_is_first_argument = false;
  const char* holder_type = nullptr;  // Null if unknown or not applicable.
};

template <typename T>
struct CallbackParamTraits {
  typedef T LocalType;
};
template <typename T>
struct CallbackParamTraits<const T&> {
  typedef T LocalType;
};
template <typename T>
struct CallbackParamTraits<const T*> {
  typedef T* LocalType;
};

// CallbackHolder and CallbackHolderBase are used to pass a
// base::RepeatingCallback from CreateFunctionTemplate through v8 (via
// v8::FunctionTemplate) to DispatchToCallback, where it is invoked.

// CallbackHolder will clean up the callback in two different scenarios:
// - If the garbage collector finds that it's garbage and collects it. (But note
//   that even _if_ we become garbage, we might never get collected!)
// - If the isolate gets disposed.
//
// TODO(crbug.com/1285119): When gin::Wrappable gets migrated over to using
//   cppgc, this class should also be considered for migration.

// This simple base class is used so that we can share a single object template
// among every CallbackHolder instance.
class CallbackHolderBase {
 public:
  CallbackHolderBase(const CallbackHolderBase&) = delete;
  CallbackHolderBase& operator=(const CallbackHolderBase&) = delete;

  v8::Local<v8::External> GetHandle(v8::Isolate* isolate);

 protected:
  explicit CallbackHolderBase(v8::Isolate* isolate);
  virtual ~CallbackHolderBase();

 private:
  class DisposeObserver : gin::PerIsolateData::DisposeObserver {
   public:
    DisposeObserver(gin::PerIsolateData* per_isolate_data,
                    CallbackHolderBase* holder);
    ~DisposeObserver() override;

    // gin::PerIsolateData::DisposeObserver
    void OnBeforeDispose(v8::Isolate* isolate) override;
    void OnDisposed() override;

   private:
    // Unlike in Chromium, it's possible for PerIsolateData to be null
    // for a given isolate - e.g. in a Node.js Worker. Thus this
    // needs to be a raw_ptr instead of a raw_ref.
    const raw_ptr<gin::PerIsolateData> per_isolate_data_;
    const raw_ref<CallbackHolderBase> holder_;
  };

  static void FirstWeakCallback(
      const v8::WeakCallbackInfo<CallbackHolderBase>& data);
  static void SecondWeakCallback(
      const v8::WeakCallbackInfo<CallbackHolderBase>& data);

  v8::Global<v8::External> v8_ref_;
  DisposeObserver dispose_observer_;
};

template <typename Sig>
class CallbackHolder : public CallbackHolderBase {
 public:
  CallbackHolder(v8::Isolate* isolate,
                 base::RepeatingCallback<Sig> callback,
                 InvokerOptions invoker_options)
      : CallbackHolderBase(isolate),
        callback(std::move(callback)),
        invoker_options(std::move(invoker_options)) {}
  CallbackHolder(const CallbackHolder&) = delete;
  CallbackHolder& operator=(const CallbackHolder&) = delete;

  base::RepeatingCallback<Sig> callback;
  InvokerOptions invoker_options;

 private:
  ~CallbackHolder() override = default;
};

template <typename T>
bool GetNextArgument(gin::Arguments* args,
                     const InvokerOptions& invoker_options,
                     bool is_first,
                     T* result) {
  if (is_first && invoker_options.holder_is_first_argument) {
    return args->GetHolder(result);
  } else {
    return args->GetNext(result);
  }
}

// Electron-specific GetNextArgument that supports std::optional.
template <typename T>
bool GetNextArgument(gin::Arguments* args,
                     const InvokerOptions& invoker_options,
                     bool is_first,
                     std::optional<T>* result) {
  T converted;
  // Use gin::Arguments::GetNext which always advances |next| counter.
  if (args->GetNext(&converted))
    result->emplace(std::move(converted));
  return true;
}

// Electron-specific GetNextArgument that supports ErrorThrower.
inline bool GetNextArgument(gin::Arguments* args,
                            const InvokerOptions& invoker_options,
                            bool is_first,
                            ErrorThrower* result) {
  *result = ErrorThrower(args->isolate());
  return true;
}

// Electron-specific GetNextArgument that supports the gin_helper::Arguments.
inline bool GetNextArgument(gin::Arguments* args,
                            const InvokerOptions& invoker_options,
                            bool is_first,
                            gin_helper::Arguments** result) {
  *result = static_cast<gin_helper::Arguments*>(args);
  return true;
}

// For advanced use cases, we allow callers to request the unparsed Arguments
// object and poke around in it directly.
inline bool GetNextArgument(gin::Arguments* args,
                            const InvokerOptions& invoker_options,
                            bool is_first,
                            gin::Arguments* result) {
  *result = *args;
  return true;
}

inline bool GetNextArgument(gin::Arguments* args,
                            const InvokerOptions& invoker_options,
                            bool is_first,
                            gin::Arguments** result) {
  *result = args;
  return true;
}

// It's common for clients to just need the isolate, so we make that easy.
inline bool GetNextArgument(gin::Arguments* args,
                            const InvokerOptions& invoker_options,
                            bool is_first,
                            v8::Isolate** result) {
  *result = args->isolate();
  return true;
}

// Throws an error indicating conversion failure.
void ThrowConversionError(gin::Arguments* args,
                          const InvokerOptions& invoker_options,
                          size_t index);

// Class template for extracting and storing single argument for callback
// at position |index|.
template <size_t index, typename ArgType, typename = void>
struct ArgumentHolder {
  using ArgLocalType = typename CallbackParamTraits<ArgType>::LocalType;

  ArgLocalType value;
  bool ok = false;

  ArgumentHolder(gin::Arguments* args, const InvokerOptions& invoker_options) {
    v8::Local<v8::Object> holder;
    if (index == 0 && invoker_options.holder_is_first_argument &&
        args->GetHolder(&holder) &&
        gin_helper::Destroyable::IsDestroyed(holder)) {
      args->ThrowTypeError("Object has been destroyed");
      return;
    }

    ok = GetNextArgument(args, invoker_options, index == 0, &value);
    if (!ok) {
      ThrowConversionError(args, invoker_options, index);
    }
  }
};

// This is required for types such as v8::LocalVector<T>, which don't have
// a default constructor. To create an element of such a type, the isolate
// has to be provided.
template <size_t index, typename ArgType>
struct ArgumentHolder<
    index,
    ArgType,
    std::enable_if_t<!std::is_default_constructible_v<
                         typename CallbackParamTraits<ArgType>::LocalType> &&
                     std::is_constructible_v<
                         typename CallbackParamTraits<ArgType>::LocalType,
                         v8::Isolate*>>> {
  using ArgLocalType = typename CallbackParamTraits<ArgType>::LocalType;

  ArgLocalType value;
  bool ok;

  ArgumentHolder(gin::Arguments* args, const InvokerOptions& invoker_options)
      : value(args->isolate()),
        ok(GetNextArgument(args, invoker_options, index == 0, &value)) {
    if (!ok) {
      ThrowConversionError(args, invoker_options, index);
    }
  }
};

// Class template for converting arguments from JavaScript to C++ and running
// the callback with them.
template <typename IndicesType, typename... ArgTypes>
class Invoker;

template <size_t... indices, typename... ArgTypes>
class Invoker<std::index_sequence<indices...>, ArgTypes...>
    : public ArgumentHolder<indices, ArgTypes>... {
 public:
  // Invoker<> inherits from ArgumentHolder<> for each argument.
  // C++ has always been strict about the class initialization order,
  // so it is guaranteed ArgumentHolders will be initialized (and thus, will
  // extract arguments from Arguments) in the right order.
  Invoker(gin::Arguments* args, const InvokerOptions& invoker_options)
      : ArgumentHolder<indices, ArgTypes>(args, invoker_options)...,
        args_(args) {}

  bool IsOK() { return And(ArgumentHolder<indices, ArgTypes>::ok...); }

  template <typename ReturnType>
  void DispatchToCallback(
      base::RepeatingCallback<ReturnType(ArgTypes...)> callback) {
    gin_helper::MicrotasksScope microtasks_scope(
        args_->isolate(),
        args_->GetHolderCreationContext()->GetMicrotaskQueue(), true);
    args_->Return(
        callback.Run(std::move(ArgumentHolder<indices, ArgTypes>::value)...));
  }

  // In C++, you can declare the function foo(void), but you can't pass a void
  // expression to foo. As a result, we must specialize the case of Callbacks
  // that have the void return type.
  void DispatchToCallback(base::RepeatingCallback<void(ArgTypes...)> callback) {
    gin_helper::MicrotasksScope microtasks_scope(
        args_->isolate(),
        args_->GetHolderCreationContext()->GetMicrotaskQueue(), true);
    callback.Run(std::move(ArgumentHolder<indices, ArgTypes>::value)...);
  }

 private:
  static bool And() { return true; }
  template <typename... T>
  static bool And(bool arg1, T... args) {
    return arg1 && And(args...);
  }

  raw_ptr<gin::Arguments> args_;
};

// DispatchToCallback converts all the JavaScript arguments to C++ types and
// invokes the base::RepeatingCallback.
template <typename Sig>
struct Dispatcher {};

template <typename ReturnType, typename... ArgTypes>
struct Dispatcher<ReturnType(ArgTypes...)> {
  static void DispatchToCallbackImpl(gin::Arguments* args) {
    v8::Local<v8::External> v8_holder;
    CHECK(args->GetData(&v8_holder));
    CallbackHolderBase* holder_base =
        reinterpret_cast<CallbackHolderBase*>(v8_holder->Value());

    typedef CallbackHolder<ReturnType(ArgTypes...)> HolderT;
    HolderT* holder = static_cast<HolderT*>(holder_base);

    using Indices = std::index_sequence_for<ArgTypes...>;
    Invoker<Indices, ArgTypes...> invoker(args, holder->invoker_options);
    if (invoker.IsOK())
      invoker.DispatchToCallback(holder->callback);
  }

  static void DispatchToCallback(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    gin::Arguments args(info);
    DispatchToCallbackImpl(&args);
  }

  static void DispatchToCallbackForProperty(
      v8::Local<v8::Name>,
      const v8::PropertyCallbackInfo<v8::Value>& info) {
    gin::Arguments args(info);
    DispatchToCallbackImpl(&args);
  }
};

// CreateFunctionTemplate creates a v8::FunctionTemplate that will create
// JavaScript functions that execute a provided C++ function or
// base::RepeatingCallback. JavaScript arguments are automatically converted via
// gin::Converter, as is the return value of the C++ function, if any.
// |invoker_options| contains additional parameters. If it contains a
// holder_type, it will be used to provide a useful conversion error if the
// holder is the first argument. If not provided, a generic invocation error
// will be used.
//
// NOTE: V8 caches FunctionTemplates for a lifetime of a web page for its own
// internal reasons, thus it is generally a good idea to cache the template
// returned by this function.  Otherwise, repeated method invocations from JS
// will create substantial memory leaks. See http://crbug.com/463487.
//
// The callback will be destroyed if either the function template gets garbage
// collected or _after_ the isolate is disposed. Garbage collection can never be
// relied upon. As such, any destructors for objects bound to the callback must
// not depend on the isolate being alive at the point they are called. The order
// in which callbacks are destroyed is not guaranteed.
template <typename Sig>
v8::Local<v8::FunctionTemplate> CreateFunctionTemplate(
    v8::Isolate* isolate,
    base::RepeatingCallback<Sig> callback,
    InvokerOptions invoker_options = {}) {
  typedef CallbackHolder<Sig> HolderT;
  HolderT* holder =
      new HolderT(isolate, std::move(callback), std::move(invoker_options));

  v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(
      isolate, &Dispatcher<Sig>::DispatchToCallback,
      gin::ConvertToV8<v8::Local<v8::External>>(isolate,
                                                holder->GetHandle(isolate)),
      v8::Local<v8::Signature>(), 0, v8::ConstructorBehavior::kAllow);
  return tmpl;
}

// CreateDataPropertyCallback creates a v8::AccessorNameGetterCallback and
// corresponding data value that will hold and execute the provided
// base::RepeatingCallback, using automatic conversions similar to
// |CreateFunctionTemplate|.
//
// It is expected that these will be passed to v8::Template::SetLazyDataProperty
// or another similar function.
template <typename Sig>
std::pair<v8::AccessorNameGetterCallback, v8::Local<v8::Value>>
CreateDataPropertyCallback(v8::Isolate* isolate,
                           base::RepeatingCallback<Sig> callback,
                           InvokerOptions invoker_options = {}) {
  typedef CallbackHolder<Sig> HolderT;
  HolderT* holder =
      new HolderT(isolate, std::move(callback), std::move(invoker_options));
  return {&Dispatcher<Sig>::DispatchToCallbackForProperty,
          gin::ConvertToV8<v8::Local<v8::External>>(
              isolate, holder->GetHandle(isolate))};
}

// Base template - used only for non-member function pointers. Other types
// either go to one of the below specializations, or go here and fail to compile
// because of base::Bind().
template <typename T, typename Enable = void>
struct CallbackTraits {
  static v8::Local<v8::FunctionTemplate> CreateTemplate(v8::Isolate* isolate,
                                                        T callback) {
    return gin_helper::CreateFunctionTemplate(isolate,
                                              base::BindRepeating(callback));
  }
};

// Specialization for base::RepeatingCallback.
template <typename T>
struct CallbackTraits<base::RepeatingCallback<T>> {
  static v8::Local<v8::FunctionTemplate> CreateTemplate(
      v8::Isolate* isolate,
      const base::RepeatingCallback<T>& callback) {
    return gin_helper::CreateFunctionTemplate(isolate, callback);
  }
};

// Specialization for member function pointers. We need to handle this case
// specially because the first parameter for callbacks to MFP should typically
// come from the JavaScript "this" object the function was called on, not
// from the first normal parameter.
template <typename T>
struct CallbackTraits<
    T,
    typename std::enable_if<std::is_member_function_pointer<T>::value>::type> {
  static v8::Local<v8::FunctionTemplate> CreateTemplate(v8::Isolate* isolate,
                                                        T callback) {
    InvokerOptions invoker_options = {.holder_is_first_argument = true};
    return gin_helper::CreateFunctionTemplate(
        isolate, base::BindRepeating(callback), std::move(invoker_options));
  }
};

}  // namespace gin_helper

#endif  // ELECTRON_SHELL_COMMON_GIN_HELPER_FUNCTION_TEMPLATE_H_
