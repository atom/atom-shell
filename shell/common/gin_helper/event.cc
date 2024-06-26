// Copyright (c) 2023 Salesforce, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/gin_helper/event.h"
#include "gin/dictionary.h"
#include "gin/object_template_builder.h"

namespace gin_helper::internal {

// static
gin::Handle<Event> Event::New(v8::Isolate* isolate) {
  return gin::CreateHandle(isolate, new Event());
}
// static
v8::Local<v8::ObjectTemplate> Event::FillObjectTemplate(
    v8::Isolate* isolate,
    v8::Local<v8::ObjectTemplate> templ) {
  return gin::ObjectTemplateBuilder(isolate, "Event", templ)
      .SetMethod("preventDefault", &Event::PreventDefault)
      .SetProperty("defaultPrevented", &Event::GetDefaultPrevented)
      .Build();
}

Event::Event() = default;

Event::~Event() = default;

gin::WrapperInfo Event::kWrapperInfo = {gin::kEmbedderNativeGin};

const char* Event::GetTypeName() {
  return GetClassName();
}

}  // namespace gin_helper::internal
