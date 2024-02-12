// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_UI_ELECTRON_MENU_MODEL_H_
#define ELECTRON_SHELL_BROWSER_UI_ELECTRON_MENU_MODEL_H_

#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "ui/base/models/simple_menu_model.h"
#include "url/gurl.h"

namespace electron {

class ElectronMenuModel : public ui::SimpleMenuModel {
 public:
#if BUILDFLAG(IS_MAC)
  struct SharingItem {
    SharingItem();
    SharingItem(SharingItem&&);
    SharingItem(const SharingItem&) = delete;
    ~SharingItem();

    std::optional<std::vector<std::string>> texts;
    std::optional<std::vector<GURL>> urls;
    std::optional<std::vector<base::FilePath>> file_paths;
  };
#endif

  class Delegate : public ui::SimpleMenuModel::Delegate {
   public:
    ~Delegate() override {}

    virtual bool GetAcceleratorForCommandIdWithParams(
        int command_id,
        bool use_default_accelerator,
        ui::Accelerator* accelerator) const = 0;

    virtual bool ShouldRegisterAcceleratorForCommandId(
        int command_id) const = 0;

    virtual bool ShouldCommandIdWorkWhenHidden(int command_id) const = 0;

#if BUILDFLAG(IS_MAC)
    virtual bool GetSharingItemForCommandId(int command_id,
                                            SharingItem* item) const = 0;
#endif

   private:
    // ui::SimpleMenuModel::Delegate:
    bool GetAcceleratorForCommandId(
        int command_id,
        ui::Accelerator* accelerator) const override;
  };

  class Observer : public base::CheckedObserver {
   public:
    ~Observer() override {}

    // Notifies the menu will open.
    virtual void OnMenuWillShow() {}

    // Notifies the menu has been closed.
    virtual void OnMenuWillClose() {}
  };

  explicit ElectronMenuModel(Delegate* delegate);
  ~ElectronMenuModel() override;

  // disable copy
  ElectronMenuModel(const ElectronMenuModel&) = delete;
  ElectronMenuModel& operator=(const ElectronMenuModel&) = delete;

  void AddObserver(Observer* obs) { observers_.AddObserver(obs); }
  void RemoveObserver(Observer* obs) { observers_.RemoveObserver(obs); }

  void SetToolTip(size_t index, const std::u16string& toolTip);
  std::u16string GetToolTipAt(size_t index);
  void SetRole(size_t index, const std::u16string& role);
  std::u16string GetRoleAt(size_t index);
  void SetSecondaryLabel(size_t index, const std::u16string& sublabel);
  std::u16string GetSecondaryLabelAt(size_t index) const override;
  bool GetAcceleratorAtWithParams(size_t index,
                                  bool use_default_accelerator,
                                  ui::Accelerator* accelerator) const;
  bool ShouldRegisterAcceleratorAt(size_t index) const;
  bool WorksWhenHiddenAt(size_t index) const;
#if BUILDFLAG(IS_MAC)
  // Return the SharingItem of menu item.
  bool GetSharingItemAt(size_t index, SharingItem* item) const;
  // Set/Get the SharingItem of this menu.
  void SetSharingItem(SharingItem item);
  [[nodiscard]] const std::optional<SharingItem>& sharing_item() const {
    return sharing_item_;
  }

#endif

  // ui::SimpleMenuModel:
  void MenuWillClose() override;
  void MenuWillShow() override;

  base::WeakPtr<ElectronMenuModel> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  using SimpleMenuModel::GetSubmenuModelAt;
  ElectronMenuModel* GetSubmenuModelAt(size_t index);

 private:
  raw_ptr<Delegate> delegate_;  // weak ref.

#if BUILDFLAG(IS_MAC)
  std::optional<SharingItem> sharing_item_;
#endif

  base::flat_map<int, std::u16string> toolTips_;   // command id -> tooltip
  base::flat_map<int, std::u16string> roles_;      // command id -> role
  base::flat_map<int, std::u16string> sublabels_;  // command id -> sublabel
  base::ObserverList<Observer> observers_;

  base::WeakPtrFactory<ElectronMenuModel> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_ELECTRON_MENU_MODEL_H_
