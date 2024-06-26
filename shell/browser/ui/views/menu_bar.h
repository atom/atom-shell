// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_UI_VIEWS_MENU_BAR_H_
#define ELECTRON_SHELL_BROWSER_UI_VIEWS_MENU_BAR_H_

#include "base/memory/raw_ptr.h"
#include "shell/browser/native_window_observer.h"
#include "shell/browser/ui/electron_menu_model.h"
#include "shell/browser/ui/views/menu_delegate.h"
#include "shell/browser/ui/views/root_view.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/accessible_pane_view.h"

namespace views {
class MenuButton;
}

namespace electron {

class MenuBar : public views::AccessiblePaneView,
                private MenuDelegate::Observer,
                private NativeWindowObserver {
  METADATA_HEADER(MenuBar, views::AccessiblePaneView)

 public:
  MenuBar(NativeWindow* window, RootView* root_view);
  ~MenuBar() override;

  // disable copy
  MenuBar(const MenuBar&) = delete;
  MenuBar& operator=(const MenuBar&) = delete;

  // Replaces current menu with a new one.
  void SetMenu(ElectronMenuModel* menu_model);

  // Shows underline under accelerators.
  void SetAcceleratorVisibility(bool visible);

  // Returns true if the submenu has accelerator |key|
  bool HasAccelerator(char16_t key);

  // Shows the submenu whose accelerator is |key|.
  void ActivateAccelerator(char16_t key);

  // Returns there are how many items in the root menu.
  size_t GetItemCount() const;

  // Get the menu under specified screen point.
  bool GetMenuButtonFromScreenPoint(const gfx::Point& point,
                                    ElectronMenuModel** menu_model,
                                    views::MenuButton** button);

  void ViewHierarchyChanged(
      const views::ViewHierarchyChangedDetails& details) override;

 private:
  // MenuDelegate::Observer:
  void OnBeforeExecuteCommand() override;
  void OnMenuClosed() override;

  // NativeWindowObserver:
  void OnWindowBlur() override;
  void OnWindowFocus() override;

  // views::AccessiblePaneView:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool SetPaneFocusAndFocusDefault() override;
  void OnThemeChanged() override;

  // views::FocusChangeListener:
  void OnDidChangeFocus(View* focused_before, View* focused_now) override;

  // views::View:
  void GetAccessibleNodeData(ui::AXNodeData* node_data) override;

  void ButtonPressed(size_t id, const ui::Event& event);

  void RebuildChildren();
  void UpdateViewColors();
  void RefreshColorCache(const ui::NativeTheme* theme);
  View* FindAccelChild(char16_t key);

  SkColor background_color_;
#if BUILDFLAG(IS_LINUX)
  SkColor enabled_color_;
  SkColor disabled_color_;
#endif

  raw_ptr<NativeWindow> window_;
  raw_ptr<RootView> root_view_;
  raw_ptr<ElectronMenuModel> menu_model_ = nullptr;
  bool accelerator_installed_ = false;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_VIEWS_MENU_BAR_H_
