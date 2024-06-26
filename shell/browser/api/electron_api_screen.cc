// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_screen.h"

#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "gin/dictionary.h"
#include "gin/handle.h"
#include "shell/browser/browser.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/gfx_converter.h"
#include "shell/common/gin_converters/native_window_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/object_template_builder.h"
#include "shell/common/node_includes.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/point.h"

#if BUILDFLAG(IS_WIN)
#include "ui/display/win/screen_win.h"
#endif

#if defined(USE_OZONE)
#include "ui/ozone/public/ozone_platform.h"
#endif

namespace electron::api {

gin::WrapperInfo Screen::kWrapperInfo = {gin::kEmbedderNativeGin};

namespace {

// Convert the changed_metrics bitmask to string array.
std::vector<std::string> MetricsToArray(uint32_t metrics) {
  std::vector<std::string> array;
  if (metrics & display::DisplayObserver::DISPLAY_METRIC_BOUNDS)
    array.emplace_back("bounds");
  if (metrics & display::DisplayObserver::DISPLAY_METRIC_WORK_AREA)
    array.emplace_back("workArea");
  if (metrics & display::DisplayObserver::DISPLAY_METRIC_DEVICE_SCALE_FACTOR)
    array.emplace_back("scaleFactor");
  if (metrics & display::DisplayObserver::DISPLAY_METRIC_ROTATION)
    array.emplace_back("rotation");
  return array;
}

void DelayEmit(Screen* screen,
               const std::string_view name,
               const display::Display& display) {
  screen->Emit(name, display);
}

void DelayEmitWithMetrics(Screen* screen,
                          const std::string_view name,
                          const display::Display& display,
                          const std::vector<std::string>& metrics) {
  screen->Emit(name, display, metrics);
}

}  // namespace

Screen::Screen(v8::Isolate* isolate, display::Screen* screen)
    : screen_(screen) {
  screen_->AddObserver(this);
}

Screen::~Screen() {
  screen_->RemoveObserver(this);
}

gfx::Point Screen::GetCursorScreenPoint(v8::Isolate* isolate) {
#if defined(USE_OZONE)
  // Wayland will crash unless a window is created prior to calling
  // GetCursorScreenPoint.
  if (!ui::OzonePlatform::IsInitialized()) {
    gin_helper::ErrorThrower thrower(isolate);
    thrower.ThrowError(
        "screen.getCursorScreenPoint() cannot be called before a window has "
        "been created.");
    return gfx::Point();
  }
#endif
  return screen_->GetCursorScreenPoint();
}

#if BUILDFLAG(IS_WIN)

static gfx::Rect ScreenToDIPRect(electron::NativeWindow* window,
                                 const gfx::Rect& rect) {
  HWND hwnd = window ? window->GetAcceleratedWidget() : nullptr;
  return display::win::ScreenWin::ScreenToDIPRect(hwnd, rect);
}

static gfx::Rect DIPToScreenRect(electron::NativeWindow* window,
                                 const gfx::Rect& rect) {
  HWND hwnd = window ? window->GetAcceleratedWidget() : nullptr;
  return display::win::ScreenWin::DIPToScreenRect(hwnd, rect);
}

#endif

void Screen::OnDisplayAdded(const display::Display& new_display) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostNonNestableTask(
      FROM_HERE, base::BindOnce(&DelayEmit, base::Unretained(this),
                                "display-added", new_display));
}

void Screen::OnDisplaysRemoved(const display::Displays& old_displays) {
  for (const auto& old_display : old_displays) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostNonNestableTask(
        FROM_HERE, base::BindOnce(&DelayEmit, base::Unretained(this),
                                  "display-removed", old_display));
  }
}

void Screen::OnDisplayMetricsChanged(const display::Display& display,
                                     uint32_t changed_metrics) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostNonNestableTask(
      FROM_HERE, base::BindOnce(&DelayEmitWithMetrics, base::Unretained(this),
                                "display-metrics-changed", display,
                                MetricsToArray(changed_metrics)));
}

// static
v8::Local<v8::Value> Screen::Create(gin_helper::ErrorThrower error_thrower) {
  if (!Browser::Get()->is_ready()) {
    error_thrower.ThrowError(
        "The 'screen' module can't be used before the app 'ready' event");
    return v8::Null(error_thrower.isolate());
  }

  display::Screen* screen = display::Screen::GetScreen();
  if (!screen) {
    error_thrower.ThrowError("Failed to get screen information");
    return v8::Null(error_thrower.isolate());
  }

  return gin::CreateHandle(error_thrower.isolate(),
                           new Screen(error_thrower.isolate(), screen))
      .ToV8();
}

gin::ObjectTemplateBuilder Screen::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin_helper::EventEmitterMixin<Screen>::GetObjectTemplateBuilder(
             isolate)
      .SetMethod("getCursorScreenPoint", &Screen::GetCursorScreenPoint)
      .SetMethod("getPrimaryDisplay", &Screen::GetPrimaryDisplay)
      .SetMethod("getAllDisplays", &Screen::GetAllDisplays)
      .SetMethod("getDisplayNearestPoint", &Screen::GetDisplayNearestPoint)
#if BUILDFLAG(IS_WIN)
      .SetMethod("screenToDipPoint", &display::win::ScreenWin::ScreenToDIPPoint)
      .SetMethod("dipToScreenPoint", &display::win::ScreenWin::DIPToScreenPoint)
      .SetMethod("screenToDipRect", &ScreenToDIPRect)
      .SetMethod("dipToScreenRect", &DIPToScreenRect)
#endif
      .SetMethod("getDisplayMatching", &Screen::GetDisplayMatching);
}

const char* Screen::GetTypeName() {
  return "Screen";
}

}  // namespace electron::api

namespace {

using electron::api::Screen;

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary dict(isolate, exports);
  dict.SetMethod("createScreen", base::BindRepeating(&Screen::Create));
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_browser_screen, Initialize)
