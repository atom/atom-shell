// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_UI_WIN_ELECTRON_DESKTOP_WINDOW_TREE_HOST_WIN_H_
#define ELECTRON_SHELL_BROWSER_UI_WIN_ELECTRON_DESKTOP_WINDOW_TREE_HOST_WIN_H_

#include <windows.h>

#include <optional>

#include "shell/browser/native_window_views.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host_win.h"

namespace electron {

class ElectronDesktopWindowTreeHostWin : public views::DesktopWindowTreeHostWin,
                                         public ::ui::NativeThemeObserver {
 public:
  ElectronDesktopWindowTreeHostWin(
      NativeWindowViews* native_window_view,
      views::DesktopNativeWidgetAura* desktop_native_widget_aura);
  ~ElectronDesktopWindowTreeHostWin() override;

  // disable copy
  ElectronDesktopWindowTreeHostWin(const ElectronDesktopWindowTreeHostWin&) =
      delete;
  ElectronDesktopWindowTreeHostWin& operator=(
      const ElectronDesktopWindowTreeHostWin&) = delete;

 protected:
  // views::DesktopWindowTreeHostWin:
  bool PreHandleMSG(UINT message,
                    WPARAM w_param,
                    LPARAM l_param,
                    LRESULT* result) override;
  bool ShouldPaintAsActive() const override;
  bool GetDwmFrameInsetsInPixels(gfx::Insets* insets) const override;
  bool GetClientAreaInsets(gfx::Insets* insets,
                           HMONITOR monitor) const override;
  bool HandleMouseEventForCaption(UINT message) const override;

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;
  bool ShouldWindowContentsBeTransparent() const override;

 private:
  raw_ptr<NativeWindowViews> native_window_view_;  // weak ref
  std::optional<bool> force_should_paint_as_active_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_WIN_ELECTRON_DESKTOP_WINDOW_TREE_HOST_WIN_H_
