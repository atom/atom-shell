// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef SHELL_BROWSER_UI_TRAY_ICON_GTK_H_
#define SHELL_BROWSER_UI_TRAY_ICON_GTK_H_

#include <memory>
#include <string>

#include "shell/browser/ui/tray_icon.h"
#include "ui/views/linux_ui/status_icon_linux.h"

namespace views {
class StatusIconLinux;
}

namespace electron {

class TrayIconGtk : public TrayIcon, public views::StatusIconLinux::Delegate {
 public:
  TrayIconGtk();
  ~TrayIconGtk() override;

  // TrayIcon:
  void SetImage(const gfx::Image& image) override;
  void SetToolTip(const std::string& tool_tip) override;
  void SetContextMenu(AtomMenuModel* menu_model) override;

  // views::StatusIconLinux::Delegate
  void OnClick() override;
  bool HasClickAction() override;
  // The following four methods are only used by StatusIconLinuxDbus, which we
  // aren't yet using, so they are given stub implementations.
  const gfx::ImageSkia& GetImage() const override;
  const base::string16& GetToolTip() const override;
  const ui::MenuModel* GetMenuModel() const override;
  void OnImplInitialized(bool success) override;

 private:
  std::unique_ptr<views::StatusIconLinux> icon_;

  gfx::ImageSkia dummy_image_;
  base::string16 dummy_string_;

  DISALLOW_COPY_AND_ASSIGN(TrayIconGtk);
};

}  // namespace electron

#endif  // SHELL_BROWSER_UI_TRAY_ICON_GTK_H_
