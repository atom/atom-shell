// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_MENU_MAC_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_MENU_MAC_H_

#include "shell/browser/api/electron_api_menu.h"

#include <map>

#import "shell/browser/ui/cocoa/electron_menu_controller.h"

namespace electron::api {

class MenuMac : public Menu {
 protected:
  explicit MenuMac(gin::Arguments* args);
  ~MenuMac() override;

  // Menu
  void PopupAt(BaseWindow* window,
               int x,
               int y,
               int positioning_item,
               ui::MenuSourceType source_type,
               base::OnceClosure callback) override;
  void PopupOnUI(const base::WeakPtr<NativeWindow>& native_window,
                 int32_t window_id,
                 int x,
                 int y,
                 int positioning_item,
                 base::OnceClosure callback);
  void ClosePopupAt(int32_t window_id) override;
  std::u16string GetAcceleratorTextAtForTesting(int index) const override;

 private:
  friend class Menu;

  void ClosePopupOnUI(int32_t window_id);
  void OnClosed(int32_t window_id, base::OnceClosure callback);

  ElectronMenuController* __strong menu_controller_;

  // window ID -> open context menu
  std::map<int32_t, ElectronMenuController*> popup_controllers_;

  base::WeakPtrFactory<MenuMac> weak_factory_{this};
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_MENU_MAC_H_
