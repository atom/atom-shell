// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_SCREEN_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_SCREEN_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "gin/wrappable.h"
#include "shell/browser/event_emitter_mixin.h"
#include "shell/common/gin_helper/error_thrower.h"
#include "ui/display/display_observer.h"
#include "ui/display/screen.h"

namespace gfx {
class Point;
class Rect;
class Screen;
}  // namespace gfx

namespace electron::api {

class Screen : public gin::Wrappable<Screen>,
               public gin_helper::EventEmitterMixin<Screen>,
               private display::DisplayObserver {
 public:
  static v8::Local<v8::Value> Create(gin_helper::ErrorThrower error_thrower);

  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  // disable copy
  Screen(const Screen&) = delete;
  Screen& operator=(const Screen&) = delete;

 protected:
  Screen(v8::Isolate* isolate, display::Screen* screen);
  ~Screen() override;

  gfx::Point GetCursorScreenPoint(v8::Isolate* isolate);
  display::Display GetPrimaryDisplay() const {
    return screen_->GetPrimaryDisplay();
  }
  const std::vector<display::Display>& GetAllDisplays() const {
    return screen_->GetAllDisplays();
  }
  display::Display GetDisplayNearestPoint(const gfx::Point& point) const {
    return screen_->GetDisplayNearestPoint(point);
  }
  display::Display GetDisplayMatching(const gfx::Rect& match_rect) const {
    return screen_->GetDisplayMatching(match_rect);
  }

  // display::DisplayObserver:
  void OnDisplayAdded(const display::Display& new_display) override;
  void OnDisplaysRemoved(const display::Displays& removed_displays) override;
  void OnDisplayMetricsChanged(const display::Display& display,
                               uint32_t changed_metrics) override;

 private:
  raw_ptr<display::Screen> screen_;
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_SCREEN_H_
