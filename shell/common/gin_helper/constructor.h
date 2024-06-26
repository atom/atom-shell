// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_COMMON_GIN_HELPER_CONSTRUCTOR_H_
#define ELECTRON_SHELL_COMMON_GIN_HELPER_CONSTRUCTOR_H_

#include "shell/common/gin_helper/function_template.h"
#include "shell/common/gin_helper/wrappable_base.h"

namespace gin_helper {

namespace internal {

// This set of templates invokes a base::RepeatingCallback by converting the
// Arguments into native types. It relies on the function_template.h to provide
// helper templates.
inline WrappableBase* InvokeFactory(
    gin::Arguments* args,
    const base::RepeatingCallback<WrappableBase*()>& callback) {
  return callback.Run();
}

template <typename P1>
inline WrappableBase* InvokeFactory(
    gin::Arguments* args,
    const base::RepeatingCallback<WrappableBase*(P1)>& callback) {
  typename CallbackParamTraits<P1>::LocalType a1;
  if (!gin_helper::GetNextArgument(args, {.holder_is_first_argument = true}, 0,
                                   &a1))
    return nullptr;
  return callback.Run(a1);
}

template <typename P1, typename P2>
inline WrappableBase* InvokeFactory(
    gin::Arguments* args,
    const base::RepeatingCallback<WrappableBase*(P1, P2)>& callback) {
  typename CallbackParamTraits<P1>::LocalType a1;
  typename CallbackParamTraits<P2>::LocalType a2;
  if (!gin_helper::GetNextArgument(args, {.holder_is_first_argument = true}, 0,
                                   &a1) ||
      !gin_helper::GetNextArgument(args, {.holder_is_first_argument = false}, 0,
                                   &a2))
    return nullptr;
  return callback.Run(a1, a2);
}

template <typename P1, typename P2, typename P3>
inline WrappableBase* InvokeFactory(
    gin::Arguments* args,
    const base::RepeatingCallback<WrappableBase*(P1, P2, P3)>& callback) {
  typename CallbackParamTraits<P1>::LocalType a1;
  typename CallbackParamTraits<P2>::LocalType a2;
  typename CallbackParamTraits<P3>::LocalType a3;
  if (!gin_helper::GetNextArgument(args, {.holder_is_first_argument = true}, 0,
                                   &a1) ||
      !gin_helper::GetNextArgument(args, {.holder_is_first_argument = false}, 0,
                                   &a2) ||
      !gin_helper::GetNextArgument(args, {.holder_is_first_argument = false}, 0,
                                   &a3))
    return nullptr;
  return callback.Run(a1, a2, a3);
}

template <typename P1, typename P2, typename P3, typename P4>
inline WrappableBase* InvokeFactory(
    gin::Arguments* args,
    const base::RepeatingCallback<WrappableBase*(P1, P2, P3, P4)>& callback) {
  typename CallbackParamTraits<P1>::LocalType a1;
  typename CallbackParamTraits<P2>::LocalType a2;
  typename CallbackParamTraits<P3>::LocalType a3;
  typename CallbackParamTraits<P4>::LocalType a4;
  if (!gin_helper::GetNextArgument(args, {.holder_is_first_argument = true}, 0,
                                   &a1) ||
      !gin_helper::GetNextArgument(args, {.holder_is_first_argument = false}, 0,
                                   &a2) ||
      !gin_helper::GetNextArgument(args, {.holder_is_first_argument = false}, 0,
                                   &a3) ||
      !gin_helper::GetNextArgument(args, {.holder_is_first_argument = false}, 0,
                                   &a4))
    return nullptr;
  return callback.Run(a1, a2, a3, a4);
}

template <typename P1, typename P2, typename P3, typename P4, typename P5>
inline WrappableBase* InvokeFactory(
    gin::Arguments* args,
    const base::RepeatingCallback<WrappableBase*(P1, P2, P3, P4, P5)>&
        callback) {
  typename CallbackParamTraits<P1>::LocalType a1;
  typename CallbackParamTraits<P2>::LocalType a2;
  typename CallbackParamTraits<P3>::LocalType a3;
  typename CallbackParamTraits<P4>::LocalType a4;
  typename CallbackParamTraits<P5>::LocalType a5;
  if (!gin_helper::GetNextArgument(args, {.holder_is_first_argument = true}, 0,
                                   &a1) ||
      !gin_helper::GetNextArgument(args, {.holder_is_first_argument = false}, 0,
                                   &a2) ||
      !gin_helper::GetNextArgument(args, {.holder_is_first_argument = false}, 0,
                                   &a3) ||
      !gin_helper::GetNextArgument(args, {.holder_is_first_argument = false}, 0,
                                   &a4) ||
      !gin_helper::GetNextArgument(args, {.holder_is_first_argument = false}, 0,
                                   &a5))
    return nullptr;
  return callback.Run(a1, a2, a3, a4, a5);
}

template <typename P1,
          typename P2,
          typename P3,
          typename P4,
          typename P5,
          typename P6>
inline WrappableBase* InvokeFactory(
    gin::Arguments* args,
    const base::RepeatingCallback<WrappableBase*(P1, P2, P3, P4, P5, P6)>&
        callback) {
  typename CallbackParamTraits<P1>::LocalType a1;
  typename CallbackParamTraits<P2>::LocalType a2;
  typename CallbackParamTraits<P3>::LocalType a3;
  typename CallbackParamTraits<P4>::LocalType a4;
  typename CallbackParamTraits<P5>::LocalType a5;
  typename CallbackParamTraits<P6>::LocalType a6;
  if (!gin_helper::GetNextArgument(args, {.holder_is_first_argument = true}, 0,
                                   &a1) ||
      !gin_helper::GetNextArgument(args, {.holder_is_first_argument = false}, 0,
                                   &a2) ||
      !gin_helper::GetNextArgument(args, {.holder_is_first_argument = false}, 0,
                                   &a3) ||
      !gin_helper::GetNextArgument(args, {.holder_is_first_argument = false}, 0,
                                   &a4) ||
      !gin_helper::GetNextArgument(args, {.holder_is_first_argument = false}, 0,
                                   &a5) ||
      !gin_helper::GetNextArgument(args, {.holder_is_first_argument = false}, 0,
                                   &a6))
    return nullptr;
  return callback.Run(a1, a2, a3, a4, a5, a6);
}

template <typename Sig>
void InvokeNew(const base::RepeatingCallback<Sig>& factory,
               v8::Isolate* isolate,
               gin_helper::Arguments* args) {
  if (!args->IsConstructCall()) {
    args->ThrowError("Requires constructor call");
    return;
  }

  WrappableBase* object;
  {
    // Don't continue if the constructor throws an exception.
    v8::TryCatch try_catch(isolate);
    object = internal::InvokeFactory(args, factory);
    if (try_catch.HasCaught()) {
      try_catch.ReThrow();
      return;
    }
  }

  if (!object)
    args->ThrowError();

  return;
}

}  // namespace internal

// Create a FunctionTemplate that can be "new"ed in JavaScript.
// It is user's responsibility to ensure this function is called for one type
// only ONCE in the program's whole lifetime, otherwise we would have memory
// leak.
template <typename T, typename Sig>
v8::Local<v8::Function> CreateConstructor(
    v8::Isolate* isolate,
    const base::RepeatingCallback<Sig>& func) {
#ifndef NDEBUG
  static bool called = false;
  CHECK(!called) << "CreateConstructor can only be called for one type once";
  called = true;
#endif
  v8::Local<v8::FunctionTemplate> templ = gin_helper::CreateFunctionTemplate(
      isolate, base::BindRepeating(&internal::InvokeNew<Sig>, func));
  templ->InstanceTemplate()->SetInternalFieldCount(1);
  T::BuildPrototype(isolate, templ);
  return templ->GetFunction(isolate->GetCurrentContext()).ToLocalChecked();
}

}  // namespace gin_helper

#endif  // ELECTRON_SHELL_COMMON_GIN_HELPER_CONSTRUCTOR_H_
