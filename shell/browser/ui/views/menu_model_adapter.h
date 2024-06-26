// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_UI_VIEWS_MENU_MODEL_ADAPTER_H_
#define ELECTRON_SHELL_BROWSER_UI_VIEWS_MENU_MODEL_ADAPTER_H_

#include "base/memory/raw_ptr.h"
#include "shell/browser/ui/electron_menu_model.h"
#include "ui/views/controls/menu/menu_model_adapter.h"

namespace electron {

class MenuModelAdapter : public views::MenuModelAdapter {
 public:
  explicit MenuModelAdapter(ElectronMenuModel* menu_model);
  ~MenuModelAdapter() override;

  // disable copy
  MenuModelAdapter(const MenuModelAdapter&) = delete;
  MenuModelAdapter& operator=(const MenuModelAdapter&) = delete;

 protected:
  // views::MenuModelAdapter
  bool GetAccelerator(int id, ui::Accelerator* accelerator) const override;

 private:
  raw_ptr<ElectronMenuModel> menu_model_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_VIEWS_MENU_MODEL_ADAPTER_H_
