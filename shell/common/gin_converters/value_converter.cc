// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/gin_converters/value_converter.h"

#include <memory>
#include <utility>

#include "content/public/renderer/v8_value_converter.h"

namespace gin {

bool Converter<base::Value::Dict>::FromV8(v8::Isolate* isolate,
                                          v8::Local<v8::Value> val,
                                          base::Value::Dict* out) {
  std::unique_ptr<base::Value> value =
      content::V8ValueConverter::Create()->FromV8Value(
          val, isolate->GetCurrentContext());
  if (value && value->is_dict()) {
    *out = std::move(*value).TakeDict();
    return true;
  } else {
    return false;
  }
}

bool Converter<base::Value>::FromV8(v8::Isolate* isolate,
                                    v8::Local<v8::Value> val,
                                    base::Value* out) {
  std::unique_ptr<base::Value> value =
      content::V8ValueConverter::Create()->FromV8Value(
          val, isolate->GetCurrentContext());
  if (value) {
    *out = std::move(*value);
    return true;
  } else {
    return false;
  }
}

v8::Local<v8::Value> Converter<base::ValueView>::ToV8(
    v8::Isolate* isolate,
    const base::ValueView val) {
  return content::V8ValueConverter::Create()->ToV8Value(
      val, isolate->GetCurrentContext());
}

bool Converter<base::Value::List>::FromV8(v8::Isolate* isolate,
                                          v8::Local<v8::Value> val,
                                          base::Value::List* out) {
  std::unique_ptr<base::Value> value =
      content::V8ValueConverter::Create()->FromV8Value(
          val, isolate->GetCurrentContext());
  if (value && value->is_list()) {
    *out = std::move(*value).TakeList();
    return true;
  } else {
    return false;
  }
}

}  // namespace gin
