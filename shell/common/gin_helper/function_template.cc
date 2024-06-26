// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.

#include "shell/common/gin_helper/function_template.h"

#include "base/observer_list.h"
#include "base/strings/strcat.h"

namespace gin_helper {

CallbackHolderBase::DisposeObserver::DisposeObserver(
    gin::PerIsolateData* per_isolate_data,
    CallbackHolderBase* holder)
    : per_isolate_data_(per_isolate_data), holder_(*holder) {
  if (per_isolate_data_)
    per_isolate_data_->AddDisposeObserver(this);
}
CallbackHolderBase::DisposeObserver::~DisposeObserver() {
  if (per_isolate_data_)
    per_isolate_data_->RemoveDisposeObserver(this);
}
void CallbackHolderBase::DisposeObserver::OnBeforeDispose(
    v8::Isolate* isolate) {
  holder_->v8_ref_.Reset();
}
void CallbackHolderBase::DisposeObserver::OnDisposed() {
  // The holder contains the observer, so the observer is destroyed here also.
  delete &holder_.get();
}

CallbackHolderBase::CallbackHolderBase(v8::Isolate* isolate)
    : v8_ref_(isolate, v8::External::New(isolate, this)),
      dispose_observer_(gin::PerIsolateData::From(isolate), this) {
  v8_ref_.SetWeak(this, &CallbackHolderBase::FirstWeakCallback,
                  v8::WeakCallbackType::kParameter);
}

CallbackHolderBase::~CallbackHolderBase() {
  DCHECK(v8_ref_.IsEmpty());
}

v8::Local<v8::External> CallbackHolderBase::GetHandle(v8::Isolate* isolate) {
  return v8::Local<v8::External>::New(isolate, v8_ref_);
}

// static
void CallbackHolderBase::FirstWeakCallback(
    const v8::WeakCallbackInfo<CallbackHolderBase>& data) {
  data.GetParameter()->v8_ref_.Reset();
  data.SetSecondPassCallback(SecondWeakCallback);
}

// static
void CallbackHolderBase::SecondWeakCallback(
    const v8::WeakCallbackInfo<CallbackHolderBase>& data) {
  delete data.GetParameter();
}

void ThrowConversionError(gin::Arguments* args,
                          const InvokerOptions& invoker_options,
                          size_t index) {
  if (index == 0 && invoker_options.holder_is_first_argument) {
    // Failed to get the appropriate `this` object. This can happen if a
    // method is invoked using Function.prototype.[call|apply] and passed an
    // invalid (or null) `this` argument.
    std::string error =
        invoker_options.holder_type
            ? base::StrCat({"Illegal invocation: Function must be "
                            "called on an object of type ",
                            invoker_options.holder_type})
            : "Illegal invocation";
    args->ThrowTypeError(error);
  } else {
    // Otherwise, this failed parsing on a different argument.
    // Arguments::ThrowError() will try to include appropriate information.
    // Ideally we would include the expected c++ type in the error message
    // here, too (which we can access via typeid(ArgType).name()), however we
    // compile with no-rtti, which disables typeid.
    args->ThrowError();
  }
}

}  // namespace gin_helper
