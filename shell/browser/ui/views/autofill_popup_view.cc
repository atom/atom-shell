// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/browser/ui/views/autofill_popup_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "cc/paint/skia_paint_canvas.h"
#include "content/public/browser/render_view_host.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/color/color_provider.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/border.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"

namespace electron {

void AutofillPopupChildView::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  node_data->role = ax::mojom::Role::kMenuItem;
  node_data->SetName(suggestion_);
}

BEGIN_METADATA(AutofillPopupChildView)
END_METADATA

AutofillPopupView::AutofillPopupView(AutofillPopup* popup,
                                     views::Widget* parent_widget)
    : popup_(popup), parent_widget_(parent_widget) {
  CreateChildViews();
  SetFocusBehavior(FocusBehavior::ALWAYS);
  set_drag_controller(this);
}

AutofillPopupView::~AutofillPopupView() {
  if (popup_) {
    auto* host = popup_->frame_host_->GetRenderViewHost()->GetWidget();
    host->RemoveKeyPressEventCallback(keypress_callback_);
    popup_->view_ = nullptr;
    popup_ = nullptr;
  }

  RemoveObserver();

  if (view_proxy_.get()) {
    view_proxy_->ResetView();
  }

  if (GetWidget()) {
    GetWidget()->Close();
  }
}

void AutofillPopupView::Show() {
  bool visible = parent_widget_->IsVisible();
  visible = visible || view_proxy_;
  if (!popup_ || !visible || parent_widget_->IsClosed())
    return;

  const bool initialize_widget = !GetWidget();
  if (initialize_widget) {
    parent_widget_->AddObserver(this);

    // The widget is destroyed by the corresponding NativeWidget, so we use
    // a weak pointer to hold the reference and don't have to worry about
    // deletion.
    auto* widget = new views::Widget;
    views::Widget::InitParams params(views::Widget::InitParams::TYPE_POPUP);
    params.delegate = this;
    params.parent = parent_widget_->GetNativeView();
    params.z_order = ui::ZOrderLevel::kFloatingUIElement;
    widget->Init(std::move(params));

    // No animation for popup appearance (too distracting).
    widget->SetVisibilityAnimationTransition(views::Widget::ANIMATE_HIDE);

    show_time_ = base::Time::Now();
  }

  SetBorder(views::CreateSolidBorder(
      kPopupBorderThickness,
      GetColorProvider()->GetColor(ui::kColorUnfocusedBorder)));

  DoUpdateBoundsAndRedrawPopup();
  GetWidget()->Show();

  if (initialize_widget)
    views::WidgetFocusManager::GetInstance()->AddFocusChangeListener(this);

  keypress_callback_ = base::BindRepeating(
      &AutofillPopupView::HandleKeyPressEvent, base::Unretained(this));
  auto* host = popup_->frame_host_->GetRenderViewHost()->GetWidget();
  host->AddKeyPressEventCallback(keypress_callback_);

  NotifyAccessibilityEvent(ax::mojom::Event::kMenuStart, true);
}

void AutofillPopupView::Hide() {
  if (popup_) {
    auto* host = popup_->frame_host_->GetRenderViewHost()->GetWidget();
    host->RemoveKeyPressEventCallback(keypress_callback_);
    popup_ = nullptr;
  }

  RemoveObserver();
  NotifyAccessibilityEvent(ax::mojom::Event::kMenuEnd, true);

  if (GetWidget()) {
    GetWidget()->Close();
  }
}

void AutofillPopupView::OnSuggestionsChanged() {
  if (!popup_)
    return;

  CreateChildViews();
  if (popup_->line_count() == 0) {
    popup_->Hide();
    return;
  }
  DoUpdateBoundsAndRedrawPopup();
}

void AutofillPopupView::WriteDragDataForView(views::View*,
                                             const gfx::Point&,
                                             ui::OSExchangeData*) {}

int AutofillPopupView::GetDragOperationsForView(views::View*,
                                                const gfx::Point&) {
  return ui::DragDropTypes::DRAG_NONE;
}

bool AutofillPopupView::CanStartDragForView(views::View*,
                                            const gfx::Point&,
                                            const gfx::Point&) {
  return false;
}

void AutofillPopupView::OnSelectedRowChanged(
    std::optional<int> previous_row_selection,
    std::optional<int> current_row_selection) {
  SchedulePaint();

  if (current_row_selection) {
    int selected = current_row_selection.value_or(-1);
    if (selected == -1 || static_cast<size_t>(selected) >= children().size())
      return;
    children().at(selected)->NotifyAccessibilityEvent(
        ax::mojom::Event::kSelection, true);
  }
}

void AutofillPopupView::DrawAutofillEntry(gfx::Canvas* canvas,
                                          int index,
                                          const gfx::Rect& entry_rect) {
  if (!popup_)
    return;

  canvas->FillRect(entry_rect, GetColorProvider()->GetColor(
                                   popup_->GetBackgroundColorIDForRow(index)));

  const bool is_rtl = base::i18n::IsRTL();
  const int text_align =
      is_rtl ? gfx::Canvas::TEXT_ALIGN_RIGHT : gfx::Canvas::TEXT_ALIGN_LEFT;
  gfx::Rect value_rect = entry_rect;
  value_rect.Inset(gfx::Insets::VH(0, kEndPadding));

  int x_align_left = value_rect.x();
  const int value_width = gfx::GetStringWidth(
      popup_->value_at(index), popup_->GetValueFontListForRow(index));
  int value_x_align_left = x_align_left;
  value_x_align_left =
      is_rtl ? value_rect.right() - value_width : value_rect.x();

  canvas->DrawStringRectWithFlags(
      popup_->value_at(index), popup_->GetValueFontListForRow(index),
      GetColorProvider()->GetColor(ui::kColorResultsTableNormalText),
      gfx::Rect(value_x_align_left, value_rect.y(), value_width,
                value_rect.height()),
      text_align);

  // Draw the label text, if one exists.
  if (auto const& label = popup_->label_at(index); !label.empty()) {
    const int label_width =
        gfx::GetStringWidth(label, popup_->GetLabelFontListForRow(index));
    int label_x_align_left = x_align_left;
    label_x_align_left =
        is_rtl ? value_rect.x() : value_rect.right() - label_width;

    canvas->DrawStringRectWithFlags(
        label, popup_->GetLabelFontListForRow(index),
        GetColorProvider()->GetColor(ui::kColorResultsTableDimmedText),
        gfx::Rect(label_x_align_left, entry_rect.y(), label_width,
                  entry_rect.height()),
        text_align);
  }
}

void AutofillPopupView::CreateChildViews() {
  if (!popup_)
    return;

  RemoveAllChildViews();

  for (int i = 0; i < popup_->line_count(); ++i) {
    auto* child_view = new AutofillPopupChildView(popup_->value_at(i));
    child_view->set_drag_controller(this);
    AddChildView(child_view);
  }
}

void AutofillPopupView::DoUpdateBoundsAndRedrawPopup() {
  if (!popup_)
    return;

  // Clamp popup_bounds_ to ensure it's never zero-width.
  popup_->popup_bounds_.Union(
      gfx::Rect(popup_->popup_bounds_.origin(), gfx::Size(1, 1)));
  GetWidget()->SetBounds(popup_->popup_bounds_);
  if (view_proxy_.get()) {
    view_proxy_->SetBounds(popup_->popup_bounds_in_view());
  }
  SchedulePaint();
}

void AutofillPopupView::OnPaint(gfx::Canvas* canvas) {
  if (!popup_ || static_cast<size_t>(popup_->line_count()) != children().size())
    return;
  gfx::Canvas* draw_canvas = canvas;
  SkBitmap bitmap;

  std::unique_ptr<cc::SkiaPaintCanvas> paint_canvas;
  if (view_proxy_.get()) {
    bitmap.allocN32Pixels(popup_->popup_bounds_in_view().width(),
                          popup_->popup_bounds_in_view().height(), true);
    paint_canvas = std::make_unique<cc::SkiaPaintCanvas>(bitmap);
    draw_canvas = new gfx::Canvas(paint_canvas.get(), 1.0);
  }

  draw_canvas->DrawColor(
      GetColorProvider()->GetColor(ui::kColorResultsTableNormalBackground));
  OnPaintBorder(draw_canvas);

  for (int i = 0; i < popup_->line_count(); ++i) {
    gfx::Rect line_rect = popup_->GetRowBounds(i);

    DrawAutofillEntry(draw_canvas, i, line_rect);
  }

  if (view_proxy_.get()) {
    view_proxy_->SetBounds(popup_->popup_bounds_in_view());
    view_proxy_->SetBitmap(bitmap);
  }
}

void AutofillPopupView::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  node_data->role = ax::mojom::Role::kMenu;
  node_data->SetName("Autofill Menu");
}

void AutofillPopupView::OnMouseCaptureLost() {
  ClearSelection();
}

bool AutofillPopupView::OnMouseDragged(const ui::MouseEvent& event) {
  if (HitTestPoint(event.location())) {
    SetSelection(event.location());

    // We must return true in order to get future OnMouseDragged and
    // OnMouseReleased events.
    return true;
  }

  // If we move off of the popup, we lose the selection.
  ClearSelection();
  return false;
}

void AutofillPopupView::OnMouseExited(const ui::MouseEvent& event) {
  // Pressing return causes the cursor to hide, which will generate an
  // OnMouseExited event. Pressing return should activate the current selection
  // via AcceleratorPressed, so we need to let that run first.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&AutofillPopupView::ClearSelection,
                                weak_ptr_factory_.GetWeakPtr()));
}

void AutofillPopupView::OnMouseMoved(const ui::MouseEvent& event) {
  // A synthesized mouse move will be sent when the popup is first shown.
  // Don't preview a suggestion if the mouse happens to be hovering there.
#if BUILDFLAG(IS_WIN)
  if (base::Time::Now() - show_time_ <= base::Milliseconds(50))
    return;
#else
  if (event.flags() & ui::EF_IS_SYNTHESIZED)
    return;
#endif

  if (HitTestPoint(event.location()))
    SetSelection(event.location());
  else
    ClearSelection();
}

bool AutofillPopupView::OnMousePressed(const ui::MouseEvent& event) {
  return event.GetClickCount() == 1;
}

void AutofillPopupView::OnMouseReleased(const ui::MouseEvent& event) {
  // We only care about the left click.
  if (event.IsOnlyLeftMouseButton() && HitTestPoint(event.location()))
    AcceptSelection(event.location());
}

void AutofillPopupView::OnGestureEvent(ui::GestureEvent* event) {
  switch (event->type()) {
    case ui::ET_GESTURE_TAP_DOWN:
    case ui::ET_GESTURE_SCROLL_BEGIN:
    case ui::ET_GESTURE_SCROLL_UPDATE:
      if (HitTestPoint(event->location()))
        SetSelection(event->location());
      else
        ClearSelection();
      break;
    case ui::ET_GESTURE_TAP:
    case ui::ET_GESTURE_SCROLL_END:
      if (HitTestPoint(event->location()))
        AcceptSelection(event->location());
      else
        ClearSelection();
      break;
    case ui::ET_GESTURE_TAP_CANCEL:
    case ui::ET_SCROLL_FLING_START:
      ClearSelection();
      break;
    default:
      return;
  }
  event->SetHandled();
}

bool AutofillPopupView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  if (accelerator.modifiers() != ui::EF_NONE)
    return false;

  if (accelerator.key_code() == ui::VKEY_ESCAPE) {
    if (popup_)
      popup_->Hide();
    return true;
  }

  if (accelerator.key_code() == ui::VKEY_RETURN)
    return AcceptSelectedLine();

  return false;
}

bool AutofillPopupView::HandleKeyPressEvent(
    const input::NativeWebKeyboardEvent& event) {
  if (!popup_)
    return false;
  switch (event.windows_key_code) {
    case ui::VKEY_UP:
      SelectPreviousLine();
      return true;
    case ui::VKEY_DOWN:
      SelectNextLine();
      return true;
    case ui::VKEY_PRIOR:  // Page up.
      SetSelectedLine(0);
      return true;
    case ui::VKEY_NEXT:  // Page down.
      SetSelectedLine(popup_->line_count() - 1);
      return true;
    case ui::VKEY_ESCAPE:
      popup_->Hide();
      return true;
    case ui::VKEY_TAB:
      // A tab press should cause the selected line to be accepted, but still
      // return false so the tab key press propagates and changes the cursor
      // location.
      AcceptSelectedLine();
      return false;
    case ui::VKEY_RETURN:
      return AcceptSelectedLine();
    default:
      return false;
  }
}

void AutofillPopupView::OnNativeFocusChanged(gfx::NativeView focused_now) {
  if (GetWidget() && GetWidget()->GetNativeView() != focused_now && popup_)
    popup_->Hide();
}

void AutofillPopupView::OnWidgetBoundsChanged(views::Widget* widget,
                                              const gfx::Rect& new_bounds) {
  if (widget != parent_widget_)
    return;
  if (popup_)
    popup_->Hide();
}

void AutofillPopupView::AcceptSuggestion(int index) {
  if (!popup_)
    return;

  popup_->AcceptSuggestion(index);
  popup_->Hide();
}

bool AutofillPopupView::AcceptSelectedLine() {
  if (!selected_line_ || selected_line_.value() >= popup_->line_count())
    return false;

  AcceptSuggestion(selected_line_.value());
  return true;
}

void AutofillPopupView::AcceptSelection(const gfx::Point& point) {
  if (!popup_)
    return;

  SetSelectedLine(popup_->LineFromY(point.y()));
  AcceptSelectedLine();
}

void AutofillPopupView::SetSelectedLine(std::optional<int> selected_line) {
  if (!popup_)
    return;
  if (selected_line_ == selected_line)
    return;
  if (selected_line && selected_line.value() >= popup_->line_count())
    return;

  auto previous_selected_line(selected_line_);
  selected_line_ = selected_line;
  OnSelectedRowChanged(previous_selected_line, selected_line_);
}

void AutofillPopupView::SetSelection(const gfx::Point& point) {
  if (!popup_)
    return;

  SetSelectedLine(popup_->LineFromY(point.y()));
}

void AutofillPopupView::SelectNextLine() {
  if (!popup_)
    return;

  int new_selected_line = selected_line_ ? *selected_line_ + 1 : 0;
  if (new_selected_line >= popup_->line_count())
    new_selected_line = 0;

  SetSelectedLine(new_selected_line);
}

void AutofillPopupView::SelectPreviousLine() {
  if (!popup_)
    return;

  int new_selected_line = selected_line_.value_or(0) - 1;
  if (new_selected_line < 0)
    new_selected_line = popup_->line_count() - 1;

  SetSelectedLine(new_selected_line);
}

void AutofillPopupView::ClearSelection() {
  SetSelectedLine(std::nullopt);
}

void AutofillPopupView::RemoveObserver() {
  parent_widget_->RemoveObserver(this);
  views::WidgetFocusManager::GetInstance()->RemoveFocusChangeListener(this);
}

}  // namespace electron
