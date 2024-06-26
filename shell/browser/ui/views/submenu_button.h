// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_UI_VIEWS_SUBMENU_BUTTON_H_
#define ELECTRON_SHELL_BROWSER_UI_VIEWS_SUBMENU_BUTTON_H_

#include <string>

#include "ui/accessibility/ax_node_data.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/animation/ink_drop_highlight.h"
#include "ui/views/controls/button/menu_button.h"

namespace electron {

// Special button that used by menu bar to show submenus.
class SubmenuButton : public views::MenuButton {
  METADATA_HEADER(SubmenuButton, views::MenuButton)

 public:
  SubmenuButton(PressedCallback callback,
                const std::u16string& title,
                const SkColor& background_color);
  ~SubmenuButton() override;

  // disable copy
  SubmenuButton(const SubmenuButton&) = delete;
  SubmenuButton& operator=(const SubmenuButton&) = delete;

  void SetAcceleratorVisibility(bool visible);
  void SetUnderlineColor(SkColor color);

  char16_t accelerator() const { return accelerator_; }

  // views::MenuButton:
  void GetAccessibleNodeData(ui::AXNodeData* node_data) override;

  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  bool GetUnderlinePosition(const std::u16string& text,
                            char16_t* accelerator,
                            int* start,
                            int* end) const;
  void GetCharacterPosition(const std::u16string& text,
                            int index,
                            int* pos) const;

  char16_t accelerator_ = 0;

  bool show_underline_ = false;

  int underline_start_ = 0;
  int underline_end_ = 0;
  int text_width_ = 0;
  int text_height_ = 0;
  SkColor underline_color_ = SK_ColorBLACK;
  SkColor background_color_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_VIEWS_SUBMENU_BUTTON_H_
