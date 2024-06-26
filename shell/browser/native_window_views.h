// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_NATIVE_WINDOW_VIEWS_H_
#define ELECTRON_SHELL_BROWSER_NATIVE_WINDOW_VIEWS_H_

#include "shell/browser/native_window.h"

#include <memory>
#include <optional>
#include <set>
#include <string>

#include "base/memory/raw_ptr.h"
#include "shell/browser/ui/views/root_view.h"
#include "ui/base/ozone_buildflags.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"
#include "ui/views/widget/widget_observer.h"

#if BUILDFLAG(IS_WIN)
#include "base/win/scoped_gdi_object.h"
#include "shell/browser/ui/win/taskbar_host.h"
#endif

namespace electron {

#if BUILDFLAG(IS_LINUX)
class GlobalMenuBarX11;
#endif

#if BUILDFLAG(IS_OZONE_X11)
class EventDisabler;
#endif

#if BUILDFLAG(IS_WIN)
gfx::Rect ScreenToDIPRect(HWND hwnd, const gfx::Rect& pixel_bounds);
#endif

class NativeWindowViews : public NativeWindow,
                          private views::WidgetObserver,
                          private ui::EventHandler {
 public:
  NativeWindowViews(const gin_helper::Dictionary& options,
                    NativeWindow* parent);
  ~NativeWindowViews() override;

  // NativeWindow:
  void SetContentView(views::View* view) override;
  void Close() override;
  void CloseImmediately() override;
  void Focus(bool focus) override;
  bool IsFocused() const override;
  void Show() override;
  void ShowInactive() override;
  void Hide() override;
  bool IsVisible() const override;
  bool IsEnabled() const override;
  void SetEnabled(bool enable) override;
  void Maximize() override;
  void Unmaximize() override;
  bool IsMaximized() const override;
  void Minimize() override;
  void Restore() override;
  bool IsMinimized() const override;
  void SetFullScreen(bool fullscreen) override;
  bool IsFullscreen() const override;
  void SetBounds(const gfx::Rect& bounds, bool animate) override;
  gfx::Rect GetBounds() const override;
  gfx::Rect GetContentBounds() const override;
  gfx::Size GetContentSize() const override;
  gfx::Rect GetNormalBounds() const override;
  SkColor GetBackgroundColor() const override;
  void SetContentSizeConstraints(
      const extensions::SizeConstraints& size_constraints) override;
#if BUILDFLAG(IS_WIN)
  extensions::SizeConstraints GetContentSizeConstraints() const override;
#endif
  void SetResizable(bool resizable) override;
  bool MoveAbove(const std::string& sourceId) override;
  void MoveTop() override;
  bool IsResizable() const override;
  void SetAspectRatio(double aspect_ratio,
                      const gfx::Size& extra_size) override;
  void SetMovable(bool movable) override;
  bool IsMovable() const override;
  void SetMinimizable(bool minimizable) override;
  bool IsMinimizable() const override;
  void SetMaximizable(bool maximizable) override;
  bool IsMaximizable() const override;
  void SetFullScreenable(bool fullscreenable) override;
  bool IsFullScreenable() const override;
  void SetClosable(bool closable) override;
  bool IsClosable() const override;
  void SetAlwaysOnTop(ui::ZOrderLevel z_order,
                      const std::string& level,
                      int relativeLevel) override;
  ui::ZOrderLevel GetZOrderLevel() const override;
  void Center() override;
  void Invalidate() override;
  void SetTitle(const std::string& title) override;
  std::string GetTitle() const override;
  void FlashFrame(bool flash) override;
  void SetSkipTaskbar(bool skip) override;
  void SetExcludedFromShownWindowsMenu(bool excluded) override;
  bool IsExcludedFromShownWindowsMenu() const override;
  void SetSimpleFullScreen(bool simple_fullscreen) override;
  bool IsSimpleFullScreen() const override;
  void SetKiosk(bool kiosk) override;
  bool IsKiosk() const override;
  bool IsTabletMode() const override;
  void SetBackgroundColor(SkColor color) override;
  void SetHasShadow(bool has_shadow) override;
  bool HasShadow() const override;
  void SetOpacity(const double opacity) override;
  double GetOpacity() const override;
  void SetIgnoreMouseEvents(bool ignore, bool forward) override;
  void SetContentProtection(bool enable) override;
  void SetFocusable(bool focusable) override;
  bool IsFocusable() const override;
  void SetMenu(ElectronMenuModel* menu_model) override;
  void SetParentWindow(NativeWindow* parent) override;
  gfx::NativeView GetNativeView() const override;
  gfx::NativeWindow GetNativeWindow() const override;
  void SetOverlayIcon(const gfx::Image& overlay,
                      const std::string& description) override;
  void SetProgressBar(double progress, const ProgressState state) override;
  void SetAutoHideMenuBar(bool auto_hide) override;
  bool IsMenuBarAutoHide() const override;
  void SetMenuBarVisibility(bool visible) override;
  bool IsMenuBarVisible() const override;
  void SetBackgroundMaterial(const std::string& type) override;

  void SetVisibleOnAllWorkspaces(bool visible,
                                 bool visibleOnFullScreen,
                                 bool skipTransformProcessType) override;

  bool IsVisibleOnAllWorkspaces() const override;

  void SetGTKDarkThemeEnabled(bool use_dark_theme) override;

  content::DesktopMediaID GetDesktopMediaID() const override;
  gfx::AcceleratedWidget GetAcceleratedWidget() const override;
  NativeWindowHandle GetNativeWindowHandle() const override;

  gfx::Rect ContentBoundsToWindowBounds(const gfx::Rect& bounds) const override;
  gfx::Rect WindowBoundsToContentBounds(const gfx::Rect& bounds) const override;

  void IncrementChildModals();
  void DecrementChildModals();

#if BUILDFLAG(IS_WIN)
  // Catch-all message handling and filtering. Called before
  // HWNDMessageHandler's built-in handling, which may pre-empt some
  // expectations in Views/Aura if messages are consumed. Returns true if the
  // message was consumed by the delegate and should not be processed further
  // by the HWNDMessageHandler. In this case, |result| is returned. |result| is
  // not modified otherwise.
  bool PreHandleMSG(UINT message,
                    WPARAM w_param,
                    LPARAM l_param,
                    LRESULT* result);
  void SetIcon(HICON small_icon, HICON app_icon);
#elif BUILDFLAG(IS_LINUX)
  void SetIcon(const gfx::ImageSkia& icon);
#endif

#if BUILDFLAG(IS_WIN)
  TaskbarHost& taskbar_host() { return taskbar_host_; }
#endif

#if BUILDFLAG(IS_WIN)
  bool IsWindowControlsOverlayEnabled() const {
    return (title_bar_style_ == NativeWindowViews::TitleBarStyle::kHidden) &&
           titlebar_overlay_;
  }
  SkColor overlay_button_color() const { return overlay_button_color_; }
  void set_overlay_button_color(SkColor color) {
    overlay_button_color_ = color;
  }
  SkColor overlay_symbol_color() const { return overlay_symbol_color_; }
  void set_overlay_symbol_color(SkColor color) {
    overlay_symbol_color_ = color;
  }

  void UpdateThickFrame();
#endif

 private:
  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& bounds) override;
  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetDestroyed(views::Widget* widget) override;

  // views::WidgetDelegate:
  views::View* GetInitiallyFocusedView() override;
  bool CanMaximize() const override;
  bool CanMinimize() const override;
  std::u16string GetWindowTitle() const override;
  views::View* GetContentsView() override;
  bool ShouldDescendIntoChildForEventHandling(
      gfx::NativeView child,
      const gfx::Point& location) override;
  views::ClientView* CreateClientView(views::Widget* widget) override;
  std::unique_ptr<views::NonClientFrameView> CreateNonClientFrameView(
      views::Widget* widget) override;
  void OnWidgetMove() override;
#if BUILDFLAG(IS_WIN)
  bool ExecuteWindowsCommand(int command_id) override;
#endif

#if BUILDFLAG(IS_WIN)
  void HandleSizeEvent(WPARAM w_param, LPARAM l_param);
  void ResetWindowControls();
  void SetForwardMouseMessages(bool forward);
  static LRESULT CALLBACK SubclassProc(HWND hwnd,
                                       UINT msg,
                                       WPARAM w_param,
                                       LPARAM l_param,
                                       UINT_PTR subclass_id,
                                       DWORD_PTR ref_data);
  static LRESULT CALLBACK MouseHookProc(int n_code,
                                        WPARAM w_param,
                                        LPARAM l_param);
#endif

  // Enable/disable:
  bool ShouldBeEnabled() const;
  void SetEnabledInternal(bool enabled);

  // NativeWindow:
  void HandleKeyboardEvent(content::WebContents*,
                           const input::NativeWebKeyboardEvent& event) override;

  // ui::EventHandler:
  void OnMouseEvent(ui::MouseEvent* event) override;

  // Returns the restore state for the window.
  ui::WindowShowState GetRestoredState();

  // Maintain window placement.
  void MoveBehindTaskBarIfNeeded();

  RootView root_view_{this};

  // The view should be focused by default.
  raw_ptr<views::View> focused_view_ = nullptr;

  // The "resizable" flag on Linux is implemented by setting size constraints,
  // we need to make sure size constraints are restored when window becomes
  // resizable again. This is also used on Windows, to keep taskbar resize
  // events from resizing the window.
  extensions::SizeConstraints old_size_constraints_;

#if BUILDFLAG(IS_LINUX)
  std::unique_ptr<GlobalMenuBarX11> global_menu_bar_;
#endif

#if BUILDFLAG(IS_OZONE_X11)
  // To disable the mouse events.
  std::unique_ptr<EventDisabler> event_disabler_;
#endif

#if BUILDFLAG(IS_WIN)

  ui::WindowShowState last_window_state_;

  gfx::Rect last_normal_placement_bounds_;

  // In charge of running taskbar related APIs.
  TaskbarHost taskbar_host_;

  // Memoized version of a11y check
  bool checked_for_a11y_support_ = false;

  // Whether to show the WS_THICKFRAME style.
  bool thick_frame_ = true;

  // The bounds of window before maximize/fullscreen.
  gfx::Rect restore_bounds_;

  // The icons of window and taskbar.
  base::win::ScopedHICON window_icon_;
  base::win::ScopedHICON app_icon_;

  // The set of windows currently forwarding mouse messages.
  static std::set<NativeWindowViews*> forwarding_windows_;
  static HHOOK mouse_hook_;
  bool forwarding_mouse_messages_ = false;
  HWND legacy_window_ = nullptr;
  bool layered_ = false;

  // Set to true if the window is always on top and behind the task bar.
  bool behind_task_bar_ = false;

  // Whether we want to set window placement without side effect.
  bool is_setting_window_placement_ = false;

  // Whether the window is currently being resized.
  bool is_resizing_ = false;

  // Whether the window is currently being moved.
  bool is_moving_ = false;

  std::optional<gfx::Rect> pending_bounds_change_;

  // The color to use as the theme and symbol colors respectively for Window
  // Controls Overlay if enabled on Windows.
  SkColor overlay_button_color_;
  SkColor overlay_symbol_color_;

  // The message ID of the "TaskbarCreated" message, sent to us when we need to
  // reset our thumbar buttons.
  UINT taskbar_created_message_ = 0;
#endif

  // Handles unhandled keyboard messages coming back from the renderer process.
  views::UnhandledKeyboardEventHandler keyboard_event_handler_;

  // Whether the menubar is visible before the window enters fullscreen
  bool menu_bar_visible_before_fullscreen_ = false;

  // Whether the window should be enabled based on user calls to SetEnabled()
  bool is_enabled_ = true;
  // How many modal children this window has;
  // used to determine enabled state
  unsigned int num_modal_children_ = 0;

  bool use_content_size_ = false;
  bool movable_ = true;
  bool resizable_ = true;
  bool maximizable_ = true;
  bool minimizable_ = true;
  bool fullscreenable_ = true;
  std::string title_;
  gfx::Size widget_size_;
  double opacity_ = 1.0;
  bool widget_destroyed_ = false;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_NATIVE_WINDOW_VIEWS_H_
