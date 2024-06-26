// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/win/notify_icon_host.h"

#include <commctrl.h>
#include <winuser.h>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/stl_util.h"
#include "base/timer/timer.h"
#include "base/win/win_util.h"
#include "base/win/windows_types.h"
#include "base/win/wrapped_window_proc.h"
#include "content/public/browser/browser_thread.h"
#include "shell/browser/ui/win/notify_icon.h"
#include "ui/display/screen.h"
#include "ui/events/event_constants.h"
#include "ui/events/win/system_event_state_lookup.h"
#include "ui/gfx/win/hwnd_util.h"

namespace electron {

namespace {

const UINT kNotifyIconMessage = WM_APP + 1;

// |kBaseIconId| is 2 to avoid conflicts with plugins that hard-code id 1.
const UINT kBaseIconId = 2;

const wchar_t kNotifyIconHostWindowClass[] = L"Electron_NotifyIconHostWindow";

constexpr unsigned int kMouseLeaveCheckFrequency = 250;

bool IsWinPressed() {
  return ((::GetKeyState(VK_LWIN) & 0x8000) == 0x8000) ||
         ((::GetKeyState(VK_RWIN) & 0x8000) == 0x8000);
}

int GetKeyboardModifiers() {
  int modifiers = ui::EF_NONE;
  if (ui::win::IsShiftPressed())
    modifiers |= ui::EF_SHIFT_DOWN;
  if (ui::win::IsCtrlPressed())
    modifiers |= ui::EF_CONTROL_DOWN;
  if (ui::win::IsAltPressed())
    modifiers |= ui::EF_ALT_DOWN;
  if (IsWinPressed())
    modifiers |= ui::EF_COMMAND_DOWN;
  return modifiers;
}

}  // namespace

// Helper class used to detect mouse entered and mouse exited events based on
// mouse move event.
class NotifyIconHost::MouseEnteredExitedDetector {
 public:
  MouseEnteredExitedDetector() = default;
  ~MouseEnteredExitedDetector() = default;

  // disallow copy
  MouseEnteredExitedDetector(const MouseEnteredExitedDetector&) = delete;
  MouseEnteredExitedDetector& operator=(const MouseEnteredExitedDetector&) =
      delete;

  // disallow move
  MouseEnteredExitedDetector(MouseEnteredExitedDetector&&) = delete;
  MouseEnteredExitedDetector& operator=(MouseEnteredExitedDetector&&) = delete;

  void MouseMoveEvent(raw_ptr<NotifyIcon> icon) {
    if (!icon)
      return;

    // If cursor is out of icon then skip this move event.
    if (!IsCursorOverIcon(icon))
      return;

    // If user moved cursor to other icon then send mouse exited event for
    // old icon.
    if (current_mouse_entered_ &&
        current_mouse_entered_->icon_id() != icon->icon_id()) {
      SendExitedEvent();
    }

    // If timer is running then cursor is already over icon and
    // CheckCursorPositionOverIcon will be repeatedly checking when to send
    // mouse exited event.
    if (mouse_exit_timer_.IsRunning())
      return;

    SendEnteredEvent(icon);

    // Start repeatedly checking when to send mouse exited event.
    StartObservingIcon(icon);
  }

  void IconRemoved(raw_ptr<NotifyIcon> icon) {
    if (current_mouse_entered_ &&
        current_mouse_entered_->icon_id() == icon->icon_id()) {
      SendExitedEvent();
    }
  }

 private:
  void SendEnteredEvent(raw_ptr<NotifyIcon> icon) {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(&NotifyIcon::HandleMouseEntered,
                                  icon->GetWeakPtr(), GetKeyboardModifiers()));
  }

  void SendExitedEvent() {
    mouse_exit_timer_.Stop();
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(&NotifyIcon::HandleMouseExited,
                                  std::move(current_mouse_entered_),
                                  GetKeyboardModifiers()));
  }

  bool IsCursorOverIcon(raw_ptr<NotifyIcon> icon) {
    gfx::Point cursor_pos =
        display::Screen::GetScreen()->GetCursorScreenPoint();
    return icon->GetBounds().Contains(cursor_pos);
  }

  void CheckCursorPositionOverIcon() {
    if (!current_mouse_entered_ ||
        IsCursorOverIcon(current_mouse_entered_.get()))
      return;

    SendExitedEvent();
  }

  void StartObservingIcon(raw_ptr<NotifyIcon> icon) {
    current_mouse_entered_ = icon->GetWeakPtr();
    mouse_exit_timer_.Start(
        FROM_HERE, base::Milliseconds(kMouseLeaveCheckFrequency),
        base::BindRepeating(
            &MouseEnteredExitedDetector::CheckCursorPositionOverIcon,
            weak_factory_.GetWeakPtr()));
  }

  // Timer used to check if cursor is still over the icon.
  base::MetronomeTimer mouse_exit_timer_;

  // Weak pointer to icon over which cursor is hovering.
  base::WeakPtr<NotifyIcon> current_mouse_entered_;

  base::WeakPtrFactory<MouseEnteredExitedDetector> weak_factory_{this};
};

NotifyIconHost::NotifyIconHost() {
  // Register our window class
  WNDCLASSEX window_class;
  base::win::InitializeWindowClass(
      kNotifyIconHostWindowClass,
      &base::win::WrappedWindowProc<NotifyIconHost::WndProcStatic>, 0, 0, 0,
      nullptr, nullptr, nullptr, nullptr, nullptr, &window_class);
  instance_ = window_class.hInstance;
  atom_ = RegisterClassEx(&window_class);
  CHECK(atom_);

  // If the taskbar is re-created after we start up, we have to rebuild all of
  // our icons.
  taskbar_created_message_ = RegisterWindowMessage(TEXT("TaskbarCreated"));

  // Create an offscreen window for handling messages for the status icons. We
  // create a hidden WS_POPUP window instead of an HWND_MESSAGE window, because
  // only top-level windows such as popups can receive broadcast messages like
  // "TaskbarCreated".
  window_ = CreateWindow(MAKEINTATOM(atom_), 0, WS_POPUP, 0, 0, 0, 0, 0, 0,
                         instance_, 0);
  gfx::CheckWindowCreated(window_, ::GetLastError());
  gfx::SetWindowUserData(window_, this);

  mouse_entered_exited_detector_ =
      std::make_unique<MouseEnteredExitedDetector>();
}

NotifyIconHost::~NotifyIconHost() {
  if (window_)
    DestroyWindow(window_);

  if (atom_)
    UnregisterClass(MAKEINTATOM(atom_), instance_);

  for (NotifyIcon* ptr : notify_icons_)
    delete ptr;
}

NotifyIcon* NotifyIconHost::CreateNotifyIcon(std::optional<UUID> guid) {
  if (guid.has_value()) {
    for (NotifyIcons::const_iterator i(notify_icons_.begin());
         i != notify_icons_.end(); ++i) {
      auto* current_win_icon = static_cast<NotifyIcon*>(*i);
      if (current_win_icon->guid() == guid.value()) {
        LOG(WARNING)
            << "Guid already in use. Existing tray entry will be replaced.";
      }
    }
  }

  auto* notify_icon =
      new NotifyIcon(this, NextIconId(), window_, kNotifyIconMessage,
                     guid.has_value() ? guid.value() : GUID_DEFAULT);

  notify_icons_.push_back(notify_icon);
  return notify_icon;
}

void NotifyIconHost::Remove(NotifyIcon* icon) {
  NotifyIcons::iterator i(
      std::find(notify_icons_.begin(), notify_icons_.end(), icon));

  if (i == notify_icons_.end()) {
    NOTREACHED();
  }

  mouse_entered_exited_detector_->IconRemoved(*i);

  notify_icons_.erase(i);
}

LRESULT CALLBACK NotifyIconHost::WndProcStatic(HWND hwnd,
                                               UINT message,
                                               WPARAM wparam,
                                               LPARAM lparam) {
  auto* msg_wnd =
      reinterpret_cast<NotifyIconHost*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  if (msg_wnd)
    return msg_wnd->WndProc(hwnd, message, wparam, lparam);
  else
    return ::DefWindowProc(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK NotifyIconHost::WndProc(HWND hwnd,
                                         UINT message,
                                         WPARAM wparam,
                                         LPARAM lparam) {
  if (message == taskbar_created_message_) {
    // We need to reset all of our icons because the taskbar went away.
    for (NotifyIcons::const_iterator i(notify_icons_.begin());
         i != notify_icons_.end(); ++i) {
      auto* win_icon = static_cast<NotifyIcon*>(*i);
      win_icon->ResetIcon();
    }
    return TRUE;
  } else if (message == kNotifyIconMessage) {
    NotifyIcon* win_icon = nullptr;

    // Find the selected status icon.
    for (NotifyIcons::const_iterator i(notify_icons_.begin());
         i != notify_icons_.end(); ++i) {
      auto* current_win_icon = static_cast<NotifyIcon*>(*i);
      if (current_win_icon->icon_id() == wparam) {
        win_icon = current_win_icon;
        break;
      }
    }

    // It is possible for this procedure to be called with an obsolete icon
    // id.  In that case we should just return early before handling any
    // actions.
    if (!win_icon)
      return TRUE;

    // We use a WeakPtr factory for NotifyIcons here so
    // that the callback is aware if the NotifyIcon gets
    // garbage-collected. This occurs when the tray gets
    // GC'd, and the BALLOON events below will not emit.
    base::WeakPtr<NotifyIcon> win_icon_weak = win_icon->GetWeakPtr();

    switch (lparam) {
      case NIN_BALLOONSHOW:
        content::GetUIThreadTaskRunner({})->PostTask(
            FROM_HERE,
            base::BindOnce(&NotifyIcon::NotifyBalloonShow, win_icon_weak));
        return TRUE;

      case NIN_BALLOONUSERCLICK:
        content::GetUIThreadTaskRunner({})->PostTask(
            FROM_HERE,
            base::BindOnce(&NotifyIcon::NotifyBalloonClicked, win_icon_weak));
        return TRUE;

      case NIN_BALLOONTIMEOUT:
        content::GetUIThreadTaskRunner({})->PostTask(
            FROM_HERE,
            base::BindOnce(&NotifyIcon::NotifyBalloonClosed, win_icon_weak));
        return TRUE;

      case WM_LBUTTONDOWN:
      case WM_RBUTTONDOWN:
      case WM_MBUTTONDOWN:
      case WM_LBUTTONDBLCLK:
      case WM_RBUTTONDBLCLK:
      case WM_MBUTTONDBLCLK:
      case WM_CONTEXTMENU:
        // Walk our icons, find which one was clicked on, and invoke its
        // HandleClickEvent() method.
        content::GetUIThreadTaskRunner({})->PostTask(
            FROM_HERE,
            base::BindOnce(
                &NotifyIcon::HandleClickEvent, win_icon_weak,
                GetKeyboardModifiers(),
                (lparam == WM_LBUTTONDOWN || lparam == WM_LBUTTONDBLCLK),
                (lparam == WM_LBUTTONDBLCLK || lparam == WM_RBUTTONDBLCLK),
                (lparam == WM_MBUTTONDOWN || lparam == WM_MBUTTONDBLCLK)));

        return TRUE;

      case WM_MOUSEMOVE:
        mouse_entered_exited_detector_->MouseMoveEvent(win_icon);

        content::GetUIThreadTaskRunner({})->PostTask(
            FROM_HERE, base::BindOnce(&NotifyIcon::HandleMouseMoveEvent,
                                      win_icon_weak, GetKeyboardModifiers()));
        return TRUE;
    }
  }
  return ::DefWindowProc(hwnd, message, wparam, lparam);
}

UINT NotifyIconHost::NextIconId() {
  UINT icon_id = next_icon_id_++;
  return kBaseIconId + icon_id;
}

}  // namespace electron
