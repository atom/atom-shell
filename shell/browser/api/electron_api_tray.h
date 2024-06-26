// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_TRAY_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_TRAY_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gin/handle.h"
#include "gin/wrappable.h"
#include "shell/browser/event_emitter_mixin.h"
#include "shell/browser/javascript_environment.h"
#include "shell/browser/ui/tray_icon.h"
#include "shell/browser/ui/tray_icon_observer.h"
#include "shell/common/gin_converters/guid_converter.h"
#include "shell/common/gin_helper/cleaned_up_at_exit.h"
#include "shell/common/gin_helper/constructible.h"
#include "shell/common/gin_helper/error_thrower.h"
#include "shell/common/gin_helper/pinnable.h"

namespace gfx {
class Image;
}

namespace gin_helper {
class Dictionary;
}

namespace electron::api {

class Menu;

class Tray : public gin::Wrappable<Tray>,
             public gin_helper::EventEmitterMixin<Tray>,
             public gin_helper::Constructible<Tray>,
             public gin_helper::CleanedUpAtExit,
             public gin_helper::Pinnable<Tray>,
             private TrayIconObserver {
 public:
  // gin_helper::Constructible
  static gin::Handle<Tray> New(gin_helper::ErrorThrower thrower,
                               v8::Local<v8::Value> image,
                               std::optional<UUID> guid,
                               gin::Arguments* args);

  static void FillObjectTemplate(v8::Isolate*, v8::Local<v8::ObjectTemplate>);
  static const char* GetClassName() { return "Tray"; }

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  const char* GetTypeName() override;

  // disable copy
  Tray(const Tray&) = delete;
  Tray& operator=(const Tray&) = delete;

 private:
  Tray(v8::Isolate* isolate,
       v8::Local<v8::Value> image,
       std::optional<UUID> guid);
  ~Tray() override;

  // TrayIconObserver:
  void OnClicked(const gfx::Rect& bounds,
                 const gfx::Point& location,
                 int modifiers) override;
  void OnDoubleClicked(const gfx::Rect& bounds, int modifiers) override;
  void OnRightClicked(const gfx::Rect& bounds, int modifiers) override;
  void OnMiddleClicked(const gfx::Rect& bounds, int modifiers) override;
  void OnBalloonShow() override;
  void OnBalloonClicked() override;
  void OnBalloonClosed() override;
  void OnDrop() override;
  void OnDropFiles(const std::vector<std::string>& files) override;
  void OnDropText(const std::string& text) override;
  void OnDragEntered() override;
  void OnDragExited() override;
  void OnDragEnded() override;
  void OnMouseUp(const gfx::Point& location, int modifiers) override;
  void OnMouseDown(const gfx::Point& location, int modifiers) override;
  void OnMouseEntered(const gfx::Point& location, int modifiers) override;
  void OnMouseExited(const gfx::Point& location, int modifiers) override;
  void OnMouseMoved(const gfx::Point& location, int modifiers) override;

  // JS API:
  void Destroy();
  bool IsDestroyed();
  void SetImage(v8::Isolate* isolate, v8::Local<v8::Value> image);
  void SetPressedImage(v8::Isolate* isolate, v8::Local<v8::Value> image);
  void SetToolTip(const std::string& tool_tip);
  void SetTitle(const std::string& title,
                const std::optional<gin_helper::Dictionary>& options,
                gin::Arguments* args);
  std::string GetTitle();
  void SetIgnoreDoubleClickEvents(bool ignore);
  bool GetIgnoreDoubleClickEvents();
  void DisplayBalloon(gin_helper::ErrorThrower thrower,
                      const gin_helper::Dictionary& options);
  void RemoveBalloon();
  void Focus();
  void PopUpContextMenu(gin::Arguments* args);
  void CloseContextMenu();
  void SetContextMenu(gin_helper::ErrorThrower thrower,
                      v8::Local<v8::Value> arg);
  gfx::Rect GetBounds();

  bool CheckAlive();

  v8::Global<v8::Value> menu_;
  std::unique_ptr<TrayIcon> tray_icon_;
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_TRAY_H_
