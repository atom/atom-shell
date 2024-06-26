// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_UI_VIEWS_ROOT_VIEW_H_
#define ELECTRON_SHELL_BROWSER_UI_VIEWS_ROOT_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "shell/browser/ui/accelerator_util.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"

namespace input {
struct NativeWebKeyboardEvent;
}

namespace electron {

class ElectronMenuModel;
class MenuBar;
class NativeWindow;

class RootView : public views::View {
 public:
  explicit RootView(NativeWindow* window);
  ~RootView() override;

  // disable copy
  RootView(const RootView&) = delete;
  RootView& operator=(const RootView&) = delete;

  void SetMenu(ElectronMenuModel* menu_model);
  bool HasMenu() const;
  int GetMenuBarHeight() const;
  void SetAutoHideMenuBar(bool auto_hide);
  bool is_menu_bar_auto_hide() const { return menu_bar_autohide_; }
  void SetMenuBarVisibility(bool visible);
  bool is_menu_bar_visible() const { return menu_bar_visible_; }
  void HandleKeyEvent(const input::NativeWebKeyboardEvent& event);
  void ResetAltState();
  void RestoreFocus();
  // Register/Unregister accelerators supported by the menu model.
  void RegisterAcceleratorsWithFocusManager(ElectronMenuModel* menu_model);
  void UnregisterAcceleratorsWithFocusManager();

  views::View* GetMainView() { return &main_view_.get(); }

  // views::View:
  gfx::Size GetMinimumSize() const override;
  gfx::Size GetMaximumSize() const override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;

 private:
  // Parent window, weak ref.
  const raw_ref<NativeWindow> window_;

  // Menu bar.
  std::unique_ptr<MenuBar> menu_bar_;
  bool menu_bar_autohide_ = false;
  bool menu_bar_visible_ = false;
  bool menu_bar_alt_pressed_ = false;

  // Main view area.
  const raw_ref<views::View> main_view_;

  // Map from accelerator to menu item's command id.
  accelerator_util::AcceleratorTable accelerator_table_;

  views::ViewTracker last_focused_view_tracker_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_VIEWS_ROOT_VIEW_H_
