// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// FIXME(ckerr) this incorrect #include order is a temporary
// fix to unblock the roll. Will fix in an upgrade followup.
#include "ui/base/ozone_buildflags.h"
#if BUILDFLAG(IS_OZONE_X11)
#include "ui/base/x/x11_util.h"
#endif

#include "shell/browser/native_window_views.h"

#if BUILDFLAG(IS_WIN)
#include <dwmapi.h>
#include <wrl/client.h>
#endif

#include <memory>
#include <utility>
#include <vector>

#include "base/containers/contains.h"
#include "base/memory/raw_ref.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/desktop_media_id.h"
#include "content/public/common/color_parser.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/ui/inspectable_web_contents.h"
#include "shell/browser/ui/views/root_view.h"
#include "shell/browser/web_contents_preferences.h"
#include "shell/browser/web_view_manager.h"
#include "shell/browser/window_list.h"
#include "shell/common/electron_constants.h"
#include "shell/common/gin_converters/image_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/options_switches.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/hit_test.h"
#include "ui/display/screen.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/views/background.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/native_widget_private.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/client_view.h"
#include "ui/wm/core/shadow_types.h"
#include "ui/wm/core/window_util.h"

#if BUILDFLAG(IS_LINUX)
#include "base/strings/string_util.h"
#include "shell/browser/browser.h"
#include "shell/browser/linux/unity_service.h"
#include "shell/browser/ui/electron_desktop_window_tree_host_linux.h"
#include "shell/browser/ui/views/client_frame_view_linux.h"
#include "shell/browser/ui/views/native_frame_view.h"
#include "shell/browser/ui/views/opaque_frame_view.h"
#include "shell/common/platform_util.h"
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#include "ui/views/window/native_frame_view.h"

#if BUILDFLAG(IS_OZONE_X11)
#include "shell/browser/ui/views/global_menu_bar_x11.h"
#include "shell/browser/ui/x/event_disabler.h"
#include "shell/browser/ui/x/x_window_utils.h"
#include "ui/gfx/x/atom_cache.h"
#include "ui/gfx/x/connection.h"
#include "ui/gfx/x/shape.h"
#include "ui/gfx/x/xproto.h"
#endif

#elif BUILDFLAG(IS_WIN)
#include "base/win/win_util.h"
#include "base/win/windows_version.h"
#include "shell/browser/ui/views/win_frame_view.h"
#include "shell/browser/ui/win/electron_desktop_native_widget_aura.h"
#include "skia/ext/skia_utils_win.h"
#include "ui/display/win/screen_win.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/win/hwnd_util.h"
#include "ui/gfx/win/msg_util.h"
#endif

namespace electron {

#if BUILDFLAG(IS_WIN)

DWM_SYSTEMBACKDROP_TYPE GetBackdropFromString(const std::string& material) {
  if (material == "none") {
    return DWMSBT_NONE;
  } else if (material == "acrylic") {
    return DWMSBT_TRANSIENTWINDOW;
  } else if (material == "mica") {
    return DWMSBT_MAINWINDOW;
  } else if (material == "tabbed") {
    return DWMSBT_TABBEDWINDOW;
  }
  return DWMSBT_AUTO;
}

// Similar to the ones in display::win::ScreenWin, but with rounded values
// These help to avoid problems that arise from unresizable windows where the
// original ceil()-ed values can cause calculation errors, since converting
// both ways goes through a ceil() call. Related issue: #15816
gfx::Rect ScreenToDIPRect(HWND hwnd, const gfx::Rect& pixel_bounds) {
  float scale_factor = display::win::ScreenWin::GetScaleFactorForHWND(hwnd);
  gfx::Rect dip_rect = ScaleToRoundedRect(pixel_bounds, 1.0f / scale_factor);
  dip_rect.set_origin(
      display::win::ScreenWin::ScreenToDIPRect(hwnd, pixel_bounds).origin());
  return dip_rect;
}

#endif

namespace {

#if BUILDFLAG(IS_WIN)
const LPCWSTR kUniqueTaskBarClassName = L"Shell_TrayWnd";

void FlipWindowStyle(HWND handle, bool on, DWORD flag) {
  DWORD style = ::GetWindowLong(handle, GWL_STYLE);
  if (on)
    style |= flag;
  else
    style &= ~flag;
  ::SetWindowLong(handle, GWL_STYLE, style);
  // Window's frame styles are cached so we need to call SetWindowPos
  // with the SWP_FRAMECHANGED flag to update cache properly.
  ::SetWindowPos(handle, 0, 0, 0, 0, 0,  // ignored
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

gfx::Rect DIPToScreenRect(HWND hwnd, const gfx::Rect& pixel_bounds) {
  float scale_factor = display::win::ScreenWin::GetScaleFactorForHWND(hwnd);
  gfx::Rect screen_rect = ScaleToRoundedRect(pixel_bounds, scale_factor);
  screen_rect.set_origin(
      display::win::ScreenWin::DIPToScreenRect(hwnd, pixel_bounds).origin());
  return screen_rect;
}

// Chromium uses a buggy implementation that converts content rect to window
// rect when calculating min/max size, we should use the same implementation
// when passing min/max size so we can get correct results.
gfx::Size WindowSizeToContentSizeBuggy(HWND hwnd, const gfx::Size& size) {
  // Calculate the size of window frame, using same code with the
  // HWNDMessageHandler::OnGetMinMaxInfo method.
  // The pitfall is, when window is minimized the calculated window frame size
  // will be different from other states.
  RECT client_rect, rect;
  GetClientRect(hwnd, &client_rect);
  GetWindowRect(hwnd, &rect);
  CR_DEFLATE_RECT(&rect, &client_rect);
  // Convert DIP size to pixel size, do calculation and then return DIP size.
  gfx::Rect screen_rect = DIPToScreenRect(hwnd, gfx::Rect(size));
  gfx::Size screen_client_size(screen_rect.width() - (rect.right - rect.left),
                               screen_rect.height() - (rect.bottom - rect.top));
  return ScreenToDIPRect(hwnd, gfx::Rect(screen_client_size)).size();
}

#endif

[[maybe_unused]] bool IsX11() {
  return ui::OzonePlatform::GetInstance()
      ->GetPlatformProperties()
      .electron_can_call_x11;
}

class NativeWindowClientView : public views::ClientView {
 public:
  NativeWindowClientView(views::Widget* widget,
                         views::View* root_view,
                         NativeWindowViews* window)
      : views::ClientView{widget, root_view},
        window_{raw_ref<NativeWindowViews>::from_ptr(window)} {}
  ~NativeWindowClientView() override = default;

  // disable copy
  NativeWindowClientView(const NativeWindowClientView&) = delete;
  NativeWindowClientView& operator=(const NativeWindowClientView&) = delete;

  // views::ClientView
  views::CloseRequestResult OnWindowCloseRequested() override {
    window_->NotifyWindowCloseButtonClicked();
    return views::CloseRequestResult::kCannotClose;
  }

 private:
  const raw_ref<NativeWindowViews> window_;
};

}  // namespace

NativeWindowViews::NativeWindowViews(const gin_helper::Dictionary& options,
                                     NativeWindow* parent)
    : NativeWindow(options, parent) {
  options.Get(options::kTitle, &title_);

  bool menu_bar_autohide;
  if (options.Get(options::kAutoHideMenuBar, &menu_bar_autohide))
    root_view_.SetAutoHideMenuBar(menu_bar_autohide);

#if BUILDFLAG(IS_WIN)
  // On Windows we rely on the CanResize() to indicate whether window can be
  // resized, and it should be set before window is created.
  options.Get(options::kResizable, &resizable_);
  options.Get(options::kMinimizable, &minimizable_);
  options.Get(options::kMaximizable, &maximizable_);

  // Transparent window must not have thick frame.
  options.Get("thickFrame", &thick_frame_);
  if (transparent())
    thick_frame_ = false;

  overlay_button_color_ = color_utils::GetSysSkColor(COLOR_BTNFACE);
  overlay_symbol_color_ = color_utils::GetSysSkColor(COLOR_BTNTEXT);
#endif

  v8::Local<v8::Value> titlebar_overlay;
  if (options.Get(options::ktitleBarOverlay, &titlebar_overlay) &&
      titlebar_overlay->IsObject()) {
    auto titlebar_overlay_obj =
        gin_helper::Dictionary::CreateEmpty(options.isolate());
    options.Get(options::ktitleBarOverlay, &titlebar_overlay_obj);

    std::string overlay_color_string;
    if (titlebar_overlay_obj.Get(options::kOverlayButtonColor,
                                 &overlay_color_string)) {
      bool success = content::ParseCssColorString(overlay_color_string,
                                                  &overlay_button_color_);
      DCHECK(success);
    }

    std::string overlay_symbol_color_string;
    if (titlebar_overlay_obj.Get(options::kOverlaySymbolColor,
                                 &overlay_symbol_color_string)) {
      bool success = content::ParseCssColorString(overlay_symbol_color_string,
                                                  &overlay_symbol_color_);
      DCHECK(success);
    }
  }

  // |hidden| is the only non-default titleBarStyle valid on Windows and Linux.
  if (title_bar_style_ == TitleBarStyle::kHidden)
    set_has_frame(false);

#if BUILDFLAG(IS_WIN)
  // If the taskbar is re-created after we start up, we have to rebuild all of
  // our buttons.
  taskbar_created_message_ = RegisterWindowMessage(TEXT("TaskbarCreated"));
#endif

  if (enable_larger_than_screen())
    // We need to set a default maximum window size here otherwise Windows
    // will not allow us to resize the window larger than scree.
    // Setting directly to INT_MAX somehow doesn't work, so we just divide
    // by 10, which should still be large enough.
    SetContentSizeConstraints(extensions::SizeConstraints(
        gfx::Size(), gfx::Size(INT_MAX / 10, INT_MAX / 10)));

  int width = 800, height = 600;
  options.Get(options::kWidth, &width);
  options.Get(options::kHeight, &height);
  gfx::Rect bounds(0, 0, width, height);
  widget_size_ = bounds.size();

  widget()->AddObserver(this);

  using InitParams = views::Widget::InitParams;
  auto params =
      InitParams{InitParams::CLIENT_OWNS_WIDGET, InitParams::TYPE_WINDOW};
  params.bounds = bounds;
  params.delegate = this;
  params.remove_standard_frame = !has_frame() || has_client_frame();

  // If a client frame, we need to draw our own shadows.
  if (IsTranslucent() || has_client_frame())
    params.opacity = InitParams::WindowOpacity::kTranslucent;

  // The given window is most likely not rectangular since it is translucent and
  // has no standard frame, don't show a shadow for it.
  if (IsTranslucent() && !has_frame())
    params.shadow_type = InitParams::ShadowType::kNone;

  bool focusable;
  if (options.Get(options::kFocusable, &focusable) && !focusable)
    params.activatable = InitParams::Activatable::kNo;

#if BUILDFLAG(IS_WIN)
  if (parent)
    params.parent = parent->GetNativeWindow();

  params.native_widget = new ElectronDesktopNativeWidgetAura(this);
#elif BUILDFLAG(IS_LINUX)
  std::string name = Browser::Get()->GetName();
  // Set WM_WINDOW_ROLE.
  params.wm_role_name = "browser-window";
  // Set WM_CLASS.
  params.wm_class_name = base::ToLowerASCII(name);
  params.wm_class_class = name;
  // Set Wayland application ID.
  params.wayland_app_id = platform_util::GetXdgAppId();

  auto* native_widget = new views::DesktopNativeWidgetAura(widget());
  params.native_widget = native_widget;
  params.desktop_window_tree_host =
      new ElectronDesktopWindowTreeHostLinux(this, native_widget);
#endif

  widget()->Init(std::move(params));
  widget()->SetNativeWindowProperty(kElectronNativeWindowKey, this);
  SetCanResize(resizable_);

  bool fullscreen = false;
  options.Get(options::kFullscreen, &fullscreen);

  std::string window_type;
  options.Get(options::kType, &window_type);

#if BUILDFLAG(IS_LINUX)
  // Set _GTK_THEME_VARIANT to dark if we have "dark-theme" option set.
  bool use_dark_theme = false;
  if (options.Get(options::kDarkTheme, &use_dark_theme) && use_dark_theme) {
    SetGTKDarkThemeEnabled(use_dark_theme);
  }

  if (parent)
    SetParentWindow(parent);

  if (IsX11()) {
    // Before the window is mapped the SetWMSpecState can not work, so we have
    // to manually set the _NET_WM_STATE.
    std::vector<x11::Atom> state_atom_list;

    // Before the window is mapped, there is no SHOW_FULLSCREEN_STATE.
    if (fullscreen) {
      state_atom_list.push_back(x11::GetAtom("_NET_WM_STATE_FULLSCREEN"));
    }

    if (parent) {
      // Force using dialog type for child window.
      window_type = "dialog";

      // Modal window needs the _NET_WM_STATE_MODAL hint.
      if (is_modal())
        state_atom_list.push_back(x11::GetAtom("_NET_WM_STATE_MODAL"));
    }

    if (!state_atom_list.empty()) {
      auto* connection = x11::Connection::Get();
      connection->SetArrayProperty(
          static_cast<x11::Window>(GetAcceleratedWidget()),
          x11::GetAtom("_NET_WM_STATE"), x11::Atom::ATOM, state_atom_list);
    }

    // Set the _NET_WM_WINDOW_TYPE.
    if (!window_type.empty())
      SetWindowType(static_cast<x11::Window>(GetAcceleratedWidget()),
                    window_type);
  }
#endif

#if BUILDFLAG(IS_WIN)
  if (!has_frame()) {
    // Set Window style so that we get a minimize and maximize animation when
    // frameless.
    DWORD frame_style = WS_CAPTION | WS_OVERLAPPED;
    if (resizable_)
      frame_style |= WS_THICKFRAME;
    if (minimizable_)
      frame_style |= WS_MINIMIZEBOX;
    if (maximizable_)
      frame_style |= WS_MAXIMIZEBOX;
    // We should not show a frame for transparent window.
    if (!thick_frame_)
      frame_style &= ~(WS_THICKFRAME | WS_CAPTION);
    ::SetWindowLong(GetAcceleratedWidget(), GWL_STYLE, frame_style);
  }

  LONG ex_style = ::GetWindowLong(GetAcceleratedWidget(), GWL_EXSTYLE);
  if (window_type == "toolbar")
    ex_style |= WS_EX_TOOLWINDOW;
  ::SetWindowLong(GetAcceleratedWidget(), GWL_EXSTYLE, ex_style);
#endif

  if (has_frame() && !has_client_frame()) {
    // TODO(zcbenz): This was used to force using native frame on Windows 2003,
    // we should check whether setting it in InitParams can work.
    widget()->set_frame_type(views::Widget::FrameType::kForceNative);
    widget()->FrameTypeChanged();
#if BUILDFLAG(IS_WIN)
    // thickFrame also works for normal window.
    if (!thick_frame_)
      FlipWindowStyle(GetAcceleratedWidget(), false, WS_THICKFRAME);
#endif
  }

  // Default content view.
  SetContentView(new views::View());

  gfx::Size size = bounds.size();
  if (has_frame() &&
      options.Get(options::kUseContentSize, &use_content_size_) &&
      use_content_size_)
    size = ContentBoundsToWindowBounds(gfx::Rect(size)).size();

  widget()->CenterWindow(size);

#if BUILDFLAG(IS_WIN)
  // Save initial window state.
  if (fullscreen)
    last_window_state_ = ui::SHOW_STATE_FULLSCREEN;
  else
    last_window_state_ = ui::SHOW_STATE_NORMAL;
#endif

  // Listen to mouse events.
  aura::Window* window = GetNativeWindow();
  if (window)
    window->AddPreTargetHandler(this);

#if BUILDFLAG(IS_LINUX)
  // On linux after the widget is initialized we might have to force set the
  // bounds if the bounds are smaller than the current display
  SetBounds(gfx::Rect(GetPosition(), bounds.size()), false);
#endif

  SetOwnedByWidget(false);
  RegisterDeleteDelegateCallback(base::BindOnce(
      [](NativeWindowViews* window) {
        if (window->is_modal() && window->parent()) {
          auto* parent = window->parent();
          // Enable parent window after current window gets closed.
          static_cast<NativeWindowViews*>(parent)->DecrementChildModals();
          // Focus on parent window.
          parent->Focus(true);
        }

        window->NotifyWindowClosed();
      },
      this));
}

NativeWindowViews::~NativeWindowViews() {
  widget()->RemoveObserver(this);

#if BUILDFLAG(IS_WIN)
  // Disable mouse forwarding to relinquish resources, should any be held.
  SetForwardMouseMessages(false);
#endif

  aura::Window* window = GetNativeWindow();
  if (window)
    window->RemovePreTargetHandler(this);
}

void NativeWindowViews::SetGTKDarkThemeEnabled(bool use_dark_theme) {
#if BUILDFLAG(IS_LINUX)
  if (IsX11()) {
    const std::string color = use_dark_theme ? "dark" : "light";
    auto* connection = x11::Connection::Get();
    connection->SetStringProperty(
        static_cast<x11::Window>(GetAcceleratedWidget()),
        x11::GetAtom("_GTK_THEME_VARIANT"), x11::GetAtom("UTF8_STRING"), color);
  }
#endif
}

void NativeWindowViews::SetContentView(views::View* view) {
  if (content_view()) {
    root_view_.GetMainView()->RemoveChildView(content_view());
  }
  set_content_view(view);
  focused_view_ = view;
  root_view_.GetMainView()->AddChildView(content_view());
  root_view_.GetMainView()->DeprecatedLayoutImmediately();
}

void NativeWindowViews::Close() {
  if (!IsClosable()) {
    WindowList::WindowCloseCancelled(this);
    return;
  }

  widget()->Close();
}

void NativeWindowViews::CloseImmediately() {
  widget()->CloseNow();
}

void NativeWindowViews::Focus(bool focus) {
  // For hidden window focus() should do nothing.
  if (!IsVisible())
    return;

  if (focus) {
    widget()->Activate();
  } else {
    widget()->Deactivate();
  }
}

bool NativeWindowViews::IsFocused() const {
  return widget()->IsActive();
}

void NativeWindowViews::Show() {
  if (is_modal() && NativeWindow::parent() &&
      !widget()->native_widget_private()->IsVisible())
    static_cast<NativeWindowViews*>(parent())->IncrementChildModals();

  widget()->native_widget_private()->Show(GetRestoredState(), gfx::Rect());

  // explicitly focus the window
  widget()->Activate();

  NotifyWindowShow();

#if BUILDFLAG(IS_LINUX)
  if (global_menu_bar_)
    global_menu_bar_->OnWindowMapped();

  // On X11, setting Z order before showing the window doesn't take effect,
  // so we have to call it again.
  if (IsX11())
    widget()->SetZOrderLevel(widget()->GetZOrderLevel());
#endif
}

void NativeWindowViews::ShowInactive() {
  widget()->ShowInactive();

  NotifyWindowShow();

#if BUILDFLAG(IS_LINUX)
  if (global_menu_bar_)
    global_menu_bar_->OnWindowMapped();
#endif
}

void NativeWindowViews::Hide() {
  if (is_modal() && NativeWindow::parent())
    static_cast<NativeWindowViews*>(parent())->DecrementChildModals();

  widget()->Hide();

  NotifyWindowHide();

#if BUILDFLAG(IS_LINUX)
  if (global_menu_bar_)
    global_menu_bar_->OnWindowUnmapped();
#endif

#if BUILDFLAG(IS_WIN)
  // When the window is removed from the taskbar via win.hide(),
  // the thumbnail buttons need to be set up again.
  // Ensure that when the window is hidden,
  // the taskbar host is notified that it should re-add them.
  taskbar_host_.SetThumbarButtonsAdded(false);
#endif
}

bool NativeWindowViews::IsVisible() const {
#if BUILDFLAG(IS_WIN)
  // widget()->IsVisible() calls ::IsWindowVisible, which returns non-zero if a
  // window or any of its parent windows are visible. We want to only check the
  // current window.
  bool visible =
      ::GetWindowLong(GetAcceleratedWidget(), GWL_STYLE) & WS_VISIBLE;
  // WS_VISIBLE is true even if a window is miminized - explicitly check that.
  return visible && !IsMinimized();
#else
  return widget()->IsVisible();
#endif
}

bool NativeWindowViews::IsEnabled() const {
#if BUILDFLAG(IS_WIN)
  return ::IsWindowEnabled(GetAcceleratedWidget());
#elif BUILDFLAG(IS_LINUX)
  if (IsX11())
    return !event_disabler_.get();
  NOTIMPLEMENTED();
  return true;
#endif
}

void NativeWindowViews::IncrementChildModals() {
  num_modal_children_++;
  SetEnabledInternal(ShouldBeEnabled());
}

void NativeWindowViews::DecrementChildModals() {
  if (num_modal_children_ > 0) {
    num_modal_children_--;
  }
  SetEnabledInternal(ShouldBeEnabled());
}

void NativeWindowViews::SetEnabled(bool enable) {
  if (enable != is_enabled_) {
    is_enabled_ = enable;
    SetEnabledInternal(ShouldBeEnabled());
  }
}

bool NativeWindowViews::ShouldBeEnabled() const {
  return is_enabled_ && (num_modal_children_ == 0);
}

void NativeWindowViews::SetEnabledInternal(bool enable) {
  if (enable && IsEnabled()) {
    return;
  } else if (!enable && !IsEnabled()) {
    return;
  }

#if BUILDFLAG(IS_WIN)
  ::EnableWindow(GetAcceleratedWidget(), enable);
#else
  if (IsX11()) {
    views::DesktopWindowTreeHostPlatform* tree_host =
        views::DesktopWindowTreeHostLinux::GetHostForWidget(
            GetAcceleratedWidget());
    if (enable) {
      tree_host->RemoveEventRewriter(event_disabler_.get());
      event_disabler_.reset();
    } else {
      event_disabler_ = std::make_unique<EventDisabler>();
      tree_host->AddEventRewriter(event_disabler_.get());
    }
  }
#endif
}

#if BUILDFLAG(IS_LINUX)
void NativeWindowViews::Maximize() {
  if (IsVisible()) {
    widget()->Maximize();
  } else {
    widget()->native_widget_private()->Show(ui::SHOW_STATE_MAXIMIZED,
                                            gfx::Rect());
    NotifyWindowShow();
  }
}
#endif

void NativeWindowViews::Unmaximize() {
  if (IsMaximized()) {
#if BUILDFLAG(IS_WIN)
    if (transparent()) {
      SetBounds(restore_bounds_, false);
      NotifyWindowUnmaximize();
      UpdateThickFrame();
      return;
    }
#endif

    widget()->Restore();

#if BUILDFLAG(IS_WIN)
    UpdateThickFrame();
#endif
  }
}

bool NativeWindowViews::IsMaximized() const {
  if (widget()->IsMaximized()) {
    return true;
  } else {
#if BUILDFLAG(IS_WIN)
    if (transparent() && !IsMinimized()) {
      // Compare the size of the window with the size of the display
      auto display = display::Screen::GetScreen()->GetDisplayNearestWindow(
          GetNativeWindow());
      // Maximized if the window is the same dimensions and placement as the
      // display
      return GetBounds() == display.work_area();
    }
#endif

    return false;
  }
}

void NativeWindowViews::Minimize() {
  if (IsVisible())
    widget()->Minimize();
  else
    widget()->native_widget_private()->Show(ui::SHOW_STATE_MINIMIZED,
                                            gfx::Rect());
}

void NativeWindowViews::Restore() {
  widget()->Restore();

#if BUILDFLAG(IS_WIN)
  UpdateThickFrame();
#endif
}

bool NativeWindowViews::IsMinimized() const {
  return widget()->IsMinimized();
}

void NativeWindowViews::SetFullScreen(bool fullscreen) {
  if (!IsFullScreenable())
    return;

#if BUILDFLAG(IS_WIN)
  // There is no native fullscreen state on Windows.
  bool leaving_fullscreen = IsFullscreen() && !fullscreen;

  if (fullscreen) {
    last_window_state_ = ui::SHOW_STATE_FULLSCREEN;
    NotifyWindowEnterFullScreen();
  } else {
    last_window_state_ = ui::SHOW_STATE_NORMAL;
    NotifyWindowLeaveFullScreen();
  }

  // For window without WS_THICKFRAME style, we can not call SetFullscreen().
  // This path will be used for transparent windows as well.
  if (!thick_frame_) {
    if (fullscreen) {
      restore_bounds_ = GetBounds();
      auto display =
          display::Screen::GetScreen()->GetDisplayNearestPoint(GetPosition());
      SetBounds(display.bounds(), false);
    } else {
      SetBounds(restore_bounds_, false);
    }
    return;
  }

  // We set the new value after notifying, so we can handle the size event
  // correctly.
  widget()->SetFullscreen(fullscreen);

  // If restoring from fullscreen and the window isn't visible, force visible,
  // else a non-responsive window shell could be rendered.
  // (this situation may arise when app starts with fullscreen: true)
  // Note: the following must be after "widget()->SetFullscreen(fullscreen);"
  if (leaving_fullscreen && !IsVisible())
    FlipWindowStyle(GetAcceleratedWidget(), true, WS_VISIBLE);
#else
  if (IsVisible())
    widget()->SetFullscreen(fullscreen);
  else if (fullscreen)
    widget()->native_widget_private()->Show(ui::SHOW_STATE_FULLSCREEN,
                                            gfx::Rect());

  // Auto-hide menubar when in fullscreen.
  if (fullscreen) {
    menu_bar_visible_before_fullscreen_ = IsMenuBarVisible();
    SetMenuBarVisibility(false);
  } else {
    SetMenuBarVisibility(!IsMenuBarAutoHide() &&
                         menu_bar_visible_before_fullscreen_);
    menu_bar_visible_before_fullscreen_ = false;
  }
#endif
}

bool NativeWindowViews::IsFullscreen() const {
  return widget()->IsFullscreen();
}

void NativeWindowViews::SetBounds(const gfx::Rect& bounds, bool animate) {
#if BUILDFLAG(IS_WIN)
  if (is_moving_ || is_resizing_) {
    pending_bounds_change_ = bounds;
  }
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
  // On Linux and Windows the minimum and maximum size should be updated with
  // window size when window is not resizable.
  if (!resizable_) {
    SetMaximumSize(bounds.size());
    SetMinimumSize(bounds.size());
  }
#endif

  widget()->SetBounds(bounds);
}

gfx::Rect NativeWindowViews::GetBounds() const {
#if BUILDFLAG(IS_WIN)
  if (IsMinimized())
    return widget()->GetRestoredBounds();
#endif

  return widget()->GetWindowBoundsInScreen();
}

gfx::Rect NativeWindowViews::GetContentBounds() const {
  return content_view() ? content_view()->GetBoundsInScreen() : gfx::Rect();
}

gfx::Size NativeWindowViews::GetContentSize() const {
#if BUILDFLAG(IS_WIN)
  if (IsMinimized())
    return NativeWindow::GetContentSize();
#endif

  return content_view() ? content_view()->size() : gfx::Size();
}

gfx::Rect NativeWindowViews::GetNormalBounds() const {
#if BUILDFLAG(IS_WIN)
  if (IsMaximized() && transparent())
    return restore_bounds_;
#endif
  return widget()->GetRestoredBounds();
}

void NativeWindowViews::SetContentSizeConstraints(
    const extensions::SizeConstraints& size_constraints) {
  NativeWindow::SetContentSizeConstraints(size_constraints);
#if BUILDFLAG(IS_WIN)
  // Changing size constraints would force adding the WS_THICKFRAME style, so
  // do nothing if thickFrame is false.
  if (!thick_frame_)
    return;
#endif
  // widget_delegate() is only available after Init() is called, we make use of
  // this to determine whether native widget has initialized.
  if (widget() && widget()->widget_delegate())
    widget()->OnSizeConstraintsChanged();
  if (resizable_)
    old_size_constraints_ = size_constraints;
}

#if BUILDFLAG(IS_WIN)
// This override does almost the same with its parent, except that it uses
// the WindowSizeToContentSizeBuggy method to convert window size to content
// size. See the comment of the method for the reason behind this.
extensions::SizeConstraints NativeWindowViews::GetContentSizeConstraints()
    const {
  if (content_size_constraints_)
    return *content_size_constraints_;
  if (!size_constraints_)
    return extensions::SizeConstraints();
  extensions::SizeConstraints constraints;
  if (size_constraints_->HasMaximumSize()) {
    constraints.set_maximum_size(WindowSizeToContentSizeBuggy(
        GetAcceleratedWidget(), size_constraints_->GetMaximumSize()));
  }
  if (size_constraints_->HasMinimumSize()) {
    constraints.set_minimum_size(WindowSizeToContentSizeBuggy(
        GetAcceleratedWidget(), size_constraints_->GetMinimumSize()));
  }
  return constraints;
}
#endif

void NativeWindowViews::SetResizable(bool resizable) {
  if (resizable != resizable_) {
    // On Linux there is no "resizable" property of a window, we have to set
    // both the minimum and maximum size to the window size to achieve it.
    if (resizable) {
      SetContentSizeConstraints(old_size_constraints_);
      SetMaximizable(maximizable_);
    } else {
      old_size_constraints_ = GetContentSizeConstraints();
      resizable_ = false;
      gfx::Size content_size = GetContentSize();
      SetContentSizeConstraints(
          extensions::SizeConstraints(content_size, content_size));
    }
  }

  resizable_ = resizable;
  SetCanResize(resizable_);

#if BUILDFLAG(IS_WIN)
  UpdateThickFrame();
#endif
}

bool NativeWindowViews::MoveAbove(const std::string& sourceId) {
  const content::DesktopMediaID id = content::DesktopMediaID::Parse(sourceId);
  if (id.type != content::DesktopMediaID::TYPE_WINDOW)
    return false;

#if BUILDFLAG(IS_WIN)
  const HWND otherWindow = reinterpret_cast<HWND>(id.id);
  if (!::IsWindow(otherWindow))
    return false;

  ::SetWindowPos(GetAcceleratedWidget(), GetWindow(otherWindow, GW_HWNDPREV), 0,
                 0, 0, 0,
                 SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
#else
  if (IsX11()) {
    if (!IsWindowValid(static_cast<x11::Window>(id.id)))
      return false;

    electron::MoveWindowAbove(static_cast<x11::Window>(GetAcceleratedWidget()),
                              static_cast<x11::Window>(id.id));
  }
#endif

  return true;
}

void NativeWindowViews::MoveTop() {
// TODO(julien.isorce): fix chromium in order to use existing
// widget()->StackAtTop().
#if BUILDFLAG(IS_WIN)
  gfx::Point pos = GetPosition();
  gfx::Size size = GetSize();
  ::SetWindowPos(GetAcceleratedWidget(), HWND_TOP, pos.x(), pos.y(),
                 size.width(), size.height(),
                 SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
#else
  if (IsX11())
    electron::MoveWindowToForeground(
        static_cast<x11::Window>(GetAcceleratedWidget()));
#endif
}

bool NativeWindowViews::IsResizable() const {
#if BUILDFLAG(IS_WIN)
  if (has_frame())
    return ::GetWindowLong(GetAcceleratedWidget(), GWL_STYLE) & WS_THICKFRAME;
#endif
  return resizable_;
}

void NativeWindowViews::SetAspectRatio(double aspect_ratio,
                                       const gfx::Size& extra_size) {
  NativeWindow::SetAspectRatio(aspect_ratio, extra_size);
  gfx::SizeF aspect(aspect_ratio, 1.0);
  // Scale up because SetAspectRatio() truncates aspect value to int
  aspect.Scale(100);

  widget()->SetAspectRatio(aspect);
}

void NativeWindowViews::SetMovable(bool movable) {
  movable_ = movable;
}

bool NativeWindowViews::IsMovable() const {
#if BUILDFLAG(IS_WIN)
  return movable_;
#else
  return true;  // Not implemented on Linux.
#endif
}

void NativeWindowViews::SetMinimizable(bool minimizable) {
#if BUILDFLAG(IS_WIN)
  FlipWindowStyle(GetAcceleratedWidget(), minimizable, WS_MINIMIZEBOX);
  if (IsWindowControlsOverlayEnabled()) {
    auto* frame_view =
        static_cast<WinFrameView*>(widget()->non_client_view()->frame_view());
    frame_view->caption_button_container()->UpdateButtons();
  }
#endif
  minimizable_ = minimizable;
}

bool NativeWindowViews::IsMinimizable() const {
#if BUILDFLAG(IS_WIN)
  return ::GetWindowLong(GetAcceleratedWidget(), GWL_STYLE) & WS_MINIMIZEBOX;
#else
  return true;  // Not implemented on Linux.
#endif
}

void NativeWindowViews::SetMaximizable(bool maximizable) {
#if BUILDFLAG(IS_WIN)
  FlipWindowStyle(GetAcceleratedWidget(), maximizable, WS_MAXIMIZEBOX);
  if (IsWindowControlsOverlayEnabled()) {
    auto* frame_view =
        static_cast<WinFrameView*>(widget()->non_client_view()->frame_view());
    frame_view->caption_button_container()->UpdateButtons();
  }
#endif
  maximizable_ = maximizable;
}

bool NativeWindowViews::IsMaximizable() const {
#if BUILDFLAG(IS_WIN)
  return ::GetWindowLong(GetAcceleratedWidget(), GWL_STYLE) & WS_MAXIMIZEBOX;
#else
  return true;  // Not implemented on Linux.
#endif
}

void NativeWindowViews::SetExcludedFromShownWindowsMenu(bool excluded) {}

bool NativeWindowViews::IsExcludedFromShownWindowsMenu() const {
  // return false on unsupported platforms
  return false;
}

void NativeWindowViews::SetFullScreenable(bool fullscreenable) {
  fullscreenable_ = fullscreenable;
}

bool NativeWindowViews::IsFullScreenable() const {
  return fullscreenable_;
}

void NativeWindowViews::SetClosable(bool closable) {
#if BUILDFLAG(IS_WIN)
  HMENU menu = GetSystemMenu(GetAcceleratedWidget(), false);
  if (closable) {
    EnableMenuItem(menu, SC_CLOSE, MF_BYCOMMAND | MF_ENABLED);
  } else {
    EnableMenuItem(menu, SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
  }
  if (IsWindowControlsOverlayEnabled()) {
    auto* frame_view =
        static_cast<WinFrameView*>(widget()->non_client_view()->frame_view());
    frame_view->caption_button_container()->UpdateButtons();
  }
#endif
}

bool NativeWindowViews::IsClosable() const {
#if BUILDFLAG(IS_WIN)
  HMENU menu = GetSystemMenu(GetAcceleratedWidget(), false);
  MENUITEMINFO info;
  memset(&info, 0, sizeof(info));
  info.cbSize = sizeof(info);
  info.fMask = MIIM_STATE;
  if (!GetMenuItemInfo(menu, SC_CLOSE, false, &info)) {
    return false;
  }
  return !(info.fState & MFS_DISABLED);
#elif BUILDFLAG(IS_LINUX)
  return true;
#endif
}

void NativeWindowViews::SetAlwaysOnTop(ui::ZOrderLevel z_order,
                                       const std::string& level,
                                       int relativeLevel) {
  bool level_changed = z_order != widget()->GetZOrderLevel();
  widget()->SetZOrderLevel(z_order);

#if BUILDFLAG(IS_WIN)
  // Reset the placement flag.
  behind_task_bar_ = false;
  if (z_order != ui::ZOrderLevel::kNormal) {
    // On macOS the window is placed behind the Dock for the following levels.
    // Re-use the same names on Windows to make it easier for the user.
    static const std::vector<std::string> levels = {
        "floating", "torn-off-menu", "modal-panel", "main-menu", "status"};
    behind_task_bar_ = base::Contains(levels, level);
  }
#endif
  MoveBehindTaskBarIfNeeded();

  // This must be notified at the very end or IsAlwaysOnTop
  // will not yet have been updated to reflect the new status
  if (level_changed)
    NativeWindow::NotifyWindowAlwaysOnTopChanged();
}

ui::ZOrderLevel NativeWindowViews::GetZOrderLevel() const {
  return widget()->GetZOrderLevel();
}

// We previous called widget()->CenterWindow() here, but in
// Chromium CL 4916277 behavior was changed to center relative to the
// parent window if there is one. We want to keep the old behavior
// for now to avoid breaking API contract, but should consider the long
// term plan for this aligning with upstream.
void NativeWindowViews::Center() {
#if BUILDFLAG(IS_LINUX)
  auto display =
      display::Screen::GetScreen()->GetDisplayNearestWindow(GetNativeWindow());
  gfx::Rect window_bounds_in_screen = display.work_area();
  window_bounds_in_screen.ClampToCenteredSize(GetSize());
  widget()->SetBounds(window_bounds_in_screen);
#else
  HWND hwnd = GetAcceleratedWidget();
  gfx::Size size = display::win::ScreenWin::DIPToScreenSize(hwnd, GetSize());
  gfx::CenterAndSizeWindow(nullptr, hwnd, size);
#endif
}

void NativeWindowViews::Invalidate() {
  widget()->SchedulePaintInRect(gfx::Rect(GetBounds().size()));
}

void NativeWindowViews::SetTitle(const std::string& title) {
  title_ = title;
  widget()->UpdateWindowTitle();
}

std::string NativeWindowViews::GetTitle() const {
  return title_;
}

void NativeWindowViews::FlashFrame(bool flash) {
#if BUILDFLAG(IS_WIN)
  // The Chromium's implementation has a bug stopping flash.
  if (!flash) {
    FLASHWINFO fwi;
    fwi.cbSize = sizeof(fwi);
    fwi.hwnd = GetAcceleratedWidget();
    fwi.dwFlags = FLASHW_STOP;
    fwi.uCount = 0;
    FlashWindowEx(&fwi);
    return;
  }
#endif
  widget()->FlashFrame(flash);
}

void NativeWindowViews::SetSkipTaskbar(bool skip) {
#if BUILDFLAG(IS_WIN)
  Microsoft::WRL::ComPtr<ITaskbarList> taskbar;
  if (FAILED(::CoCreateInstance(CLSID_TaskbarList, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&taskbar))) ||
      FAILED(taskbar->HrInit()))
    return;
  if (skip) {
    taskbar->DeleteTab(GetAcceleratedWidget());
  } else {
    taskbar->AddTab(GetAcceleratedWidget());
    taskbar_host_.RestoreThumbarButtons(GetAcceleratedWidget());
  }
#endif
}

void NativeWindowViews::SetSimpleFullScreen(bool simple_fullscreen) {
  SetFullScreen(simple_fullscreen);
}

bool NativeWindowViews::IsSimpleFullScreen() const {
  return IsFullscreen();
}

void NativeWindowViews::SetKiosk(bool kiosk) {
  SetFullScreen(kiosk);
}

bool NativeWindowViews::IsKiosk() const {
  return IsFullscreen();
}

bool NativeWindowViews::IsTabletMode() const {
#if BUILDFLAG(IS_WIN)
  return base::win::IsWindows10OrGreaterTabletMode(GetAcceleratedWidget());
#else
  return false;
#endif
}

SkColor NativeWindowViews::GetBackgroundColor() const {
  auto* background = root_view_.background();
  if (!background)
    return SK_ColorTRANSPARENT;
  return background->get_color();
}

void NativeWindowViews::SetBackgroundColor(SkColor background_color) {
  // web views' background color.
  root_view_.SetBackground(views::CreateSolidBackground(background_color));

#if BUILDFLAG(IS_WIN)
  // Set the background color of native window.
  HBRUSH brush = CreateSolidBrush(skia::SkColorToCOLORREF(background_color));
  ULONG_PTR previous_brush =
      SetClassLongPtr(GetAcceleratedWidget(), GCLP_HBRBACKGROUND,
                      reinterpret_cast<LONG_PTR>(brush));
  if (previous_brush)
    DeleteObject((HBRUSH)previous_brush);
  InvalidateRect(GetAcceleratedWidget(), nullptr, 1);
#endif
}

void NativeWindowViews::SetHasShadow(bool has_shadow) {
  wm::SetShadowElevation(GetNativeWindow(),
                         has_shadow ? wm::kShadowElevationInactiveWindow
                                    : wm::kShadowElevationNone);
}

bool NativeWindowViews::HasShadow() const {
  return GetNativeWindow()->GetProperty(wm::kShadowElevationKey) !=
         wm::kShadowElevationNone;
}

void NativeWindowViews::SetOpacity(const double opacity) {
#if BUILDFLAG(IS_WIN)
  const double boundedOpacity = std::clamp(opacity, 0.0, 1.0);
  HWND hwnd = GetAcceleratedWidget();
  if (!layered_) {
    LONG ex_style = ::GetWindowLong(hwnd, GWL_EXSTYLE);
    ex_style |= WS_EX_LAYERED;
    ::SetWindowLong(hwnd, GWL_EXSTYLE, ex_style);
    layered_ = true;
  }
  ::SetLayeredWindowAttributes(hwnd, 0, boundedOpacity * 255, LWA_ALPHA);
  opacity_ = boundedOpacity;
#else
  opacity_ = 1.0;  // setOpacity unsupported on Linux
#endif
}

double NativeWindowViews::GetOpacity() const {
  return opacity_;
}

void NativeWindowViews::SetIgnoreMouseEvents(bool ignore, bool forward) {
#if BUILDFLAG(IS_WIN)
  LONG ex_style = ::GetWindowLong(GetAcceleratedWidget(), GWL_EXSTYLE);
  if (ignore)
    ex_style |= (WS_EX_TRANSPARENT | WS_EX_LAYERED);
  else
    ex_style &= ~(WS_EX_TRANSPARENT | WS_EX_LAYERED);
  if (layered_)
    ex_style |= WS_EX_LAYERED;
  ::SetWindowLong(GetAcceleratedWidget(), GWL_EXSTYLE, ex_style);

  // Forwarding is always disabled when not ignoring mouse messages.
  if (!ignore) {
    SetForwardMouseMessages(false);
  } else {
    SetForwardMouseMessages(forward);
  }
#else
  if (IsX11()) {
    auto* connection = x11::Connection::Get();
    if (ignore) {
      x11::Rectangle r{0, 0, 1, 1};
      connection->shape().Rectangles({
          .operation = x11::Shape::So::Set,
          .destination_kind = x11::Shape::Sk::Input,
          .ordering = x11::ClipOrdering::YXBanded,
          .destination_window =
              static_cast<x11::Window>(GetAcceleratedWidget()),
          .rectangles = {r},
      });
    } else {
      connection->shape().Mask({
          .operation = x11::Shape::So::Set,
          .destination_kind = x11::Shape::Sk::Input,
          .destination_window =
              static_cast<x11::Window>(GetAcceleratedWidget()),
          .source_bitmap = x11::Pixmap::None,
      });
    }
  }
#endif
}

void NativeWindowViews::SetContentProtection(bool enable) {
#if BUILDFLAG(IS_WIN)
  HWND hwnd = GetAcceleratedWidget();
  DWORD affinity = enable ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
  ::SetWindowDisplayAffinity(hwnd, affinity);
  if (!layered_) {
    // Workaround to prevent black window on screen capture after hiding and
    // showing the BrowserWindow.
    LONG ex_style = ::GetWindowLong(hwnd, GWL_EXSTYLE);
    ex_style |= WS_EX_LAYERED;
    ::SetWindowLong(hwnd, GWL_EXSTYLE, ex_style);
    layered_ = true;
  }
#endif
}

void NativeWindowViews::SetFocusable(bool focusable) {
  widget()->widget_delegate()->SetCanActivate(focusable);
#if BUILDFLAG(IS_WIN)
  LONG ex_style = ::GetWindowLong(GetAcceleratedWidget(), GWL_EXSTYLE);
  if (focusable)
    ex_style &= ~WS_EX_NOACTIVATE;
  else
    ex_style |= WS_EX_NOACTIVATE;
  ::SetWindowLong(GetAcceleratedWidget(), GWL_EXSTYLE, ex_style);
  SetSkipTaskbar(!focusable);
  Focus(false);
#endif
}

bool NativeWindowViews::IsFocusable() const {
  bool can_activate = widget()->widget_delegate()->CanActivate();
#if BUILDFLAG(IS_WIN)
  LONG ex_style = ::GetWindowLong(GetAcceleratedWidget(), GWL_EXSTYLE);
  bool no_activate = ex_style & WS_EX_NOACTIVATE;
  return !no_activate && can_activate;
#else
  return can_activate;
#endif
}

void NativeWindowViews::SetMenu(ElectronMenuModel* menu_model) {
#if BUILDFLAG(IS_LINUX)
  // Remove global menu bar.
  if (global_menu_bar_ && menu_model == nullptr) {
    global_menu_bar_.reset();
    root_view_.UnregisterAcceleratorsWithFocusManager();
    return;
  }

  // Use global application menu bar when possible.
  bool can_use_global_menus = ui::OzonePlatform::GetInstance()
                                  ->GetPlatformProperties()
                                  .supports_global_application_menus;
  if (can_use_global_menus && ShouldUseGlobalMenuBar()) {
    if (!global_menu_bar_)
      global_menu_bar_ = std::make_unique<GlobalMenuBarX11>(this);
    if (global_menu_bar_->IsServerStarted()) {
      root_view_.RegisterAcceleratorsWithFocusManager(menu_model);
      global_menu_bar_->SetMenu(menu_model);
      return;
    }
  }
#endif

  // Should reset content size when setting menu.
  gfx::Size content_size = GetContentSize();
  bool should_reset_size = use_content_size_ && has_frame() &&
                           !IsMenuBarAutoHide() &&
                           ((!!menu_model) != root_view_.HasMenu());

  root_view_.SetMenu(menu_model);

  if (should_reset_size) {
    // Enlarge the size constraints for the menu.
    int menu_bar_height = root_view_.GetMenuBarHeight();
    extensions::SizeConstraints constraints = GetContentSizeConstraints();
    if (constraints.HasMinimumSize()) {
      gfx::Size min_size = constraints.GetMinimumSize();
      min_size.set_height(min_size.height() + menu_bar_height);
      constraints.set_minimum_size(min_size);
    }
    if (constraints.HasMaximumSize()) {
      gfx::Size max_size = constraints.GetMaximumSize();
      max_size.set_height(max_size.height() + menu_bar_height);
      constraints.set_maximum_size(max_size);
    }
    SetContentSizeConstraints(constraints);

    // Resize the window to make sure content size is not changed.
    SetContentSize(content_size);
  }
}

void NativeWindowViews::SetParentWindow(NativeWindow* parent) {
  NativeWindow::SetParentWindow(parent);

#if BUILDFLAG(IS_LINUX)
  if (IsX11()) {
    auto* connection = x11::Connection::Get();
    connection->SetProperty(
        static_cast<x11::Window>(GetAcceleratedWidget()),
        x11::Atom::WM_TRANSIENT_FOR, x11::Atom::WINDOW,
        parent ? static_cast<x11::Window>(parent->GetAcceleratedWidget())
               : ui::GetX11RootWindow());
  }
#elif BUILDFLAG(IS_WIN)
  // To set parentship between windows into Windows is better to play with the
  //  owner instead of the parent, as Windows natively seems to do if a parent
  //  is specified at window creation time.
  // For do this we must NOT use the ::SetParent function, instead we must use
  //  the ::GetWindowLongPtr or ::SetWindowLongPtr functions with "nIndex" set
  //  to "GWLP_HWNDPARENT" which actually means the window owner.
  HWND hwndParent = parent ? parent->GetAcceleratedWidget() : nullptr;
  if (hwndParent ==
      (HWND)::GetWindowLongPtr(GetAcceleratedWidget(), GWLP_HWNDPARENT))
    return;
  ::SetWindowLongPtr(GetAcceleratedWidget(), GWLP_HWNDPARENT,
                     (LONG_PTR)hwndParent);
  // Ensures the visibility
  if (IsVisible()) {
    WINDOWPLACEMENT wp;
    wp.length = sizeof(WINDOWPLACEMENT);
    ::GetWindowPlacement(GetAcceleratedWidget(), &wp);
    ::ShowWindow(GetAcceleratedWidget(), SW_HIDE);
    ::ShowWindow(GetAcceleratedWidget(), wp.showCmd);
    ::BringWindowToTop(GetAcceleratedWidget());
  }
#endif
}

gfx::NativeView NativeWindowViews::GetNativeView() const {
  return widget()->GetNativeView();
}

gfx::NativeWindow NativeWindowViews::GetNativeWindow() const {
  return widget()->GetNativeWindow();
}

void NativeWindowViews::SetProgressBar(double progress,
                                       NativeWindow::ProgressState state) {
#if BUILDFLAG(IS_WIN)
  taskbar_host_.SetProgressBar(GetAcceleratedWidget(), progress, state);
#elif BUILDFLAG(IS_LINUX)
  if (unity::IsRunning()) {
    unity::SetProgressFraction(progress);
  }
#endif
}

void NativeWindowViews::SetOverlayIcon(const gfx::Image& overlay,
                                       const std::string& description) {
#if BUILDFLAG(IS_WIN)
  SkBitmap overlay_bitmap = overlay.AsBitmap();
  taskbar_host_.SetOverlayIcon(GetAcceleratedWidget(), overlay_bitmap,
                               description);
#endif
}

void NativeWindowViews::SetAutoHideMenuBar(bool auto_hide) {
  root_view_.SetAutoHideMenuBar(auto_hide);
}

bool NativeWindowViews::IsMenuBarAutoHide() const {
  return root_view_.is_menu_bar_auto_hide();
}

void NativeWindowViews::SetMenuBarVisibility(bool visible) {
  root_view_.SetMenuBarVisibility(visible);
}

bool NativeWindowViews::IsMenuBarVisible() const {
  return root_view_.is_menu_bar_visible();
}

void NativeWindowViews::SetBackgroundMaterial(const std::string& material) {
  NativeWindow::SetBackgroundMaterial(material);

#if BUILDFLAG(IS_WIN)
  // DWMWA_USE_HOSTBACKDROPBRUSH is only supported on Windows 11 22H2 and up.
  if (base::win::GetVersion() < base::win::Version::WIN11_22H2)
    return;

  DWM_SYSTEMBACKDROP_TYPE backdrop_type = GetBackdropFromString(material);
  HRESULT result =
      DwmSetWindowAttribute(GetAcceleratedWidget(), DWMWA_SYSTEMBACKDROP_TYPE,
                            &backdrop_type, sizeof(backdrop_type));
  if (FAILED(result))
    LOG(WARNING) << "Failed to set background material to " << material;

  // For frameless windows with a background material set, we also need to
  // remove the caption color so it doesn't render a caption bar (since the
  // window is frameless)
  COLORREF caption_color = DWMWA_COLOR_DEFAULT;
  if (backdrop_type != DWMSBT_NONE && backdrop_type != DWMSBT_AUTO &&
      !has_frame()) {
    caption_color = DWMWA_COLOR_NONE;
  }
  result = DwmSetWindowAttribute(GetAcceleratedWidget(), DWMWA_CAPTION_COLOR,
                                 &caption_color, sizeof(caption_color));

  if (FAILED(result))
    LOG(WARNING) << "Failed to set caption color to transparent";
#endif
}

void NativeWindowViews::SetVisibleOnAllWorkspaces(
    bool visible,
    bool visibleOnFullScreen,
    bool skipTransformProcessType) {
  widget()->SetVisibleOnAllWorkspaces(visible);
}

bool NativeWindowViews::IsVisibleOnAllWorkspaces() const {
#if BUILDFLAG(IS_LINUX)
  if (IsX11()) {
    // Use the presence/absence of _NET_WM_STATE_STICKY in _NET_WM_STATE to
    // determine whether the current window is visible on all workspaces.
    x11::Atom sticky_atom = x11::GetAtom("_NET_WM_STATE_STICKY");
    std::vector<x11::Atom> wm_states;
    auto* connection = x11::Connection::Get();
    connection->GetArrayProperty(
        static_cast<x11::Window>(GetAcceleratedWidget()),
        x11::GetAtom("_NET_WM_STATE"), &wm_states);
    return base::Contains(wm_states, sticky_atom);
  }
#endif
  return false;
}

content::DesktopMediaID NativeWindowViews::GetDesktopMediaID() const {
  const gfx::AcceleratedWidget accelerated_widget = GetAcceleratedWidget();
  content::DesktopMediaID::Id window_handle = content::DesktopMediaID::kNullId;
  content::DesktopMediaID::Id aura_id = content::DesktopMediaID::kNullId;
#if BUILDFLAG(IS_WIN)
  window_handle =
      reinterpret_cast<content::DesktopMediaID::Id>(accelerated_widget);
#elif BUILDFLAG(IS_LINUX)
  window_handle = static_cast<uint32_t>(accelerated_widget);
#endif
  aura::WindowTreeHost* const host =
      aura::WindowTreeHost::GetForAcceleratedWidget(accelerated_widget);
  aura::Window* const aura_window = host ? host->window() : nullptr;
  if (aura_window) {
    aura_id = content::DesktopMediaID::RegisterNativeWindow(
                  content::DesktopMediaID::TYPE_WINDOW, aura_window)
                  .window_id;
  }

  // No constructor to pass the aura_id. Make sure to not use the other
  // constructor that has a third parameter, it is for yet another purpose.
  content::DesktopMediaID result = content::DesktopMediaID(
      content::DesktopMediaID::TYPE_WINDOW, window_handle);

  // Confusing but this is how content::DesktopMediaID is designed. The id
  // property is the window handle whereas the window_id property is an id
  // given by a map containing all aura instances.
  result.window_id = aura_id;
  return result;
}

gfx::AcceleratedWidget NativeWindowViews::GetAcceleratedWidget() const {
  if (GetNativeWindow() && GetNativeWindow()->GetHost())
    return GetNativeWindow()->GetHost()->GetAcceleratedWidget();
  else
    return gfx::kNullAcceleratedWidget;
}

NativeWindowHandle NativeWindowViews::GetNativeWindowHandle() const {
  return GetAcceleratedWidget();
}

gfx::Rect NativeWindowViews::ContentBoundsToWindowBounds(
    const gfx::Rect& bounds) const {
  if (!has_frame())
    return bounds;

  gfx::Rect window_bounds(bounds);
#if BUILDFLAG(IS_WIN)
  if (widget()->non_client_view()) {
    HWND hwnd = GetAcceleratedWidget();
    gfx::Rect dpi_bounds = DIPToScreenRect(hwnd, bounds);
    window_bounds = ScreenToDIPRect(
        hwnd, widget()->non_client_view()->GetWindowBoundsForClientBounds(
                  dpi_bounds));
  }
#endif

  if (root_view_.HasMenu() && root_view_.is_menu_bar_visible()) {
    int menu_bar_height = root_view_.GetMenuBarHeight();
    window_bounds.set_y(window_bounds.y() - menu_bar_height);
    window_bounds.set_height(window_bounds.height() + menu_bar_height);
  }
  return window_bounds;
}

gfx::Rect NativeWindowViews::WindowBoundsToContentBounds(
    const gfx::Rect& bounds) const {
  if (!has_frame())
    return bounds;

  gfx::Rect content_bounds(bounds);
#if BUILDFLAG(IS_WIN)
  HWND hwnd = GetAcceleratedWidget();
  content_bounds.set_size(DIPToScreenRect(hwnd, content_bounds).size());
  RECT rect;
  SetRectEmpty(&rect);
  DWORD style = ::GetWindowLong(hwnd, GWL_STYLE);
  DWORD ex_style = ::GetWindowLong(hwnd, GWL_EXSTYLE);
  AdjustWindowRectEx(&rect, style, FALSE, ex_style);
  content_bounds.set_width(content_bounds.width() - (rect.right - rect.left));
  content_bounds.set_height(content_bounds.height() - (rect.bottom - rect.top));
  content_bounds.set_size(ScreenToDIPRect(hwnd, content_bounds).size());
#endif

  if (root_view_.HasMenu() && root_view_.is_menu_bar_visible()) {
    int menu_bar_height = root_view_.GetMenuBarHeight();
    content_bounds.set_y(content_bounds.y() + menu_bar_height);
    content_bounds.set_height(content_bounds.height() - menu_bar_height);
  }
  return content_bounds;
}

#if BUILDFLAG(IS_WIN)
void NativeWindowViews::SetIcon(HICON window_icon, HICON app_icon) {
  // We are responsible for storing the images.
  window_icon_ = base::win::ScopedHICON(CopyIcon(window_icon));
  app_icon_ = base::win::ScopedHICON(CopyIcon(app_icon));

  HWND hwnd = GetAcceleratedWidget();
  SendMessage(hwnd, WM_SETICON, ICON_SMALL,
              reinterpret_cast<LPARAM>(window_icon_.get()));
  SendMessage(hwnd, WM_SETICON, ICON_BIG,
              reinterpret_cast<LPARAM>(app_icon_.get()));
}
#elif BUILDFLAG(IS_LINUX)
void NativeWindowViews::SetIcon(const gfx::ImageSkia& icon) {
  auto* tree_host = views::DesktopWindowTreeHostLinux::GetHostForWidget(
      GetAcceleratedWidget());
  tree_host->SetWindowIcons(icon, {});
}
#endif

#if BUILDFLAG(IS_WIN)
void NativeWindowViews::UpdateThickFrame() {
  if (!thick_frame_)
    return;

  if (IsMaximized() && !transparent()) {
    // For maximized window add thick frame always, otherwise it will be removed
    // in HWNDMessageHandler::SizeConstraintsChanged() which will result in
    // maximized window bounds change.
    FlipWindowStyle(GetAcceleratedWidget(), true, WS_THICKFRAME);
  } else if (has_frame()) {
    FlipWindowStyle(GetAcceleratedWidget(), resizable_, WS_THICKFRAME);
  }
}
#endif

void NativeWindowViews::OnWidgetActivationChanged(views::Widget* changed_widget,
                                                  bool active) {
  if (changed_widget != widget())
    return;

  if (active) {
    MoveBehindTaskBarIfNeeded();
    NativeWindow::NotifyWindowFocus();
  } else {
    NativeWindow::NotifyWindowBlur();
  }

  // Hide menu bar when window is blurred.
  if (!active && IsMenuBarAutoHide() && IsMenuBarVisible())
    SetMenuBarVisibility(false);

  root_view_.ResetAltState();
}

void NativeWindowViews::OnWidgetBoundsChanged(views::Widget* changed_widget,
                                              const gfx::Rect& bounds) {
  if (changed_widget != widget())
    return;

  // Note: We intentionally use `GetBounds()` instead of `bounds` to properly
  // handle minimized windows on Windows.
  const auto new_bounds = GetBounds();
  if (widget_size_ != new_bounds.size()) {
    NotifyWindowResize();
    widget_size_ = new_bounds.size();
  }
}

void NativeWindowViews::OnWidgetDestroying(views::Widget* widget) {
  aura::Window* window = GetNativeWindow();
  if (window)
    window->RemovePreTargetHandler(this);
}

void NativeWindowViews::OnWidgetDestroyed(views::Widget* changed_widget) {
  widget_destroyed_ = true;
}

views::View* NativeWindowViews::GetInitiallyFocusedView() {
  return focused_view_;
}

bool NativeWindowViews::CanMaximize() const {
  return resizable_ && maximizable_;
}

bool NativeWindowViews::CanMinimize() const {
#if BUILDFLAG(IS_WIN)
  return minimizable_;
#elif BUILDFLAG(IS_LINUX)
  return true;
#endif
}

std::u16string NativeWindowViews::GetWindowTitle() const {
  return base::UTF8ToUTF16(title_);
}

views::View* NativeWindowViews::GetContentsView() {
  return root_view_.GetMainView();
}

bool NativeWindowViews::ShouldDescendIntoChildForEventHandling(
    gfx::NativeView child,
    const gfx::Point& location) {
  return NonClientHitTest(location) == HTNOWHERE;
}

views::ClientView* NativeWindowViews::CreateClientView(views::Widget* widget) {
  return new NativeWindowClientView{widget, &root_view_, this};
}

std::unique_ptr<views::NonClientFrameView>
NativeWindowViews::CreateNonClientFrameView(views::Widget* widget) {
#if BUILDFLAG(IS_WIN)
  auto frame_view = std::make_unique<WinFrameView>();
  frame_view->Init(this, widget);
  return frame_view;
#else
  if (has_frame() && !has_client_frame()) {
    return std::make_unique<NativeFrameView>(this, widget);
  } else {
    if (has_frame() && has_client_frame()) {
      auto frame_view = std::make_unique<ClientFrameViewLinux>();
      frame_view->Init(this, widget);
      return frame_view;
    } else {
      auto frame_view = std::make_unique<OpaqueFrameView>();
      frame_view->Init(this, widget);
      return frame_view;
    }
  }
#endif
}

void NativeWindowViews::OnWidgetMove() {
  NotifyWindowMove();
}

void NativeWindowViews::HandleKeyboardEvent(
    content::WebContents*,
    const input::NativeWebKeyboardEvent& event) {
  if (widget_destroyed_)
    return;

#if BUILDFLAG(IS_LINUX)
  if (event.windows_key_code == ui::VKEY_BROWSER_BACK)
    NotifyWindowExecuteAppCommand(kBrowserBackward);
  else if (event.windows_key_code == ui::VKEY_BROWSER_FORWARD)
    NotifyWindowExecuteAppCommand(kBrowserForward);
#endif

  keyboard_event_handler_.HandleKeyboardEvent(event,
                                              root_view_.GetFocusManager());
  root_view_.HandleKeyEvent(event);
}

void NativeWindowViews::OnMouseEvent(ui::MouseEvent* event) {
  if (event->type() != ui::ET_MOUSE_PRESSED)
    return;

  // Alt+Click should not toggle menu bar.
  root_view_.ResetAltState();

#if BUILDFLAG(IS_LINUX)
  if (event->changed_button_flags() == ui::EF_BACK_MOUSE_BUTTON)
    NotifyWindowExecuteAppCommand(kBrowserBackward);
  else if (event->changed_button_flags() == ui::EF_FORWARD_MOUSE_BUTTON)
    NotifyWindowExecuteAppCommand(kBrowserForward);
#endif
}

ui::WindowShowState NativeWindowViews::GetRestoredState() {
  if (IsMaximized()) {
#if BUILDFLAG(IS_WIN)
    // Only restore Maximized state when window is NOT transparent style
    if (!transparent()) {
      return ui::SHOW_STATE_MAXIMIZED;
    }
#else
    return ui::SHOW_STATE_MAXIMIZED;
#endif
  }

  if (IsFullscreen())
    return ui::SHOW_STATE_FULLSCREEN;

  return ui::SHOW_STATE_NORMAL;
}

void NativeWindowViews::MoveBehindTaskBarIfNeeded() {
#if BUILDFLAG(IS_WIN)
  if (behind_task_bar_) {
    const HWND task_bar_hwnd = ::FindWindow(kUniqueTaskBarClassName, nullptr);
    ::SetWindowPos(GetAcceleratedWidget(), task_bar_hwnd, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
#endif
  // TODO(julien.isorce): Implement X11 case.
}

// static
NativeWindow* NativeWindow::Create(const gin_helper::Dictionary& options,
                                   NativeWindow* parent) {
  return new NativeWindowViews(options, parent);
}

}  // namespace electron
