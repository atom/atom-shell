// Copyright (c) 2021 Ryan Gonzalez.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.
// Portions of this file are sourced from
// chrome/browser/ui/views/frame/browser_desktop_window_tree_host_linux.h,
// Copyright (c) 2019 The Chromium Authors,
// which is governed by a BSD-style license

#ifndef ELECTRON_SHELL_BROWSER_UI_ELECTRON_DESKTOP_WINDOW_TREE_HOST_LINUX_H_
#define ELECTRON_SHELL_BROWSER_UI_ELECTRON_DESKTOP_WINDOW_TREE_HOST_LINUX_H_

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "ui/linux/device_scale_factor_observer.h"
#include "ui/linux/linux_ui.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/platform_window/platform_window.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host_linux.h"

namespace electron {

class ClientFrameViewLinux;
class NativeWindowViews;

class ElectronDesktopWindowTreeHostLinux
    : public views::DesktopWindowTreeHostLinux,
      private ui::NativeThemeObserver,
      private ui::DeviceScaleFactorObserver {
 public:
  ElectronDesktopWindowTreeHostLinux(
      NativeWindowViews* native_window_view,
      views::DesktopNativeWidgetAura* desktop_native_widget_aura);
  ~ElectronDesktopWindowTreeHostLinux() override;

  // disable copy
  ElectronDesktopWindowTreeHostLinux(
      const ElectronDesktopWindowTreeHostLinux&) = delete;
  ElectronDesktopWindowTreeHostLinux& operator=(
      const ElectronDesktopWindowTreeHostLinux&) = delete;

  bool SupportsClientFrameShadow() const;

 protected:
  // views::DesktopWindowTreeHostLinuxImpl:
  void OnWidgetInitDone() override;

  // ui::PlatformWindowDelegate
  gfx::Insets CalculateInsetsInDIP(
      ui::PlatformWindowState window_state) const override;
  void OnBoundsChanged(const BoundsChange& change) override;
  void OnWindowStateChanged(ui::PlatformWindowState old_state,
                            ui::PlatformWindowState new_state) override;
  void OnWindowTiledStateChanged(ui::WindowTiledEdges new_tiled_edges) override;

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

  // views::OnDeviceScaleFactorChanged:
  void OnDeviceScaleFactorChanged() override;

  // views::DesktopWindowTreeHostLinux:
  void UpdateFrameHints() override;

 private:
  void UpdateWindowState(ui::PlatformWindowState new_state);

  raw_ptr<NativeWindowViews> native_window_view_;  // weak ref

  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      theme_observation_{this};
  base::ScopedObservation<ui::LinuxUi, ui::DeviceScaleFactorObserver>
      scale_observation_{this};
  ui::PlatformWindowState window_state_ = ui::PlatformWindowState::kUnknown;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_ELECTRON_DESKTOP_WINDOW_TREE_HOST_LINUX_H_
