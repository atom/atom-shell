// Copyright (c) 2023 Salesforce, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_COMMON_GIN_HELPER_EVENT_H_
#define ELECTRON_SHELL_COMMON_GIN_HELPER_EVENT_H_

#include "gin/handle.h"
#include "gin/wrappable.h"
#include "shell/common/gin_helper/constructible.h"

namespace v8 {
class Isolate;
template <typename T>
class Local;
class Object;
class ObjectTemplate;
}  // namespace v8

namespace gin_helper::internal {

class Event : public gin::Wrappable<Event>,
              public gin_helper::Constructible<Event> {
 public:
  // gin_helper::Constructible
  static gin::Handle<Event> New(v8::Isolate* isolate);
  static v8::Local<v8::ObjectTemplate> FillObjectTemplate(
      v8::Isolate* isolate,
      v8::Local<v8::ObjectTemplate> prototype);
  static const char* GetClassName() { return "Event"; }

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  const char* GetTypeName() override;

  ~Event() override;

  void PreventDefault() { default_prevented_ = true; }

  bool GetDefaultPrevented() { return default_prevented_; }

 private:
  Event();

  bool default_prevented_ = false;
};

}  // namespace gin_helper::internal

#endif  // ELECTRON_SHELL_COMMON_GIN_HELPER_EVENT_H_
