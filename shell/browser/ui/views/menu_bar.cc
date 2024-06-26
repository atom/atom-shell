// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/views/menu_bar.h"

#include <memory>

#include "shell/browser/native_window.h"
#include "shell/browser/ui/views/submenu_button.h"
#include "ui/color/color_provider.h"
#include "ui/native_theme/common_theme.h"
#include "ui/views/background.h"
#include "ui/views/layout/box_layout.h"

#if BUILDFLAG(IS_LINUX)
#include "ui/gtk/gtk_util.h"  // nogncheck
#endif

#if BUILDFLAG(IS_WIN)
#include "ui/gfx/color_utils.h"
#endif

namespace electron {

namespace {

// Default color of the menu bar.
const SkColor kDefaultColor = SkColorSetARGB(255, 233, 233, 233);

}  // namespace

MenuBar::MenuBar(NativeWindow* window, RootView* root_view)
    : background_color_(kDefaultColor), window_(window), root_view_(root_view) {
  const ui::NativeTheme* theme = root_view_->GetNativeTheme();
#if BUILDFLAG(IS_WIN)
  SetBackground(views::CreateThemedSolidBackground(ui::kColorMenuBackground));
#endif
  RefreshColorCache(theme);
  UpdateViewColors();
  SetFocusBehavior(FocusBehavior::ALWAYS);
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  window_->AddObserver(this);
}

MenuBar::~MenuBar() {
  window_->RemoveObserver(this);
}

void MenuBar::SetMenu(ElectronMenuModel* model) {
  menu_model_ = model;
  RebuildChildren();
}

void MenuBar::SetAcceleratorVisibility(bool visible) {
  for (views::View* child : GetChildrenInZOrder())
    static_cast<SubmenuButton*>(child)->SetAcceleratorVisibility(visible);
}

MenuBar::View* MenuBar::FindAccelChild(char16_t key) {
  if (key == 0)
    return nullptr;
  for (views::View* child : GetChildrenInZOrder()) {
    if (static_cast<SubmenuButton*>(child)->accelerator() == key)
      return child;
  }
  return nullptr;
}

bool MenuBar::HasAccelerator(char16_t key) {
  return FindAccelChild(key) != nullptr;
}

void MenuBar::ActivateAccelerator(char16_t key) {
  auto* child = FindAccelChild(key);
  if (child)
    static_cast<SubmenuButton*>(child)->Activate(nullptr);
}

size_t MenuBar::GetItemCount() const {
  return menu_model_ ? menu_model_->GetItemCount() : 0;
}

bool MenuBar::GetMenuButtonFromScreenPoint(const gfx::Point& screenPoint,
                                           ElectronMenuModel** menu_model,
                                           views::MenuButton** button) {
  if (!GetBoundsInScreen().Contains(screenPoint))
    return false;

  auto children = GetChildrenInZOrder();
  for (size_t i = 0, n = children.size(); i < n; ++i) {
    if (children[i]->GetBoundsInScreen().Contains(screenPoint) &&
        (menu_model_->GetTypeAt(i) == ElectronMenuModel::TYPE_SUBMENU)) {
      *menu_model = menu_model_->GetSubmenuModelAt(i);
      *button = static_cast<views::MenuButton*>(children[i]);
      return true;
    }
  }

  return false;
}

void MenuBar::OnBeforeExecuteCommand() {
  if (GetPaneFocusTraversable() != nullptr) {
    RemovePaneFocus();
  }
  root_view_->RestoreFocus();
}

void MenuBar::OnMenuClosed() {
  SetAcceleratorVisibility(pane_has_focus());
}

void MenuBar::OnWindowBlur() {
  UpdateViewColors();
  SetAcceleratorVisibility(pane_has_focus());
}

void MenuBar::OnWindowFocus() {
  UpdateViewColors();
  SetAcceleratorVisibility(pane_has_focus());
}

void MenuBar::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  node_data->SetNameExplicitlyEmpty();
  node_data->role = ax::mojom::Role::kMenuBar;
}

bool MenuBar::AcceleratorPressed(const ui::Accelerator& accelerator) {
  // Treat pressing Alt the same as pressing Esc.
  const ui::Accelerator& translated =
      accelerator.key_code() == ui::VKEY_MENU
          ? ui::Accelerator(ui::VKEY_ESCAPE, accelerator.modifiers(),
                            accelerator.key_state(), accelerator.time_stamp())
          : accelerator;
  bool result = views::AccessiblePaneView::AcceleratorPressed(translated);
  if (result && !pane_has_focus())
    root_view_->RestoreFocus();
  return result;
}

bool MenuBar::SetPaneFocusAndFocusDefault() {
  bool result = views::AccessiblePaneView::SetPaneFocusAndFocusDefault();
  if (result && !accelerator_installed_) {
    // Listen to Alt key events.
    // Note that there is no need to unregister the accelerator.
    accelerator_installed_ = true;
    focus_manager()->RegisterAccelerator(
        ui::Accelerator(ui::VKEY_MENU, ui::EF_ALT_DOWN),
        ui::AcceleratorManager::kNormalPriority, this);
  }
  return result;
}

void MenuBar::OnThemeChanged() {
  views::AccessiblePaneView::OnThemeChanged();
  const ui::NativeTheme* theme = root_view_->GetNativeTheme();
  RefreshColorCache(theme);
  UpdateViewColors();
}

void MenuBar::OnDidChangeFocus(View* focused_before, View* focused_now) {
  views::AccessiblePaneView::OnDidChangeFocus(focused_before, focused_now);
  SetAcceleratorVisibility(pane_has_focus());
  if (!pane_has_focus())
    root_view_->RestoreFocus();
}

void MenuBar::ButtonPressed(size_t id, const ui::Event& event) {
  // Hide the accelerator when a submenu is activated.
  SetAcceleratorVisibility(pane_has_focus());

  if (!menu_model_)
    return;

  if (!root_view_->HasFocus())
    root_view_->RequestFocus();

  ElectronMenuModel::ItemType type = menu_model_->GetTypeAt(id);
  if (type != ElectronMenuModel::TYPE_SUBMENU) {
    menu_model_->ActivatedAt(id, 0);
    return;
  }

  SubmenuButton* source = nullptr;
  for (views::View* child : children()) {
    auto* button = static_cast<SubmenuButton*>(child);
    int button_id = button->GetID();
    if (button_id >= 0 && static_cast<size_t>(button_id) == id) {
      source = button;
      break;
    }
  }
  DCHECK(source);

  // Deleted in MenuDelegate::OnMenuClosed
  auto* menu_delegate = new MenuDelegate(this);
  menu_delegate->RunMenu(
      menu_model_->GetSubmenuModelAt(id), source,
      event.IsKeyEvent() ? ui::MENU_SOURCE_KEYBOARD : ui::MENU_SOURCE_MOUSE);
  menu_delegate->AddObserver(this);
}

void MenuBar::ViewHierarchyChanged(
    const views::ViewHierarchyChangedDetails& details) {
  views::AccessiblePaneView::ViewHierarchyChanged(details);
#if BUILDFLAG(IS_WIN)
  background_color_ = GetBackground()->get_color();
#endif
}

void MenuBar::RefreshColorCache(const ui::NativeTheme* theme) {
  if (theme) {
#if BUILDFLAG(IS_LINUX)
    background_color_ = gtk::GetBgColor("GtkMenuBar#menubar");
    enabled_color_ =
        gtk::GetFgColor("GtkMenuBar#menubar GtkMenuItem#menuitem GtkLabel");
    disabled_color_ = gtk::GetFgColor(
        "GtkMenuBar#menubar GtkMenuItem#menuitem:disabled GtkLabel");
#elif BUILDFLAG(IS_WIN)
    background_color_ = GetBackground()->get_color();
#endif
  }
}

void MenuBar::RebuildChildren() {
  RemoveAllChildViews();
  for (size_t i = 0, n = GetItemCount(); i < n; ++i) {
    auto* button = new SubmenuButton(
        base::BindRepeating(&MenuBar::ButtonPressed, base::Unretained(this), i),
        menu_model_->GetLabelAt(i), background_color_);
    button->SetID(i);
    AddChildView(button);
  }
  UpdateViewColors();
}

void MenuBar::UpdateViewColors() {
#if BUILDFLAG(IS_LINUX)
  // set menubar background color
  SetBackground(views::CreateSolidBackground(background_color_));
#endif

  // set child colors
  if (menu_model_ == nullptr)
    return;
#if BUILDFLAG(IS_LINUX)
  const auto& textColor =
      window_->IsFocused() ? enabled_color_ : disabled_color_;
  for (views::View* child : GetChildrenInZOrder()) {
    auto* button = static_cast<SubmenuButton*>(child);
    button->SetTextColor(views::Button::STATE_NORMAL, textColor);
    button->SetTextColor(views::Button::STATE_DISABLED, disabled_color_);
    button->SetTextColor(views::Button::STATE_PRESSED, enabled_color_);
    button->SetTextColor(views::Button::STATE_HOVERED, textColor);
    button->SetUnderlineColor(textColor);
  }
#elif BUILDFLAG(IS_WIN)
  for (views::View* child : GetChildrenInZOrder()) {
    auto* button = static_cast<SubmenuButton*>(child);
    button->SetUnderlineColor(color_utils::GetSysSkColor(COLOR_MENUTEXT));
  }
#endif
}

BEGIN_METADATA(MenuBar)
END_METADATA

}  // namespace electron
