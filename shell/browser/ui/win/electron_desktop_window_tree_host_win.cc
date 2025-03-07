// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/win/electron_desktop_window_tree_host_win.h"

#include "base/win/windows_version.h"
#include "electron/buildflags/buildflags.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/native_window_views.h"
#include "shell/browser/ui/views/win_frame_view.h"
#include "shell/browser/win/dark_mode.h"
#include "ui/base/win/hwnd_metrics.h"

namespace electron {

ElectronDesktopWindowTreeHostWin::ElectronDesktopWindowTreeHostWin(
    NativeWindowViews* native_window_view,
    views::DesktopNativeWidgetAura* desktop_native_widget_aura)
    : views::DesktopWindowTreeHostWin(native_window_view->widget(),
                                      desktop_native_widget_aura),
      native_window_view_(native_window_view) {}

ElectronDesktopWindowTreeHostWin::~ElectronDesktopWindowTreeHostWin() = default;

bool ElectronDesktopWindowTreeHostWin::PreHandleMSG(UINT message,
                                                    WPARAM w_param,
                                                    LPARAM l_param,
                                                    LRESULT* result) {
  const bool dark_mode_supported = win::IsDarkModeSupported();
  if (dark_mode_supported && message == WM_NCCREATE) {
    win::SetDarkModeForWindow(GetAcceleratedWidget());
    ui::NativeTheme::GetInstanceForNativeUi()->AddObserver(this);
  } else if (dark_mode_supported && message == WM_DESTROY) {
    ui::NativeTheme::GetInstanceForNativeUi()->RemoveObserver(this);
  }

  return native_window_view_->PreHandleMSG(message, w_param, l_param, result);
}

bool ElectronDesktopWindowTreeHostWin::ShouldPaintAsActive() const {
  if (force_should_paint_as_active_.has_value()) {
    return force_should_paint_as_active_.value();
  }

  return views::DesktopWindowTreeHostWin::ShouldPaintAsActive();
}

bool ElectronDesktopWindowTreeHostWin::GetDwmFrameInsetsInPixels(
    gfx::Insets* insets) const {
  // Set DWMFrameInsets to prevent maximized frameless window from bleeding
  // into other monitors.
  if (IsMaximized() && !native_window_view_->has_frame()) {
    // This would be equivalent to calling:
    // DwmExtendFrameIntoClientArea({0, 0, 0, 0});
    //
    // which means do not extend window frame into client area. It is almost
    // a no-op, but it can tell Windows to not extend the window frame to be
    // larger than current workspace.
    //
    // See also:
    // https://devblogs.microsoft.com/oldnewthing/20150304-00/?p=44543
    *insets = gfx::Insets();
    return true;
  }
  return false;
}

bool ElectronDesktopWindowTreeHostWin::GetClientAreaInsets(
    gfx::Insets* insets,
    HMONITOR monitor) const {
  // Windows by default extends the maximized window slightly larger than
  // current workspace, for frameless window since the standard frame has been
  // removed, the client area would then be drew outside current workspace.
  //
  // Indenting the client area can fix this behavior.
  if (IsMaximized() && !native_window_view_->has_frame()) {
    // The insets would be eventually passed to WM_NCCALCSIZE, which takes
    // the metrics under the DPI of _main_ monitor instead of current monitor.
    //
    // Please make sure you tested maximized frameless window under multiple
    // monitors with different DPIs before changing this code.
    const int thickness = ::GetSystemMetrics(SM_CXSIZEFRAME) +
                          ::GetSystemMetrics(SM_CXPADDEDBORDER);
    *insets = gfx::Insets::TLBR(thickness, thickness, thickness, thickness);
    return true;
  }
  return false;
}

bool ElectronDesktopWindowTreeHostWin::HandleMouseEventForCaption(
    UINT message) const {
  // Windows does not seem to generate WM_NCPOINTERDOWN/UP touch events for
  // caption buttons with WCO. This results in a no-op for
  // HWNDMessageHandler::HandlePointerEventTypeTouchOrNonClient and
  // WM_SYSCOMMAND is not generated for the touch action. However, Windows will
  // also generate a mouse event for every touch action and this gets handled in
  // HWNDMessageHandler::HandleMouseEventInternal.
  // With https://chromium-review.googlesource.com/c/chromium/src/+/1048877/
  // Chromium lets the OS handle caption buttons for FrameMode::SYSTEM_DRAWN but
  // again this does not generate the SC_MINIMIZE, SC_MAXIMIZE, SC_RESTORE
  // commands when Non-client mouse events are generated for HTCLOSE,
  // HTMINBUTTON, HTMAXBUTTON. To workaround this issue, with this delegate we
  // let chromium handle the mouse events via
  // HWNDMessageHandler::HandleMouseInputForCaption instead of the OS and this
  // will generate the necessary system commands to perform caption button
  // actions due to the DefWindowProc call.
  // https://source.chromium.org/chromium/chromium/src/+/main:ui/views/win/hwnd_message_handler.cc;l=3611
  return native_window_view_->IsWindowControlsOverlayEnabled();
}

bool ElectronDesktopWindowTreeHostWin::HandleMouseEvent(ui::MouseEvent* event) {
  bool is_menu_right_click = event->IsRightMouseButton() &&
                             event->is_system_menu() &&
                             event->type() == ui::EventType::kMouseReleased;
  if (is_menu_right_click && !native_window_view_->has_frame()) {
    bool prevent_default = false;
    native_window_view_->NotifyWindowSystemContextMenu(event->x(), event->y(),
                                                       &prevent_default);
    // If the user prevents default behavior, emit contextmenu event to
    // allow bringing up the custom menu.
    if (prevent_default) {
      electron::api::WebContents::SetDisableDraggableRegions(true);
      views::DesktopWindowTreeHostWin::HandleMouseEvent(event);
    }
    return prevent_default;
  }

  return views::DesktopWindowTreeHostWin::HandleMouseEvent(event);
}

void ElectronDesktopWindowTreeHostWin::OnNativeThemeUpdated(
    ui::NativeTheme* observed_theme) {
  HWND hWnd = GetAcceleratedWidget();
  win::SetDarkModeForWindow(hWnd);

  auto* os_info = base::win::OSInfo::GetInstance();
  auto const version = os_info->version();

  // Toggle the nonclient area active state to force a redraw (Win10 workaround)
  if (version < base::win::Version::WIN11) {
    // When handling WM_NCACTIVATE messages, Chromium logical ORs the wParam and
    // the value of ShouldPaintAsActive() - so if the latter is true, it's not
    // possible to toggle the title bar to inactive. Force it to false while we
    // send the message so that the wParam value will always take effect. Refs
    // https://source.chromium.org/chromium/chromium/src/+/main:ui/views/win/hwnd_message_handler.cc;l=2332-2381;drc=e6361d070be0adc585ebbff89fec76e2df4ad768
    force_should_paint_as_active_ = false;
    ::SendMessage(hWnd, WM_NCACTIVATE, hWnd != ::GetActiveWindow(), 0);

    // Clear forced value and tell Chromium the value changed to get a repaint
    force_should_paint_as_active_.reset();
    PaintAsActiveChanged();
  }
}

bool ElectronDesktopWindowTreeHostWin::ShouldWindowContentsBeTransparent()
    const {
  // Window should be marked as opaque if no transparency setting has been
  // set, otherwise animations or videos rendered in the window will trigger a
  // DirectComposition redraw for every frame.
  // https://github.com/electron/electron/pull/39895
  return native_window_view_->GetOpacity() < 1.0 ||
         native_window_view_->IsTranslucent();
}

}  // namespace electron
