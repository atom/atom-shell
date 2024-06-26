// Copyright (c) 2019 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_NATIVE_THEME_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_NATIVE_THEME_H_

#include "base/memory/raw_ptr.h"
#include "gin/handle.h"
#include "gin/wrappable.h"
#include "shell/browser/event_emitter_mixin.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"

namespace electron::api {

class NativeTheme : public gin::Wrappable<NativeTheme>,
                    public gin_helper::EventEmitterMixin<NativeTheme>,
                    private ui::NativeThemeObserver {
 public:
  static gin::Handle<NativeTheme> Create(v8::Isolate* isolate);

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  // disable copy
  NativeTheme(const NativeTheme&) = delete;
  NativeTheme& operator=(const NativeTheme&) = delete;

 protected:
  NativeTheme(v8::Isolate* isolate,
              ui::NativeTheme* ui_theme,
              ui::NativeTheme* web_theme);
  ~NativeTheme() override;

  void SetThemeSource(ui::NativeTheme::ThemeSource override);
#if BUILDFLAG(IS_MAC)
  void UpdateMacOSAppearanceForOverrideValue(
      ui::NativeTheme::ThemeSource override);
#endif
  ui::NativeTheme::ThemeSource GetThemeSource() const;
  bool ShouldUseDarkColors();
  bool ShouldUseHighContrastColors();
  bool ShouldUseInvertedColorScheme();
  bool InForcedColorsMode();

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* theme) override;
  void OnNativeThemeUpdatedOnUI();

 private:
  raw_ptr<ui::NativeTheme> ui_theme_;
  raw_ptr<ui::NativeTheme> web_theme_;
};

}  // namespace electron::api

namespace gin {

template <>
struct Converter<ui::NativeTheme::ThemeSource> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   const ui::NativeTheme::ThemeSource& val);
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     ui::NativeTheme::ThemeSource* out);
};

}  // namespace gin

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_NATIVE_THEME_H_
