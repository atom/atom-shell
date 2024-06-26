// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_MENU_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_MENU_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "gin/arguments.h"
#include "shell/browser/api/electron_api_base_window.h"
#include "shell/browser/event_emitter_mixin.h"
#include "shell/browser/ui/electron_menu_model.h"
#include "shell/common/gin_helper/constructible.h"
#include "shell/common/gin_helper/pinnable.h"

namespace electron::api {

class Menu : public gin::Wrappable<Menu>,
             public gin_helper::EventEmitterMixin<Menu>,
             public gin_helper::Constructible<Menu>,
             public gin_helper::Pinnable<Menu>,
             public ElectronMenuModel::Delegate,
             private ElectronMenuModel::Observer {
 public:
  // gin_helper::Constructible
  static gin::Handle<Menu> New(gin::Arguments* args);
  static void FillObjectTemplate(v8::Isolate*, v8::Local<v8::ObjectTemplate>);
  static const char* GetClassName() { return "Menu"; }

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  const char* GetTypeName() override;

#if BUILDFLAG(IS_MAC)
  // Set the global menubar.
  static void SetApplicationMenu(Menu* menu);

  // Fake sending an action from the application menu.
  static void SendActionToFirstResponder(const std::string& action);
#endif

  ElectronMenuModel* model() const { return model_.get(); }

  // disable copy
  Menu(const Menu&) = delete;
  Menu& operator=(const Menu&) = delete;

 protected:
  explicit Menu(gin::Arguments* args);
  ~Menu() override;

  // Returns a new callback which keeps references of the JS wrapper until the
  // passed |callback| is called.
  base::OnceClosure BindSelfToClosure(base::OnceClosure callback);

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsCommandIdVisible(int command_id) const override;
  bool ShouldCommandIdWorkWhenHidden(int command_id) const override;
  bool GetAcceleratorForCommandIdWithParams(
      int command_id,
      bool use_default_accelerator,
      ui::Accelerator* accelerator) const override;
  bool ShouldRegisterAcceleratorForCommandId(int command_id) const override;
#if BUILDFLAG(IS_MAC)
  bool GetSharingItemForCommandId(
      int command_id,
      ElectronMenuModel::SharingItem* item) const override;
  v8::Local<v8::Value> GetUserAcceleratorAt(int command_id) const;
#endif
  void ExecuteCommand(int command_id, int event_flags) override;
  void OnMenuWillShow(ui::SimpleMenuModel* source) override;

  virtual void PopupAt(BaseWindow* window,
                       int x,
                       int y,
                       int positioning_item,
                       ui::MenuSourceType source_type,
                       base::OnceClosure callback) = 0;
  virtual void ClosePopupAt(int32_t window_id) = 0;
  virtual std::u16string GetAcceleratorTextAtForTesting(int index) const;

  std::unique_ptr<ElectronMenuModel> model_;
  raw_ptr<Menu> parent_ = nullptr;

  // Observable:
  void OnMenuWillClose() override;
  void OnMenuWillShow() override;

 private:
  void InsertItemAt(int index, int command_id, const std::u16string& label);
  void InsertSeparatorAt(int index);
  void InsertCheckItemAt(int index,
                         int command_id,
                         const std::u16string& label);
  void InsertRadioItemAt(int index,
                         int command_id,
                         const std::u16string& label,
                         int group_id);
  void InsertSubMenuAt(int index,
                       int command_id,
                       const std::u16string& label,
                       Menu* menu);
  void SetIcon(int index, const gfx::Image& image);
  void SetSublabel(int index, const std::u16string& sublabel);
  void SetToolTip(int index, const std::u16string& toolTip);
  void SetRole(int index, const std::u16string& role);
  void Clear();
  int GetIndexOfCommandId(int command_id) const;
  int GetItemCount() const;
  int GetCommandIdAt(int index) const;
  std::u16string GetLabelAt(int index) const;
  std::u16string GetSublabelAt(int index) const;
  std::u16string GetToolTipAt(int index) const;
  bool IsItemCheckedAt(int index) const;
  bool IsEnabledAt(int index) const;
  bool IsVisibleAt(int index) const;
  bool WorksWhenHiddenAt(int index) const;
};

}  // namespace electron::api

namespace gin {

template <>
struct Converter<electron::ElectronMenuModel*> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     electron::ElectronMenuModel** out) {
    // null would be transferred to nullptr.
    if (val->IsNull()) {
      *out = nullptr;
      return true;
    }

    electron::api::Menu* menu;
    if (!Converter<electron::api::Menu*>::FromV8(isolate, val, &menu))
      return false;
    *out = menu->model();
    return true;
  }
};

}  // namespace gin

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_MENU_H_
