// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_UI_AUTOFILL_POPUP_H_
#define ELECTRON_SHELL_BROWSER_UI_AUTOFILL_POPUP_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/render_frame_host.h"
#include "shell/browser/ui/views/autofill_popup_view.h"
#include "ui/color/color_id.h"
#include "ui/gfx/font_list.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace electron {

class AutofillPopupView;

class AutofillPopup : private views::ViewObserver {
 public:
  AutofillPopup();
  ~AutofillPopup() override;

  // disable copy
  AutofillPopup(const AutofillPopup&) = delete;
  AutofillPopup& operator=(const AutofillPopup&) = delete;

  void CreateView(content::RenderFrameHost* render_frame,
                  content::RenderFrameHost* embedder_frame,
                  bool offscreen,
                  views::View* parent,
                  const gfx::RectF& bounds);
  void Hide();

  void SetItems(const std::vector<std::u16string>& values,
                const std::vector<std::u16string>& labels);
  void UpdatePopupBounds();

  gfx::Rect popup_bounds_in_view();

 private:
  friend class AutofillPopupView;

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* view) override;
  void OnViewIsDeleting(views::View* view) override;

  void AcceptSuggestion(int index);

  int GetDesiredPopupHeight();
  int GetDesiredPopupWidth();
  gfx::Rect GetRowBounds(int i);
  const gfx::FontList& GetValueFontListForRow(int index) const;
  const gfx::FontList& GetLabelFontListForRow(int index) const;
  ui::ColorId GetBackgroundColorIDForRow(int index) const;

  int line_count() const { return values_.size(); }
  const std::u16string& value_at(int i) const { return values_.at(i); }
  const std::u16string& label_at(int i) const { return labels_.at(i); }
  int LineFromY(int y) const;

  int selected_index_;

  // Popup location
  gfx::Rect popup_bounds_;

  // Bounds of the autofilled element
  gfx::Rect element_bounds_;

  // Datalist suggestions
  std::vector<std::u16string> values_;
  std::vector<std::u16string> labels_;

  // Font lists for the suggestions
  gfx::FontList smaller_font_list_;
  gfx::FontList bold_font_list_;

  // For sending the accepted suggestion to the render frame that
  // asked to open the popup
  raw_ptr<content::RenderFrameHost> frame_host_ = nullptr;

  // The popup view. The lifetime is managed by the owning Widget
  raw_ptr<AutofillPopupView> view_ = nullptr;

  // The parent view that the popup view shows on. Weak ref.
  raw_ptr<views::View> parent_ = nullptr;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_AUTOFILL_POPUP_H_
