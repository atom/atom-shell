// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_NATIVE_WINDOW_MAC_H_
#define ELECTRON_SHELL_BROWSER_NATIVE_WINDOW_MAC_H_

#import <Cocoa/Cocoa.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "electron/shell/common/api/api.mojom.h"
#include "shell/browser/native_window.h"
#include "ui/display/display_observer.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/controls/native/native_view_host.h"

@class ElectronNSWindow;
@class ElectronNSWindowDelegate;
@class ElectronPreviewItem;
@class ElectronTouchBar;
@class WindowButtonsProxy;

namespace electron {

class RootViewMac;

class NativeWindowMac : public NativeWindow,
                        public ui::NativeThemeObserver,
                        public display::DisplayObserver {
 public:
  NativeWindowMac(const gin_helper::Dictionary& options, NativeWindow* parent);
  ~NativeWindowMac() override;

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
  void SetBounds(const gfx::Rect& bounds, bool animate = false) override;
  gfx::Rect GetBounds() const override;
  bool IsNormal() const override;
  gfx::Rect GetNormalBounds() const override;
  void SetSizeConstraints(
      const extensions::SizeConstraints& window_constraints) override;
  void SetContentSizeConstraints(
      const extensions::SizeConstraints& size_constraints) override;
  void SetResizable(bool resizable) override;
  bool MoveAbove(const std::string& sourceId) override;
  void MoveTop() override;
  bool IsResizable() const override;
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
                      int relative_level) override;
  std::string GetAlwaysOnTopLevel() const override;
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
  void SetBackgroundColor(SkColor color) override;
  SkColor GetBackgroundColor() const override;
  void InvalidateShadow() override;
  void SetHasShadow(bool has_shadow) override;
  bool HasShadow() const override;
  void SetOpacity(const double opacity) override;
  double GetOpacity() const override;
  void SetRepresentedFilename(const std::string& filename) override;
  std::string GetRepresentedFilename() const override;
  void SetDocumentEdited(bool edited) override;
  bool IsDocumentEdited() const override;
  void SetIgnoreMouseEvents(bool ignore, bool forward) override;
  bool IsHiddenInMissionControl() const override;
  void SetHiddenInMissionControl(bool hidden) override;
  void SetContentProtection(bool enable) override;
  void SetFocusable(bool focusable) override;
  bool IsFocusable() const override;
  void SetParentWindow(NativeWindow* parent) override;
  content::DesktopMediaID GetDesktopMediaID() const override;
  gfx::NativeView GetNativeView() const override;
  gfx::NativeWindow GetNativeWindow() const override;
  gfx::AcceleratedWidget GetAcceleratedWidget() const override;
  NativeWindowHandle GetNativeWindowHandle() const override;
  void SetProgressBar(double progress, const ProgressState state) override;
  void SetOverlayIcon(const gfx::Image& overlay,
                      const std::string& description) override;
  void SetVisibleOnAllWorkspaces(bool visible,
                                 bool visibleOnFullScreen,
                                 bool skipTransformProcessType) override;
  bool IsVisibleOnAllWorkspaces() const override;
  void SetAutoHideCursor(bool auto_hide) override;
  void SetVibrancy(const std::string& type) override;
  void SetWindowButtonVisibility(bool visible) override;
  bool GetWindowButtonVisibility() const override;
  void SetWindowButtonPosition(std::optional<gfx::Point> position) override;
  std::optional<gfx::Point> GetWindowButtonPosition() const override;
  void RedrawTrafficLights() override;
  void UpdateFrame() override;
  void SetTouchBar(
      std::vector<gin_helper::PersistentDictionary> items) override;
  void RefreshTouchBarItem(const std::string& item_id) override;
  void SetEscapeTouchBarItem(gin_helper::PersistentDictionary item) override;
  void SelectPreviousTab() override;
  void SelectNextTab() override;
  void ShowAllTabs() override;
  void MergeAllWindows() override;
  void MoveTabToNewWindow() override;
  void ToggleTabBar() override;
  bool AddTabbedWindow(NativeWindow* window) override;
  std::optional<std::string> GetTabbingIdentifier() const override;
  void SetAspectRatio(double aspect_ratio,
                      const gfx::Size& extra_size) override;
  void PreviewFile(const std::string& path,
                   const std::string& display_name) override;
  void CloseFilePreview() override;
  gfx::Rect ContentBoundsToWindowBounds(const gfx::Rect& bounds) const override;
  gfx::Rect WindowBoundsToContentBounds(const gfx::Rect& bounds) const override;
  std::optional<gfx::Rect> GetWindowControlsOverlayRect() override;
  void NotifyWindowEnterFullScreen() override;
  void NotifyWindowLeaveFullScreen() override;
  void SetActive(bool is_key) override;
  bool IsActive() const override;
  // Remove the specified child window without closing it.
  void RemoveChildWindow(NativeWindow* child) override;
  void RemoveChildFromParentWindow() override;
  // Attach child windows, if the window is visible.
  void AttachChildren() override;
  // Detach window from parent without destroying it.
  void DetachChildren() override;

  void NotifyWindowWillEnterFullScreen();
  void NotifyWindowWillLeaveFullScreen();

  // Cleanup observers when window is getting closed. Note that the destructor
  // can be called much later after window gets closed, so we should not do
  // cleanup in destructor.
  void Cleanup();

  void UpdateVibrancyRadii(bool fullscreen);

  void UpdateWindowOriginalFrame();

  bool IsPanel();

  // Set the attribute of NSWindow while work around a bug of zoom button.
  bool HasStyleMask(NSUInteger flag) const;
  void SetStyleMask(bool on, NSUInteger flag);
  void SetCollectionBehavior(bool on, NSUInteger flag);
  void SetWindowLevel(int level);

  bool HandleDeferredClose();
  void SetHasDeferredWindowClose(bool defer_close) {
    has_deferred_window_close_ = defer_close;
  }

  void set_wants_to_be_visible(bool visible) { wants_to_be_visible_ = visible; }
  bool wants_to_be_visible() const { return wants_to_be_visible_; }

  enum class VisualEffectState {
    kFollowWindow,
    kActive,
    kInactive,
  };

  ElectronPreviewItem* preview_item() const { return preview_item_; }
  ElectronTouchBar* touch_bar() const { return touch_bar_; }
  bool zoom_to_page_width() const { return zoom_to_page_width_; }
  bool always_simple_fullscreen() const { return always_simple_fullscreen_; }

  // We need to save the result of windowWillUseStandardFrame:defaultFrame
  // because macOS calls it with what it refers to as the "best fit" frame for a
  // zoom. This means that even if an aspect ratio is set, macOS might adjust it
  // to better fit the screen.
  //
  // Thus, we can't just calculate the maximized aspect ratio'd sizing from
  // the current visible screen and compare that to the current window's frame
  // to determine whether a window is maximized.
  NSRect default_frame_for_zoom() const { return default_frame_for_zoom_; }
  void set_default_frame_for_zoom(NSRect frame) {
    default_frame_for_zoom_ = frame;
  }

 protected:
  // views::WidgetDelegate:
  views::View* GetContentsView() override;
  bool CanMaximize() const override;
  std::unique_ptr<views::NonClientFrameView> CreateNonClientFrameView(
      views::Widget* widget) override;

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

  // display::DisplayObserver:
  void OnDisplayMetricsChanged(const display::Display& display,
                               uint32_t changed_metrics) override;

 private:
  // Add custom layers to the content view.
  void AddContentViewLayers();

  void InternalSetWindowButtonVisibility(bool visible);
  void InternalSetParentWindow(NativeWindow* parent, bool attach);
  void SetForwardMouseMessages(bool forward);

  void UpdateZoomButton();

  ElectronNSWindow* window_;  // Weak ref, managed by widget_.

  ElectronNSWindowDelegate* __strong window_delegate_;
  ElectronPreviewItem* __strong preview_item_;
  ElectronTouchBar* __strong touch_bar_;

  // The views::View that fills the client area.
  std::unique_ptr<RootViewMac> root_view_;

  bool fullscreen_before_kiosk_ = false;
  bool is_kiosk_ = false;
  bool zoom_to_page_width_ = false;
  std::optional<gfx::Point> traffic_light_position_;

  // Trying to close an NSWindow during a fullscreen transition will cause the
  // window to lock up. Use this to track if CloseWindow was called during a
  // fullscreen transition, to defer the -[NSWindow close] call until the
  // transition is complete.
  bool has_deferred_window_close_ = false;

  // If true, the window is either visible, or wants to be visible but is
  // currently hidden due to having a hidden parent.
  bool wants_to_be_visible_ = false;

  NSInteger attention_request_id_ = 0;  // identifier from requestUserAttention

  // The presentation options before entering kiosk mode.
  NSApplicationPresentationOptions kiosk_options_;

  // The "visualEffectState" option.
  VisualEffectState visual_effect_state_ = VisualEffectState::kFollowWindow;

  // The visibility mode of window button controls when explicitly set through
  // setWindowButtonVisibility().
  std::optional<bool> window_button_visibility_;

  // Controls the position and visibility of window buttons.
  WindowButtonsProxy* __strong buttons_proxy_;

  std::unique_ptr<SkRegion> draggable_region_;

  // Maximizable window state; necessary for persistence through redraws.
  bool maximizable_ = true;

  bool user_set_bounds_maximized_ = false;

  // Simple (pre-Lion) Fullscreen Settings
  bool always_simple_fullscreen_ = false;
  bool is_simple_fullscreen_ = false;
  bool was_maximizable_ = false;
  bool was_movable_ = false;
  bool is_active_ = false;
  NSRect original_frame_;
  NSInteger original_level_;
  NSUInteger simple_fullscreen_mask_;
  NSRect default_frame_for_zoom_;

  std::string vibrancy_type_;

  // The presentation options before entering simple fullscreen mode.
  NSApplicationPresentationOptions simple_fullscreen_options_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_NATIVE_WINDOW_MAC_H_
