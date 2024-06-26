// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/views/root_view.h"

#include <memory>

#include "components/input/native_web_keyboard_event.h"
#include "shell/browser/native_window.h"
#include "shell/browser/ui/views/menu_bar.h"
#include "ui/views/layout/box_layout.h"

namespace electron {

namespace {

bool IsAltKey(const input::NativeWebKeyboardEvent& event) {
  return event.windows_key_code == ui::VKEY_MENU;
}

bool IsAltModifier(const input::NativeWebKeyboardEvent& event) {
  using Modifiers = input::NativeWebKeyboardEvent::Modifiers;
  int modifiers = event.GetModifiers();
  modifiers &= ~Modifiers::kNumLockOn;
  modifiers &= ~Modifiers::kCapsLockOn;
  return (modifiers == Modifiers::kAltKey) ||
         (modifiers == (Modifiers::kAltKey | Modifiers::kIsLeft)) ||
         (modifiers == (Modifiers::kAltKey | Modifiers::kIsRight));
}

}  // namespace

RootView::RootView(NativeWindow* window)
    : window_{raw_ref<NativeWindow>::from_ptr(window)},
      main_view_{raw_ref<views::View>::from_ptr(
          AddChildView(std::make_unique<views::View>()))} {
  set_owned_by_client();
  views::BoxLayout* layout =
      SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical));
  main_view_->SetUseDefaultFillLayout(true);
  layout->SetFlexForView(&main_view_.get(), 1);
}

RootView::~RootView() = default;

void RootView::SetMenu(ElectronMenuModel* menu_model) {
  if (menu_model == nullptr) {
    // Remove accelerators
    UnregisterAcceleratorsWithFocusManager();
    // and menu bar.
    SetMenuBarVisibility(false);
    menu_bar_.reset();
    return;
  }

  RegisterAcceleratorsWithFocusManager(menu_model);

  // Do not show menu bar in frameless window.
  if (!window_->has_frame())
    return;

  if (!menu_bar_) {
    menu_bar_ = std::make_unique<MenuBar>(&window_.get(), this);
    menu_bar_->set_owned_by_client();
    if (!menu_bar_autohide_)
      SetMenuBarVisibility(true);
  }

  menu_bar_->SetMenu(menu_model);
  InvalidateLayout();
}

bool RootView::HasMenu() const {
  return !!menu_bar_;
}

int RootView::GetMenuBarHeight() const {
  return menu_bar_ ? menu_bar_->GetPreferredSize().height() : 0;
}

void RootView::SetAutoHideMenuBar(bool auto_hide) {
  menu_bar_autohide_ = auto_hide;
}

void RootView::SetMenuBarVisibility(bool visible) {
  if (!window_->content_view() || !menu_bar_ || menu_bar_visible_ == visible)
    return;

  menu_bar_visible_ = visible;
  if (visible) {
    AddChildViewAt(menu_bar_.get(), 0);
  } else {
    RemoveChildView(menu_bar_.get());
  }

  InvalidateLayout();
}

void RootView::HandleKeyEvent(const input::NativeWebKeyboardEvent& event) {
  if (!menu_bar_)
    return;

  // Show accelerator when "Alt" is pressed.
  if (menu_bar_visible_ && IsAltKey(event))
    menu_bar_->SetAcceleratorVisibility(
        event.GetType() == blink::WebInputEvent::Type::kRawKeyDown);

  // Show the submenu when "Alt+Key" is pressed.
  if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown &&
      event.windows_key_code >= ui::VKEY_A &&
      event.windows_key_code <= ui::VKEY_Z && IsAltModifier(event) &&
      menu_bar_->HasAccelerator(event.windows_key_code)) {
    if (!menu_bar_visible_) {
      SetMenuBarVisibility(true);

      View* focused_view = GetFocusManager()->GetFocusedView();
      last_focused_view_tracker_.SetView(focused_view);
      menu_bar_->RequestFocus();
    }

    menu_bar_->ActivateAccelerator(event.windows_key_code);
    return;
  }

  // Toggle the menu bar only when a single Alt is released.
  if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown &&
      IsAltKey(event)) {
    // When a single Alt is pressed:
    menu_bar_alt_pressed_ = true;
  } else if (event.GetType() == blink::WebInputEvent::Type::kKeyUp &&
             IsAltKey(event) && menu_bar_alt_pressed_) {
    // When a single Alt is released right after a Alt is pressed:
    menu_bar_alt_pressed_ = false;
    if (menu_bar_autohide_)
      SetMenuBarVisibility(!menu_bar_visible_);

    View* focused_view = GetFocusManager()->GetFocusedView();
    last_focused_view_tracker_.SetView(focused_view);
    if (menu_bar_visible_) {
      menu_bar_->RequestFocus();
      // Show accelerators when menu bar is focused
      menu_bar_->SetAcceleratorVisibility(true);
    }
  } else {
    // When any other keys except single Alt have been pressed/released:
    menu_bar_alt_pressed_ = false;
  }
}

void RootView::RestoreFocus() {
  View* last_focused_view = last_focused_view_tracker_.view();
  if (last_focused_view) {
    GetFocusManager()->SetFocusedViewWithReason(
        last_focused_view,
        views::FocusManager::FocusChangeReason::kFocusRestore);
  }
  if (menu_bar_autohide_)
    SetMenuBarVisibility(false);
}

void RootView::ResetAltState() {
  menu_bar_alt_pressed_ = false;
}

gfx::Size RootView::GetMinimumSize() const {
  return window_->GetMinimumSize();
}

gfx::Size RootView::GetMaximumSize() const {
  return window_->GetMaximumSize();
}

bool RootView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  return accelerator_util::TriggerAcceleratorTableCommand(&accelerator_table_,
                                                          accelerator);
}

void RootView::RegisterAcceleratorsWithFocusManager(
    ElectronMenuModel* menu_model) {
  if (!menu_model)
    return;
  // Clear previous accelerators.
  UnregisterAcceleratorsWithFocusManager();

  views::FocusManager* focus_manager = GetFocusManager();
  // Register accelerators with focus manager.
  accelerator_util::GenerateAcceleratorTable(&accelerator_table_, menu_model);
  for (const auto& iter : accelerator_table_) {
    focus_manager->RegisterAccelerator(
        iter.first, ui::AcceleratorManager::kNormalPriority, this);
  }
}

void RootView::UnregisterAcceleratorsWithFocusManager() {
  views::FocusManager* focus_manager = GetFocusManager();
  accelerator_table_.clear();
  focus_manager->UnregisterAccelerators(this);
}

}  // namespace electron
