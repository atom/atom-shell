// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <iomanip>
#include <string_view>

#include <dwmapi.h>
#include <windows.devices.enumeration.h>
#include <wrl/client.h>

#include "shell/browser/api/electron_api_system_preferences.h"

#include "base/containers/fixed_flat_map.h"
#include "base/win/core_winrt_util.h"
#include "base/win/windows_types.h"
#include "base/win/wrapped_window_proc.h"
#include "shell/common/color_util.h"
#include "ui/base/win/shell.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/win/hwnd_util.h"

namespace electron {

namespace {

const wchar_t kSystemPreferencesWindowClass[] =
    L"Electron_SystemPreferencesHostWindow";

using ABI::Windows::Devices::Enumeration::DeviceAccessStatus;
using ABI::Windows::Devices::Enumeration::DeviceClass;
using ABI::Windows::Devices::Enumeration::IDeviceAccessInformation;
using ABI::Windows::Devices::Enumeration::IDeviceAccessInformationStatics;
using Microsoft::WRL::ComPtr;

DeviceAccessStatus GetDeviceAccessStatus(DeviceClass device_class) {
  ComPtr<IDeviceAccessInformationStatics> dev_access_info_statics;
  HRESULT hr = base::win::GetActivationFactory<
      IDeviceAccessInformationStatics,
      RuntimeClass_Windows_Devices_Enumeration_DeviceAccessInformation>(
      &dev_access_info_statics);
  if (FAILED(hr)) {
    VLOG(1) << "IDeviceAccessInformationStatics failed: " << hr;
    return DeviceAccessStatus::DeviceAccessStatus_Allowed;
  }

  ComPtr<IDeviceAccessInformation> dev_access_info;
  hr = dev_access_info_statics->CreateFromDeviceClass(device_class,
                                                      &dev_access_info);
  if (FAILED(hr)) {
    VLOG(1) << "IDeviceAccessInformation failed: " << hr;
    return DeviceAccessStatus::DeviceAccessStatus_Allowed;
  }

  auto status = DeviceAccessStatus::DeviceAccessStatus_Unspecified;
  dev_access_info->get_CurrentStatus(&status);
  return status;
}

std::string ConvertDeviceAccessStatus(DeviceAccessStatus value) {
  switch (value) {
    case DeviceAccessStatus::DeviceAccessStatus_Unspecified:
      return "not-determined";
    case DeviceAccessStatus::DeviceAccessStatus_Allowed:
      return "granted";
    case DeviceAccessStatus::DeviceAccessStatus_DeniedBySystem:
      return "restricted";
    case DeviceAccessStatus::DeviceAccessStatus_DeniedByUser:
      return "denied";
    default:
      return "unknown";
  }
}

}  // namespace

namespace api {

bool SystemPreferences::IsAeroGlassEnabled() {
  return true;
}

std::string hexColorDWORDToRGBA(DWORD color) {
  DWORD rgba = color << 8 | color >> 24;
  std::ostringstream stream;
  stream << std::hex << std::setw(8) << std::setfill('0') << rgba;
  return stream.str();
}

std::string SystemPreferences::GetAccentColor() {
  DWORD color = 0;
  BOOL opaque = FALSE;

  if (FAILED(DwmGetColorizationColor(&color, &opaque))) {
    return "";
  }

  return hexColorDWORDToRGBA(color);
}

std::string SystemPreferences::GetColor(gin_helper::ErrorThrower thrower,
                                        const std::string& color) {
  static constexpr auto Lookup = base::MakeFixedFlatMap<std::string_view, int>({
      {"3d-dark-shadow", COLOR_3DDKSHADOW},
      {"3d-face", COLOR_3DFACE},
      {"3d-highlight", COLOR_3DHIGHLIGHT},
      {"3d-light", COLOR_3DLIGHT},
      {"3d-shadow", COLOR_3DSHADOW},
      {"active-border", COLOR_ACTIVEBORDER},
      {"active-caption", COLOR_ACTIVECAPTION},
      {"active-caption-gradient", COLOR_GRADIENTACTIVECAPTION},
      {"app-workspace", COLOR_APPWORKSPACE},
      {"button-text", COLOR_BTNTEXT},
      {"caption-text", COLOR_CAPTIONTEXT},
      {"desktop", COLOR_DESKTOP},
      {"disabled-text", COLOR_GRAYTEXT},
      {"highlight", COLOR_HIGHLIGHT},
      {"highlight-text", COLOR_HIGHLIGHTTEXT},
      {"hotlight", COLOR_HOTLIGHT},
      {"inactive-border", COLOR_INACTIVEBORDER},
      {"inactive-caption", COLOR_INACTIVECAPTION},
      {"inactive-caption-gradient", COLOR_GRADIENTINACTIVECAPTION},
      {"inactive-caption-text", COLOR_INACTIVECAPTIONTEXT},
      {"info-background", COLOR_INFOBK},
      {"info-text", COLOR_INFOTEXT},
      {"menu", COLOR_MENU},
      {"menu-highlight", COLOR_MENUHILIGHT},
      {"menu-text", COLOR_MENUTEXT},
      {"menubar", COLOR_MENUBAR},
      {"scrollbar", COLOR_SCROLLBAR},
      {"window", COLOR_WINDOW},
      {"window-frame", COLOR_WINDOWFRAME},
      {"window-text", COLOR_WINDOWTEXT},
  });

  if (const auto* iter = Lookup.find(color); iter != Lookup.end())
    return ToRGBAHex(color_utils::GetSysSkColor(iter->second));

  thrower.ThrowError("Unknown color: " + color);
  return "";
}

std::string SystemPreferences::GetMediaAccessStatus(
    gin_helper::ErrorThrower thrower,
    const std::string& media_type) {
  if (media_type == "camera") {
    return ConvertDeviceAccessStatus(
        GetDeviceAccessStatus(DeviceClass::DeviceClass_VideoCapture));
  } else if (media_type == "microphone") {
    return ConvertDeviceAccessStatus(
        GetDeviceAccessStatus(DeviceClass::DeviceClass_AudioCapture));
  } else if (media_type == "screen") {
    return ConvertDeviceAccessStatus(
        DeviceAccessStatus::DeviceAccessStatus_Allowed);
  } else {
    thrower.ThrowError("Invalid media type");
    return std::string();
  }
}

void SystemPreferences::InitializeWindow() {
  if (electron::IsUtilityProcess())
    return;
  // Wait until app is ready before creating sys color listener
  // Creating this listener before the app is ready causes global shortcuts
  // to not fire
  if (Browser::Get()->is_ready())
    color_change_listener_ =
        std::make_unique<gfx::ScopedSysColorChangeListener>(this);
  else
    Browser::Get()->AddObserver(this);

  WNDCLASSEX window_class;
  base::win::InitializeWindowClass(
      kSystemPreferencesWindowClass,
      &base::win::WrappedWindowProc<SystemPreferences::WndProcStatic>, 0, 0, 0,
      nullptr, nullptr, nullptr, nullptr, nullptr, &window_class);
  instance_ = window_class.hInstance;
  atom_ = RegisterClassEx(&window_class);

  // Create an offscreen window for receiving broadcast messages for the system
  // colorization color.  Create a hidden WS_POPUP window instead of an
  // HWND_MESSAGE window, because only top-level windows such as popups can
  // receive broadcast messages like "WM_DWMCOLORIZATIONCOLORCHANGED".
  window_ = CreateWindow(MAKEINTATOM(atom_), 0, WS_POPUP, 0, 0, 0, 0, 0, 0,
                         instance_, 0);
  gfx::CheckWindowCreated(window_, ::GetLastError());
  gfx::SetWindowUserData(window_, this);
}

LRESULT CALLBACK SystemPreferences::WndProcStatic(HWND hwnd,
                                                  UINT message,
                                                  WPARAM wparam,
                                                  LPARAM lparam) {
  auto* msg_wnd = reinterpret_cast<SystemPreferences*>(
      GetWindowLongPtr(hwnd, GWLP_USERDATA));
  if (msg_wnd)
    return msg_wnd->WndProc(hwnd, message, wparam, lparam);
  else
    return ::DefWindowProc(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK SystemPreferences::WndProc(HWND hwnd,
                                            UINT message,
                                            WPARAM wparam,
                                            LPARAM lparam) {
  if (message == WM_DWMCOLORIZATIONCOLORCHANGED) {
    DWORD new_color = (DWORD)wparam;
    std::string new_color_string = hexColorDWORDToRGBA(new_color);
    if (new_color_string != current_color_) {
      Emit("accent-color-changed", hexColorDWORDToRGBA(new_color));
      current_color_ = new_color_string;
    }
  }
  return ::DefWindowProc(hwnd, message, wparam, lparam);
}

void SystemPreferences::OnSysColorChange() {
  Emit("color-changed");
}

void SystemPreferences::OnFinishLaunching(base::Value::Dict launch_info) {
  color_change_listener_ =
      std::make_unique<gfx::ScopedSysColorChangeListener>(this);
}

}  // namespace api

}  // namespace electron
