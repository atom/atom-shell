// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_COMMON_GIN_CONVERTERS_STD_CONVERTER_H_
#define ELECTRON_SHELL_COMMON_GIN_CONVERTERS_STD_CONVERTER_H_

#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <type_traits>
#include <utility>

#include "gin/converter.h"

#include "base/strings/string_util.h"
#if BUILDFLAG(IS_WIN)
#include "base/strings/string_util_win.h"
#endif

namespace gin {

// Make it possible to convert move-only types.
template <typename T>
v8::Local<v8::Value> ConvertToV8(v8::Isolate* isolate, T&& input) {
  return Converter<typename std::remove_reference<T>::type>::ToV8(
      isolate, std::forward<T>(input));
}

#if !BUILDFLAG(IS_LINUX)
template <>
struct Converter<unsigned long> {  // NOLINT(runtime/int)
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   unsigned long val) {  // NOLINT(runtime/int)
    return v8::Integer::New(isolate, val);
  }
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     unsigned long* out) {  // NOLINT(runtime/int)
    auto maybe = val->IntegerValue(isolate->GetCurrentContext());
    if (maybe.IsNothing())
      return false;
    *out = maybe.FromJust();
    return true;
  }
};
#endif

template <>
struct Converter<std::nullptr_t> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate, std::nullptr_t val) {
    return v8::Null(isolate);
  }
};

template <size_t N>
struct Converter<char[N]> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate, const char (&val)[N]) {
    return v8::String::NewFromUtf8Literal(isolate, val);
  }
};

template <>
struct Converter<const char*> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate, const char* val) {
    return v8::String::NewFromUtf8(isolate, val ? val : "",
                                   v8::NewStringType::kNormal)
        .ToLocalChecked();
  }
};

template <>
struct Converter<v8::Local<v8::Array>> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   v8::Local<v8::Array> val) {
    return val;
  }
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     v8::Local<v8::Array>* out) {
    if (!val->IsArray())
      return false;
    *out = val.As<v8::Array>();
    return true;
  }
};

template <>
struct Converter<v8::Local<v8::String>> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   v8::Local<v8::String> val) {
    return val;
  }
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     v8::Local<v8::String>* out) {
    if (!val->IsString())
      return false;
    *out = val.As<v8::String>();
    return true;
  }
};

template <typename T>
struct Converter<std::span<T>> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   const std::span<const T> vals) {
    const auto n_vals = vals.size();
    auto result = v8::Array::New(isolate, static_cast<int>(n_vals));

    uint32_t index = 0U;
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    for (const auto& val : vals) {
      v8::MaybeLocal<v8::Value> maybe = Converter<T>::ToV8(isolate, val);
      v8::Local<v8::Value> element;
      if (!maybe.ToLocal(&element))
        return {};

      bool property_created = false;
      if (!result->CreateDataProperty(context, index++, element)
               .To(&property_created) ||
          !property_created) {
        NOTREACHED() << "CreateDataProperty should always succeed here.";
      }
    }
    return result;
  }
};

template <typename T>
struct Converter<std::set<T>> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   const std::set<T>& val) {
    v8::Local<v8::Array> result(
        v8::Array::New(isolate, static_cast<int>(val.size())));
    auto context = isolate->GetCurrentContext();
    typename std::set<T>::const_iterator it;
    int i;
    for (i = 0, it = val.begin(); it != val.end(); ++it, ++i)
      result->Set(context, i, Converter<T>::ToV8(isolate, *it)).Check();
    return result;
  }
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     std::set<T>* out) {
    if (!val->IsArray())
      return false;

    auto context = isolate->GetCurrentContext();
    std::set<T> result;
    v8::Local<v8::Array> array = val.As<v8::Array>();
    uint32_t length = array->Length();
    for (uint32_t i = 0; i < length; ++i) {
      T item;
      if (!Converter<T>::FromV8(isolate,
                                array->Get(context, i).ToLocalChecked(), &item))
        return false;
      result.insert(item);
    }

    out->swap(result);
    return true;
  }
};

template <typename K, typename V>
struct Converter<std::map<K, V>> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> value,
                     std::map<K, V>* out) {
    if (!value->IsObject())
      return false;
    out->clear();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> obj = value.As<v8::Object>();
    v8::Local<v8::Array> keys = obj->GetPropertyNames(context).ToLocalChecked();
    for (uint32_t i = 0; i < keys->Length(); ++i) {
      v8::MaybeLocal<v8::Value> maybe_v8key = keys->Get(context, i);
      if (maybe_v8key.IsEmpty())
        return false;
      v8::Local<v8::Value> v8key = maybe_v8key.ToLocalChecked();
      v8::MaybeLocal<v8::Value> maybe_v8value = obj->Get(context, v8key);
      if (maybe_v8value.IsEmpty())
        return false;
      K key;
      V out_value;
      if (!ConvertFromV8(isolate, v8key, &key) ||
          !ConvertFromV8(isolate, maybe_v8value.ToLocalChecked(), &out_value))
        return false;
      (*out)[key] = std::move(out_value);
    }
    return true;
  }

  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   const std::map<K, V>& dict) {
    v8::Local<v8::Object> obj = v8::Object::New(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    for (const auto& it : dict) {
      if (obj->Set(context, ConvertToV8(isolate, it.first),
                   ConvertToV8(isolate, it.second))
              .IsNothing())
        break;
    }
    return obj;
  }
};

#if BUILDFLAG(IS_WIN)
template <>
struct Converter<std::wstring> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   const std::wstring& val) {
    return Converter<std::u16string>::ToV8(isolate, base::AsString16(val));
  }
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     std::wstring* out) {
    if (!val->IsString())
      return false;

    std::u16string str;
    if (Converter<std::u16string>::FromV8(isolate, val, &str)) {
      *out = base::AsWString(str);
      return true;
    } else {
      return false;
    }
  }
};
#endif

namespace detail {

// Get a key from `key_val` and check `lookup` for a matching entry.
// Return true iff a match is found, and set `*out` to the entry's value.
template <typename KeyType, typename Out, typename Map>
bool FromV8WithLookup(v8::Isolate* isolate,
                      v8::Local<v8::Value> key_val,
                      const Map& table,
                      Out* out,
                      std::function<void(KeyType&)> key_transform = {}) {
  static_assert(std::is_same_v<typename Map::mapped_type, Out>);

  auto key = KeyType{};
  if (!ConvertFromV8(isolate, key_val, &key))
    return false;

  if (key_transform)
    key_transform(key);

  if (auto iter = table.find(key); iter != table.end()) {
    *out = iter->second;
    return true;
  }

  return false;
}

}  // namespace detail

// Convert `key_val to a string key and check `lookup` for a matching entry.
// Return true iff a match is found, and set `*out` to the entry's value.
template <typename Out, typename Map>
bool FromV8WithLookup(v8::Isolate* isolate,
                      v8::Local<v8::Value> key_val,
                      const Map& table,
                      Out* out) {
  return detail::FromV8WithLookup<std::string>(isolate, key_val, table, out);
}

// Convert `key_val` to a lowercase string key and check `lookup` for a matching
// entry. Return true iff a match is found, and set `*out` to the entry's value.
template <typename Out, typename Map>
bool FromV8WithLowerLookup(v8::Isolate* isolate,
                           v8::Local<v8::Value> key_val,
                           const Map& table,
                           Out* out) {
  static constexpr auto to_lower_ascii_inplace = [](std::string& str) {
    for (auto& ch : str)
      ch = base::ToLowerASCII(ch);
  };

  return detail::FromV8WithLookup<std::string>(isolate, key_val, table, out,
                                               to_lower_ascii_inplace);
}

}  // namespace gin

#endif  // ELECTRON_SHELL_COMMON_GIN_CONVERTERS_STD_CONVERTER_H_
