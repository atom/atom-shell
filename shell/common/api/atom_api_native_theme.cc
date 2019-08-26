// Copyright (c) 2019 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/api/atom_api_native_theme.h"

#include "native_mate/dictionary.h"
#include "native_mate/object_template_builder.h"
#include "shell/common/node_includes.h"
#include "ui/gfx/color_utils.h"
#include "ui/native_theme/native_theme.h"

namespace electron {

namespace api {

NativeTheme::NativeTheme(v8::Isolate* isolate, ui::NativeTheme* theme)
    : theme_(theme) {
  theme_->AddObserver(this);
  Init(isolate);
}

NativeTheme::~NativeTheme() {
  theme_->RemoveObserver(this);
}

void NativeTheme::OnNativeThemeUpdated(ui::NativeTheme* theme) {
  Emit("updated");
}

void NativeTheme::SetShouldUseDarkColorsOverride(
    ui::NativeTheme::OverrideShouldUseDarkColors override) {
  theme_->set_override_should_use_dark_colors(override);
#if defined(OS_MACOSX)
  // Update the macOS appearance setting for this new override value
  UpdateMacOSAppearanceForOverrideValue(override);
#endif
  // TODO(MarshallOfSound): Update all existing browsers windows to use GTK dark
  // theme
}

ui::NativeTheme::OverrideShouldUseDarkColors
NativeTheme::GetShouldUseDarkColorsOverride() const {
  return theme_->override_should_use_dark_colors();
}

bool NativeTheme::ShouldUseDarkColors() {
  return theme_->ShouldUseDarkColors();
}

bool NativeTheme::ShouldUseHighContrastColors() {
  return theme_->UsesHighContrastColors();
}

#if defined(OS_MACOSX)
const CFStringRef WhiteOnBlack = CFSTR("whiteOnBlack");
const CFStringRef UniversalAccessDomain = CFSTR("com.apple.universalaccess");
#endif

// TODO(MarshallOfSound): Implement for Linux
bool NativeTheme::ShouldUseInvertedColorScheme() {
#if defined(OS_MACOSX)
  CFPreferencesAppSynchronize(UniversalAccessDomain);
  Boolean keyExistsAndHasValidFormat = false;
  Boolean is_inverted = CFPreferencesGetAppBooleanValue(
      WhiteOnBlack, UniversalAccessDomain, &keyExistsAndHasValidFormat);
  if (!keyExistsAndHasValidFormat)
    return false;
  return is_inverted;
#else
  return color_utils::IsInvertedColorScheme();
#endif
}

// static
v8::Local<v8::Value> NativeTheme::Create(v8::Isolate* isolate) {
  ui::NativeTheme* theme = ui::NativeTheme::GetInstanceForNativeUi();
  return mate::CreateHandle(isolate, new NativeTheme(isolate, theme)).ToV8();
}

// static
void NativeTheme::BuildPrototype(v8::Isolate* isolate,
                                 v8::Local<v8::FunctionTemplate> prototype) {
  prototype->SetClassName(mate::StringToV8(isolate, "NativeTheme"));
  mate::ObjectTemplateBuilder(isolate, prototype->PrototypeTemplate())
      .SetProperty("shouldUseDarkColors", &NativeTheme::ShouldUseDarkColors)
      .SetProperty("shouldUseDarkColorsOverride",
                   &NativeTheme::GetShouldUseDarkColorsOverride,
                   &NativeTheme::SetShouldUseDarkColorsOverride)
      .SetProperty("shouldUseHighContrastColors",
                   &NativeTheme::ShouldUseHighContrastColors)
      .SetProperty("shouldUseInvertedColorScheme",
                   &NativeTheme::ShouldUseInvertedColorScheme);
}

}  // namespace api

}  // namespace electron

namespace {

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  mate::Dictionary dict(isolate, exports);
  dict.Set("nativeTheme", electron::api::NativeTheme::Create(isolate));
  dict.Set("NativeTheme", electron::api::NativeTheme::GetConstructor(isolate)
                              ->GetFunction(context)
                              .ToLocalChecked());
}

}  // namespace

namespace mate {

v8::Local<v8::Value>
Converter<ui::NativeTheme::OverrideShouldUseDarkColors>::ToV8(
    v8::Isolate* isolate,
    const ui::NativeTheme::OverrideShouldUseDarkColors& val) {
  switch (val) {
    case ui::NativeTheme::OverrideShouldUseDarkColors::kForceDarkColorsEnabled:
      return mate::ConvertToV8(isolate, true);
    case ui::NativeTheme::OverrideShouldUseDarkColors::kForceDarkColorsDisabled:
      return mate::ConvertToV8(isolate, false);
    case ui::NativeTheme::OverrideShouldUseDarkColors::kNoOverride:
    default:
      return mate::ConvertToV8(isolate, nullptr);
  }
}

bool Converter<ui::NativeTheme::OverrideShouldUseDarkColors>::FromV8(
    v8::Isolate* isolate,
    v8::Local<v8::Value> val,
    ui::NativeTheme::OverrideShouldUseDarkColors* out) {
  if (val->IsNull() || val->IsUndefined()) {
    *out = ui::NativeTheme::OverrideShouldUseDarkColors::kNoOverride;
    return true;
  }

  bool force_dark;
  if (mate::ConvertFromV8(isolate, val, &force_dark)) {
    if (force_dark) {
      *out =
          ui::NativeTheme::OverrideShouldUseDarkColors::kForceDarkColorsEnabled;
    } else {
      *out = ui::NativeTheme::OverrideShouldUseDarkColors::
          kForceDarkColorsDisabled;
    }
    return true;
  }
  return false;
}

}  // namespace mate

NODE_LINKED_MODULE_CONTEXT_AWARE(atom_common_native_theme, Initialize)
