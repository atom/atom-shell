// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Modified from
// chrome/browser/ui/views/frame/glass_browser_caption_button_container.cc

#include "shell/browser/ui/views/win_caption_button_container.h"

#include <memory>
#include <utility>

#include "shell/browser/ui/views/win_caption_button.h"
#include "shell/browser/ui/views/win_frame_view.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/compositor/layer.h"
#include "ui/strings/grit/ui_strings.h"
#include "ui/views/background.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/view_class_properties.h"

namespace electron {

namespace {

std::unique_ptr<WinCaptionButton> CreateCaptionButton(
    views::Button::PressedCallback callback,
    WinFrameView* frame_view,
    ViewID button_type,
    int accessible_name_resource_id) {
  return std::make_unique<WinCaptionButton>(
      std::move(callback), frame_view, button_type,
      l10n_util::GetStringUTF16(accessible_name_resource_id));
}

bool HitTestCaptionButton(WinCaptionButton* button, const gfx::Point& point) {
  return button && button->GetVisible() && button->bounds().Contains(point);
}

}  // anonymous namespace

WinCaptionButtonContainer::WinCaptionButtonContainer(WinFrameView* frame_view)
    : frame_view_(frame_view),
      minimize_button_(AddChildView(CreateCaptionButton(
          base::BindRepeating(&views::Widget::Minimize,
                              base::Unretained(frame_view_->frame())),
          frame_view_,
          VIEW_ID_MINIMIZE_BUTTON,
          IDS_APP_ACCNAME_MINIMIZE))),
      maximize_button_(AddChildView(CreateCaptionButton(
          base::BindRepeating(&views::Widget::Maximize,
                              base::Unretained(frame_view_->frame())),
          frame_view_,
          VIEW_ID_MAXIMIZE_BUTTON,
          IDS_APP_ACCNAME_MAXIMIZE))),
      restore_button_(AddChildView(CreateCaptionButton(
          base::BindRepeating(&views::Widget::Restore,
                              base::Unretained(frame_view_->frame())),
          frame_view_,
          VIEW_ID_RESTORE_BUTTON,
          IDS_APP_ACCNAME_RESTORE))),
      close_button_(AddChildView(CreateCaptionButton(
          base::BindRepeating(&views::Widget::CloseWithReason,
                              base::Unretained(frame_view_->frame()),
                              views::Widget::ClosedReason::kCloseButtonClicked),
          frame_view_,
          VIEW_ID_CLOSE_BUTTON,
          IDS_APP_ACCNAME_CLOSE))) {
  // Layout is horizontal, with buttons placed at the trailing end of the view.
  // This allows the container to expand to become a faux titlebar/drag handle.
  auto* const layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetMainAxisAlignment(views::LayoutAlignment::kEnd)
      .SetCrossAxisAlignment(views::LayoutAlignment::kStart)
      .SetDefault(
          views::kFlexBehaviorKey,
          views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                                   views::MinimumFlexSizeRule::kPreferred,
                                   views::MaximumFlexSizeRule::kPreferred,
                                   /* adjust_width_for_height */ false,
                                   views::MinimumFlexSizeRule::kScaleToZero));
}

WinCaptionButtonContainer::~WinCaptionButtonContainer() {}

int WinCaptionButtonContainer::NonClientHitTest(const gfx::Point& point) const {
  DCHECK(HitTestPoint(point))
      << "should only be called with a point inside this view's bounds";
  if (HitTestCaptionButton(minimize_button_, point)) {
    return HTMINBUTTON;
  }
  if (HitTestCaptionButton(maximize_button_, point)) {
    return HTMAXBUTTON;
  }
  if (HitTestCaptionButton(restore_button_, point)) {
    return HTMAXBUTTON;
  }
  if (HitTestCaptionButton(close_button_, point)) {
    return HTCLOSE;
  }
  return HTCAPTION;
}

gfx::Size WinCaptionButtonContainer::GetButtonSize() const {
  // Close button size is set the same as all the buttons
  return close_button_->GetSize();
}

void WinCaptionButtonContainer::SetButtonSize(gfx::Size size) {
  minimize_button_->SetSize(size);
  maximize_button_->SetSize(size);
  restore_button_->SetSize(size);
  close_button_->SetSize(size);
}

void WinCaptionButtonContainer::ResetWindowControls() {
  minimize_button_->SetState(views::Button::STATE_NORMAL);
  maximize_button_->SetState(views::Button::STATE_NORMAL);
  restore_button_->SetState(views::Button::STATE_NORMAL);
  close_button_->SetState(views::Button::STATE_NORMAL);
  InvalidateLayout();
}

void WinCaptionButtonContainer::AddedToWidget() {
  views::Widget* const widget = GetWidget();

  DCHECK(!widget_observation_.IsObserving());
  widget_observation_.Observe(widget);

  UpdateButtons();

  if (frame_view_->window()->IsWindowControlsOverlayEnabled()) {
    UpdateBackground();
  }
}

void WinCaptionButtonContainer::RemovedFromWidget() {
  DCHECK(widget_observation_.IsObserving());
  widget_observation_.Reset();
}

void WinCaptionButtonContainer::OnWidgetBoundsChanged(
    views::Widget* widget,
    const gfx::Rect& new_bounds) {
  UpdateButtons();
}

void WinCaptionButtonContainer::UpdateBackground() {
  const SkColor bg_color = frame_view_->window()->overlay_button_color();
  const SkAlpha theme_alpha = SkColorGetA(bg_color);
  SetBackground(views::CreateSolidBackground(bg_color));
  SetPaintToLayer();

  if (theme_alpha < SK_AlphaOPAQUE)
    layer()->SetFillsBoundsOpaquely(false);
}

void WinCaptionButtonContainer::UpdateButtons() {
  const bool minimizable = frame_view_->window()->IsMinimizable();
  minimize_button_->SetEnabled(minimizable);
  minimize_button_->SetVisible(minimizable);

  const bool is_maximized = frame_view_->frame()->IsMaximized();
  const bool maximizable = frame_view_->window()->IsMaximizable();
  restore_button_->SetVisible(is_maximized && maximizable);
  maximize_button_->SetVisible(!is_maximized && maximizable);

  // In touch mode, windows cannot be taken out of fullscreen or tiled mode, so
  // the maximize/restore button should be disabled, unless the window is not
  // maximized.
  const bool is_touch = ui::TouchUiController::Get()->touch_ui();
  restore_button_->SetEnabled(!is_touch);
  maximize_button_->SetEnabled(!is_touch || !is_maximized);

  // If the window isn't closable, the close button should be disabled.
  const bool closable = frame_view_->window()->IsClosable();
  close_button_->SetEnabled(closable);

  InvalidateLayout();
}

BEGIN_METADATA(WinCaptionButtonContainer)
END_METADATA

}  // namespace electron
