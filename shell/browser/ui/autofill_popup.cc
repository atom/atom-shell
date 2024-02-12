// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <memory>
#include <vector>

#include "base/feature_list.h"
#include "base/i18n/rtl.h"
#include "components/autofill/core/common/autofill_features.h"
#include "electron/buildflags/buildflags.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "shell/browser/native_window_views.h"
#include "shell/browser/osr/osr_render_widget_host_view.h"
#include "shell/browser/osr/osr_view_proxy.h"
#include "shell/browser/ui/autofill_popup.h"
#include "shell/common/api/api.mojom.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/text_utils.h"

namespace electron {

namespace {

void CalculatePopupXAndWidthHorizontallyCentered(
    int popup_preferred_width,
    const gfx::Rect& content_area_bounds,
    const gfx::Rect& element_bounds,
    bool is_rtl,
    gfx::Rect* popup_bounds) {
  // The preferred horizontal starting point for the pop-up is at the horizontal
  // center of the field.
  int preferred_starting_point =
      std::clamp(element_bounds.x() + (element_bounds.size().width() / 2),
                 content_area_bounds.x(), content_area_bounds.right());

  // The space available to the left and to the right.
  int space_to_right = content_area_bounds.right() - preferred_starting_point;
  int space_to_left = preferred_starting_point - content_area_bounds.x();

  // Calculate the pop-up width. This is either the preferred pop-up width, or
  // alternatively the maximum space available if there is not sufficient space
  // for the preferred width.
  int popup_width =
      std::min(popup_preferred_width, space_to_left + space_to_right);

  // Calculates the space that is available to grow into the preferred
  // direction. In RTL, this is the space to the right side of the content
  // area, in LTR this is the space to the left side of the content area.
  int space_to_grow_in_preferred_direction =
      is_rtl ? space_to_left : space_to_right;

  // Calculate how much the pop-up needs to grow into the non-preferred
  // direction.
  int amount_to_grow_in_unpreferred_direction =
      std::max(0, popup_width - space_to_grow_in_preferred_direction);

  popup_bounds->set_width(popup_width);
  if (is_rtl) {
    // Note, in RTL the |pop_up_width| must be subtracted to achieve
    // right-alignment of the pop-up with the element.
    popup_bounds->set_x(preferred_starting_point - popup_width +
                        amount_to_grow_in_unpreferred_direction);
  } else {
    popup_bounds->set_x(preferred_starting_point -
                        amount_to_grow_in_unpreferred_direction);
  }
}

void CalculatePopupXAndWidth(int popup_preferred_width,
                             const gfx::Rect& content_area_bounds,
                             const gfx::Rect& element_bounds,
                             bool is_rtl,
                             gfx::Rect* popup_bounds) {
  int right_growth_start = std::clamp(
      element_bounds.x(), content_area_bounds.x(), content_area_bounds.right());
  int left_growth_end =
      std::clamp(element_bounds.right(), content_area_bounds.x(),
                 content_area_bounds.right());

  int right_available = content_area_bounds.right() - right_growth_start;
  int left_available = left_growth_end - content_area_bounds.x();

  int popup_width = std::min(popup_preferred_width,
                             std::max(left_available, right_available));

  // Prefer to grow towards the end (right for LTR, left for RTL). But if there
  // is not enough space available in the desired direction and more space in
  // the other direction, reverse it.
  bool grow_left = false;
  if (is_rtl) {
    grow_left =
        left_available >= popup_width || left_available >= right_available;
  } else {
    grow_left =
        right_available < popup_width && right_available < left_available;
  }

  popup_bounds->set_width(popup_width);
  popup_bounds->set_x(grow_left ? left_growth_end - popup_width
                                : right_growth_start);
}

void CalculatePopupYAndHeight(int popup_preferred_height,
                              const gfx::Rect& content_area_bounds,
                              const gfx::Rect& element_bounds,
                              gfx::Rect* popup_bounds) {
  int top_growth_end = std::clamp(element_bounds.y(), content_area_bounds.y(),
                                  content_area_bounds.bottom());
  int bottom_growth_start =
      std::clamp(element_bounds.bottom(), content_area_bounds.y(),
                 content_area_bounds.bottom());

  int top_available = top_growth_end - content_area_bounds.y();
  int bottom_available = content_area_bounds.bottom() - bottom_growth_start;

  popup_bounds->set_height(popup_preferred_height);
  popup_bounds->set_y(top_growth_end);

  if (bottom_available >= popup_preferred_height ||
      bottom_available >= top_available) {
    popup_bounds->AdjustToFit(
        gfx::Rect(popup_bounds->x(), element_bounds.bottom(),
                  popup_bounds->width(), bottom_available));
  } else {
    popup_bounds->AdjustToFit(gfx::Rect(popup_bounds->x(),
                                        content_area_bounds.y(),
                                        popup_bounds->width(), top_available));
  }
}

gfx::Rect CalculatePopupBounds(const gfx::Size& desired_size,
                               const gfx::Rect& content_area_bounds,
                               const gfx::Rect& element_bounds,
                               bool is_rtl,
                               bool horizontally_centered) {
  gfx::Rect popup_bounds;

  if (horizontally_centered) {
    CalculatePopupXAndWidthHorizontallyCentered(
        desired_size.width(), content_area_bounds, element_bounds, is_rtl,
        &popup_bounds);
  } else {
    CalculatePopupXAndWidth(desired_size.width(), content_area_bounds,
                            element_bounds, is_rtl, &popup_bounds);
  }
  CalculatePopupYAndHeight(desired_size.height(), content_area_bounds,
                           element_bounds, &popup_bounds);

  return popup_bounds;
}

}  // namespace

AutofillPopup::AutofillPopup() {
  bold_font_list_ = gfx::FontList().DeriveWithWeight(gfx::Font::Weight::BOLD);
  smaller_font_list_ =
      gfx::FontList().DeriveWithSizeDelta(kSmallerFontSizeDelta);
}

AutofillPopup::~AutofillPopup() {
  Hide();
}

void AutofillPopup::CreateView(content::RenderFrameHost* frame_host,
                               content::RenderFrameHost* embedder_frame_host,
                               bool offscreen,
                               views::View* parent,
                               const gfx::RectF& r) {
  Hide();

  frame_host_ = frame_host;
  element_bounds_ = gfx::ToEnclosedRect(r);

  gfx::Vector2d height_offset(0, element_bounds_.height());
  gfx::Point menu_position(element_bounds_.origin() + height_offset);
  views::View::ConvertPointToScreen(parent, &menu_position);
  popup_bounds_ = gfx::Rect(menu_position, element_bounds_.size());

  parent_ = parent;
  parent_->AddObserver(this);

  view_ = new AutofillPopupView(this, parent->GetWidget());

  if (offscreen) {
    auto* rwhv = frame_host->GetView();
    if (embedder_frame_host != nullptr) {
      rwhv = embedder_frame_host->GetView();
    }

    auto* osr_rwhv = static_cast<OffScreenRenderWidgetHostView*>(rwhv);
    view_->view_proxy_ = std::make_unique<OffscreenViewProxy>(view_);
    osr_rwhv->AddViewProxy(view_->view_proxy_.get());
  }

  // Do this after OSR setup, we check for view_proxy_ when showing
  view_->Show();
}

void AutofillPopup::Hide() {
  if (parent_) {
    parent_->RemoveObserver(this);
    parent_ = nullptr;
  }
  if (view_) {
    view_->Hide();
    view_ = nullptr;
  }
}

void AutofillPopup::SetItems(const std::vector<std::u16string>& values,
                             const std::vector<std::u16string>& labels) {
  DCHECK(view_);
  values_ = values;
  labels_ = labels;
  UpdatePopupBounds();
  view_->OnSuggestionsChanged();
  if (view_)  // could be hidden after the change
    view_->DoUpdateBoundsAndRedrawPopup();
}

void AutofillPopup::AcceptSuggestion(int index) {
  mojo::AssociatedRemote<mojom::ElectronAutofillAgent> autofill_agent;
  frame_host_->GetRemoteAssociatedInterfaces()->GetInterface(&autofill_agent);
  autofill_agent->AcceptDataListSuggestion(value_at(index));
}

void AutofillPopup::UpdatePopupBounds() {
  DCHECK(parent_);
  gfx::Point origin(element_bounds_.origin());
  views::View::ConvertPointToScreen(parent_, &origin);

  gfx::Rect bounds(origin, element_bounds_.size());
  gfx::Size preferred_size =
      gfx::Size(GetDesiredPopupWidth(), GetDesiredPopupHeight());

  popup_bounds_ =
      CalculatePopupBounds(preferred_size, parent_->GetBoundsInScreen(), bounds,
                           base::i18n::IsRTL(), false);
}

gfx::Rect AutofillPopup::popup_bounds_in_view() {
  gfx::Point origin(popup_bounds_.origin());
  views::View::ConvertPointFromScreen(parent_, &origin);

  return gfx::Rect(origin, popup_bounds_.size());
}

void AutofillPopup::OnViewBoundsChanged(views::View* view) {
  UpdatePopupBounds();
  view_->DoUpdateBoundsAndRedrawPopup();
}

void AutofillPopup::OnViewIsDeleting(views::View* view) {
  Hide();
}

int AutofillPopup::GetDesiredPopupHeight() {
  return 2 * kPopupBorderThickness + values_.size() * kRowHeight;
}

int AutofillPopup::GetDesiredPopupWidth() {
  int popup_width = element_bounds_.width();

  for (size_t i = 0; i < values_.size(); ++i) {
    int row_size = kEndPadding + 2 * kPopupBorderThickness +
                   gfx::GetStringWidth(value_at(i), GetValueFontListForRow(i)) +
                   gfx::GetStringWidth(label_at(i), GetLabelFontListForRow(i));
    if (!label_at(i).empty())
      row_size += kNamePadding + kEndPadding;

    popup_width = std::max(popup_width, row_size);
  }

  return popup_width;
}

gfx::Rect AutofillPopup::GetRowBounds(int index) {
  int top = kPopupBorderThickness + index * kRowHeight;

  return gfx::Rect(kPopupBorderThickness, top,
                   popup_bounds_.width() - 2 * kPopupBorderThickness,
                   kRowHeight);
}

const gfx::FontList& AutofillPopup::GetValueFontListForRow(int index) const {
  return bold_font_list_;
}

const gfx::FontList& AutofillPopup::GetLabelFontListForRow(int index) const {
  return smaller_font_list_;
}

ui::ColorId AutofillPopup::GetBackgroundColorIDForRow(int index) const {
  return (view_ && index == view_->GetSelectedLine())
             ? ui::kColorResultsTableHoveredBackground
             : ui::kColorResultsTableNormalBackground;
}

int AutofillPopup::LineFromY(int y) const {
  int current_height = kPopupBorderThickness;

  for (size_t i = 0; i < values_.size(); ++i) {
    current_height += kRowHeight;

    if (y <= current_height)
      return i;
  }

  return values_.size() - 1;
}

}  // namespace electron
