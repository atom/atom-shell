// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/electron_menu_model.h"

#include <utility>

#include "base/stl_util.h"

namespace electron {

#if BUILDFLAG(IS_MAC)
ElectronMenuModel::SharingItem::SharingItem() = default;
ElectronMenuModel::SharingItem::SharingItem(SharingItem&&) = default;
ElectronMenuModel::SharingItem::~SharingItem() = default;
#endif

bool ElectronMenuModel::Delegate::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) const {
  return GetAcceleratorForCommandIdWithParams(command_id, false, accelerator);
}

ElectronMenuModel::ElectronMenuModel(Delegate* delegate)
    : ui::SimpleMenuModel(delegate), delegate_(delegate) {}

ElectronMenuModel::~ElectronMenuModel() = default;

void ElectronMenuModel::SetToolTip(size_t index,
                                   const std::u16string& toolTip) {
  int command_id = GetCommandIdAt(index);
  toolTips_[command_id] = toolTip;
}

std::u16string ElectronMenuModel::GetToolTipAt(size_t index) {
  const int command_id = GetCommandIdAt(index);
  const auto iter = toolTips_.find(command_id);
  return iter == std::end(toolTips_) ? std::u16string() : iter->second;
}

void ElectronMenuModel::SetRole(size_t index, const std::u16string& role) {
  int command_id = GetCommandIdAt(index);
  roles_[command_id] = role;
}

std::u16string ElectronMenuModel::GetRoleAt(size_t index) {
  const int command_id = GetCommandIdAt(index);
  const auto iter = roles_.find(command_id);
  return iter == std::end(roles_) ? std::u16string() : iter->second;
}

void ElectronMenuModel::SetSecondaryLabel(size_t index,
                                          const std::u16string& sublabel) {
  int command_id = GetCommandIdAt(index);
  sublabels_[command_id] = sublabel;
}

std::u16string ElectronMenuModel::GetSecondaryLabelAt(size_t index) const {
  int command_id = GetCommandIdAt(index);
  const auto iter = sublabels_.find(command_id);
  return iter == std::end(sublabels_) ? std::u16string() : iter->second;
}

bool ElectronMenuModel::GetAcceleratorAtWithParams(
    size_t index,
    bool use_default_accelerator,
    ui::Accelerator* accelerator) const {
  if (delegate_) {
    return delegate_->GetAcceleratorForCommandIdWithParams(
        GetCommandIdAt(index), use_default_accelerator, accelerator);
  }
  return false;
}

bool ElectronMenuModel::ShouldRegisterAcceleratorAt(size_t index) const {
  if (delegate_) {
    return delegate_->ShouldRegisterAcceleratorForCommandId(
        GetCommandIdAt(index));
  }
  return true;
}

bool ElectronMenuModel::WorksWhenHiddenAt(size_t index) const {
  if (delegate_) {
    return delegate_->ShouldCommandIdWorkWhenHidden(GetCommandIdAt(index));
  }
  return true;
}

#if BUILDFLAG(IS_MAC)
bool ElectronMenuModel::GetSharingItemAt(size_t index,
                                         SharingItem* item) const {
  if (delegate_)
    return delegate_->GetSharingItemForCommandId(GetCommandIdAt(index), item);
  return false;
}

void ElectronMenuModel::SetSharingItem(SharingItem item) {
  sharing_item_.emplace(std::move(item));
}
#endif

void ElectronMenuModel::MenuWillClose() {
  ui::SimpleMenuModel::MenuWillClose();
  for (Observer& observer : observers_) {
    observer.OnMenuWillClose();
  }
}

void ElectronMenuModel::MenuWillShow() {
  ui::SimpleMenuModel::MenuWillShow();
  for (Observer& observer : observers_) {
    observer.OnMenuWillShow();
  }
}

ElectronMenuModel* ElectronMenuModel::GetSubmenuModelAt(size_t index) {
  return static_cast<ElectronMenuModel*>(
      ui::SimpleMenuModel::GetSubmenuModelAt(index));
}

}  // namespace electron
