// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/message_box.h"

#include <windows.h>  // windows.h must be included first

#include <commctrl.h>

#include <map>
#include <utility>
#include <vector>

#include "base/containers/contains.h"
#include "base/lazy_instance.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/task/post_task.h"
#include "base/win/scoped_gdi_object.h"
#include "shell/browser/browser.h"
#include "shell/browser/native_window_views.h"
#include "shell/browser/ui/win/dialog_thread.h"
#include "shell/browser/unresponsive_suppressor.h"
#include "ui/gfx/icon_util.h"
#include "ui/gfx/image/image_skia.h"

namespace electron {

MessageBoxSettings::MessageBoxSettings() = default;
MessageBoxSettings::MessageBoxSettings(const MessageBoxSettings&) = default;
MessageBoxSettings::~MessageBoxSettings() = default;

namespace {

using DialogResult = std::pair<int, bool>;

// <ID, messageBox> map.
// Note that the HWND is stored in a unique_ptr, because the pointer of HWND
// will be passed between threads and we need to ensure the memory of HWND is
// not changed while g_dialogs is modified.
std::map<int, std::unique_ptr<HWND>> g_dialogs;

// Speical HWND used by the g_dialogs map.
// - ID is used but window has not been created yet.
HWND kHwndReserve = reinterpret_cast<HWND>(-1);
// - Notification to cancel message box.
HWND kHwndCancel = reinterpret_cast<HWND>(-2);

// Lock used for modifying HWND between threads.
base::LazyInstance<base::Lock>::Leaky g_hwnd_lock = LAZY_INSTANCE_INITIALIZER;

// Small command ID values are already taken by Windows, we have to start from
// a large number to avoid conflicts with Windows.
const int kIDStart = 100;

// Get the common ID from button's name.
struct CommonButtonID {
  int button;
  int id;
};
CommonButtonID GetCommonID(const std::wstring& button) {
  std::wstring lower = base::ToLowerASCII(button);
  if (lower == L"ok")
    return {TDCBF_OK_BUTTON, IDOK};
  else if (lower == L"yes")
    return {TDCBF_YES_BUTTON, IDYES};
  else if (lower == L"no")
    return {TDCBF_NO_BUTTON, IDNO};
  else if (lower == L"cancel")
    return {TDCBF_CANCEL_BUTTON, IDCANCEL};
  else if (lower == L"retry")
    return {TDCBF_RETRY_BUTTON, IDRETRY};
  else if (lower == L"close")
    return {TDCBF_CLOSE_BUTTON, IDCLOSE};
  return {-1, -1};
}

// Determine whether the buttons are common buttons, if so map common ID
// to button ID.
void MapToCommonID(const std::vector<std::wstring>& buttons,
                   std::map<int, int>* id_map,
                   TASKDIALOG_COMMON_BUTTON_FLAGS* button_flags,
                   std::vector<TASKDIALOG_BUTTON>* dialog_buttons) {
  for (size_t i = 0; i < buttons.size(); ++i) {
    auto common = GetCommonID(buttons[i]);
    if (common.button != -1) {
      // It is a common button.
      (*id_map)[common.id] = i;
      (*button_flags) |= common.button;
    } else {
      // It is a custom button.
      dialog_buttons->push_back(
          {static_cast<int>(i + kIDStart), buttons[i].c_str()});
    }
  }
}

// Callback of the task dialog. Used for storing the hwnd of task dialog when
// it is created.
HRESULT CALLBACK
TaskDialogCallback(HWND hwnd, UINT msg, WPARAM, LPARAM, LONG_PTR data) {
  if (msg == TDN_CREATED) {
    HWND* target = reinterpret_cast<HWND*>(data);
    base::AutoLock lock(g_hwnd_lock.Get());
    if (*target == kHwndCancel) {
      // If the dialog is cancelled then close it directly.
      ::PostMessage(hwnd, WM_CLOSE, 0, 0);
    } else if (*target == kHwndReserve) {
      // Otherwise save the hwnd.
      *target = hwnd;
    } else {
      NOTREACHED();
    }
  }
  return S_OK;
}

DialogResult ShowTaskDialogWstr(NativeWindow* parent,
                                MessageBoxType type,
                                const std::vector<std::wstring>& buttons,
                                int default_id,
                                int cancel_id,
                                bool no_link,
                                const std::u16string& title,
                                const std::u16string& message,
                                const std::u16string& detail,
                                const std::u16string& checkbox_label,
                                bool checkbox_checked,
                                const gfx::ImageSkia& icon,
                                HWND* hwnd) {
  TASKDIALOG_FLAGS flags =
      TDF_SIZE_TO_CONTENT |           // Show all content.
      TDF_ALLOW_DIALOG_CANCELLATION;  // Allow canceling the dialog.

  TASKDIALOGCONFIG config = {0};
  config.cbSize = sizeof(config);
  config.hInstance = GetModuleHandle(NULL);
  config.dwFlags = flags;

  if (parent) {
    config.hwndParent = static_cast<electron::NativeWindowViews*>(parent)
                            ->GetAcceleratedWidget();
  }

  if (default_id > 0)
    config.nDefaultButton = kIDStart + default_id;

  // TaskDialogIndirect doesn't allow empty name, if we set empty title it
  // will show "electron.exe" in title.
  if (title.empty()) {
    std::wstring app_name = base::UTF8ToWide(Browser::Get()->GetName());
    config.pszWindowTitle = app_name.c_str();
  } else {
    config.pszWindowTitle = base::as_wcstr(title);
  }

  base::win::ScopedHICON hicon;
  if (!icon.isNull()) {
    hicon = IconUtil::CreateHICONFromSkBitmap(*icon.bitmap());
    config.dwFlags |= TDF_USE_HICON_MAIN;
    config.hMainIcon = hicon.get();
  } else {
    // Show icon according to dialog's type.
    switch (type) {
      case MessageBoxType::kInformation:
      case MessageBoxType::kQuestion:
        config.pszMainIcon = TD_INFORMATION_ICON;
        break;
      case MessageBoxType::kWarning:
        config.pszMainIcon = TD_WARNING_ICON;
        break;
      case MessageBoxType::kError:
        config.pszMainIcon = TD_ERROR_ICON;
        break;
      case MessageBoxType::kNone:
        break;
    }
  }

  // If "detail" is empty then don't make message highlighted.
  if (detail.empty()) {
    config.pszContent = base::as_wcstr(message);
  } else {
    config.pszMainInstruction = base::as_wcstr(message);
    config.pszContent = base::as_wcstr(detail);
  }

  if (!checkbox_label.empty()) {
    config.pszVerificationText = base::as_wcstr(checkbox_label);
    if (checkbox_checked)
      config.dwFlags |= TDF_VERIFICATION_FLAG_CHECKED;
  }

  // Iterate through the buttons, put common buttons in dwCommonButtons
  // and custom buttons in pButtons.
  std::map<int, int> id_map;
  std::vector<TASKDIALOG_BUTTON> dialog_buttons;
  if (no_link) {
    for (size_t i = 0; i < buttons.size(); ++i)
      dialog_buttons.push_back(
          {static_cast<int>(i + kIDStart), buttons[i].c_str()});
  } else {
    MapToCommonID(buttons, &id_map, &config.dwCommonButtons, &dialog_buttons);
  }
  if (!dialog_buttons.empty()) {
    config.pButtons = &dialog_buttons.front();
    config.cButtons = dialog_buttons.size();
    if (!no_link)
      config.dwFlags |= TDF_USE_COMMAND_LINKS;  // custom buttons as links.
  }

  // Pass a callback to receive the HWND of the message box.
  if (hwnd) {
    config.pfCallback = &TaskDialogCallback;
    config.lpCallbackData = reinterpret_cast<LONG_PTR>(hwnd);
  }

  int id = 0;
  BOOL verificationFlagChecked = FALSE;
  TaskDialogIndirect(&config, &id, nullptr, &verificationFlagChecked);

  int button_id;
  if (id_map.find(id) != id_map.end())  // common button.
    button_id = id_map[id];
  else if (id >= kIDStart)  // custom button.
    button_id = id - kIDStart;
  else
    button_id = cancel_id;

  return std::make_tuple(button_id, verificationFlagChecked);
}

DialogResult ShowTaskDialogUTF8(const MessageBoxSettings& settings,
                                HWND* hwnd) {
  std::vector<std::wstring> buttons;
  for (const auto& button : settings.buttons)
    buttons.push_back(base::UTF8ToWide(button));

  const std::u16string title = base::UTF8ToUTF16(settings.title);
  const std::u16string message = base::UTF8ToUTF16(settings.message);
  const std::u16string detail = base::UTF8ToUTF16(settings.detail);
  const std::u16string checkbox_label =
      base::UTF8ToUTF16(settings.checkbox_label);

  return ShowTaskDialogWstr(
      settings.parent_window, settings.type, buttons, settings.default_id,
      settings.cancel_id, settings.no_link, title, message, detail,
      checkbox_label, settings.checkbox_checked, settings.icon, hwnd);
}

}  // namespace

int ShowMessageBoxSync(const MessageBoxSettings& settings) {
  electron::UnresponsiveSuppressor suppressor;
  DialogResult result = ShowTaskDialogUTF8(settings, nullptr);
  return result.first;
}

void ShowMessageBox(const MessageBoxSettings& settings,
                    MessageBoxCallback callback) {
  // Check if the ID has been taken, and mark it as reserved if not.
  HWND* hwnd = nullptr;
  if (settings.id) {
    if (base::Contains(g_dialogs, *settings.id))
      CloseMessageBox(*settings.id);
    auto it =
        g_dialogs.emplace(*settings.id, std::make_unique<HWND>(kHwndReserve));
    hwnd = it.first->second.get();
  }

  dialog_thread::Run(
      base::BindOnce(&ShowTaskDialogUTF8, settings, base::Unretained(hwnd)),
      base::BindOnce(
          [](MessageBoxCallback callback, absl::optional<int> id,
             DialogResult result) {
            if (id)
              g_dialogs.erase(*id);
            std::move(callback).Run(result.first, result.second);
          },
          std::move(callback), settings.id));
}

void CloseMessageBox(int id) {
  auto it = g_dialogs.find(id);
  if (it == g_dialogs.end()) {
    LOG(ERROR) << "CloseMessageBox called with unexist ID";
    return;
  }
  HWND* hwnd = it->second.get();
  base::AutoLock lock(g_hwnd_lock.Get());
  DCHECK(*hwnd != kHwndCancel);
  if (*hwnd == kHwndReserve) {
    // If the dialog window has not been created yet, tell it to cancel.
    *hwnd = kHwndCancel;
  } else {
    // Otherwise send a message to close it.
    ::PostMessage(*hwnd, WM_CLOSE, 0, 0);
  }
}

void ShowErrorBox(const std::u16string& title, const std::u16string& content) {
  electron::UnresponsiveSuppressor suppressor;
  ShowTaskDialogWstr(nullptr, MessageBoxType::kError, {}, -1, 0, false,
                     u"Error", title, content, u"", false, gfx::ImageSkia(),
                     nullptr);
}

}  // namespace electron
