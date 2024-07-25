// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/native_window_mac.h"

#include <AvailabilityMacros.h>
#include <objc/objc-runtime.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/apple/scoped_cftyperef.h"
#include "base/mac/mac_util.h"
#include "base/strings/sys_string_conversions.h"
#include "components/remote_cocoa/app_shim/native_widget_ns_window_bridge.h"
#include "components/remote_cocoa/browser/scoped_cg_window_id.h"
#include "content/public/browser/browser_accessibility_state.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/desktop_media_id.h"
#include "shell/browser/browser.h"
#include "shell/browser/javascript_environment.h"
#include "shell/browser/ui/cocoa/electron_native_widget_mac.h"
#include "shell/browser/ui/cocoa/electron_ns_panel.h"
#include "shell/browser/ui/cocoa/electron_ns_window.h"
#include "shell/browser/ui/cocoa/electron_ns_window_delegate.h"
#include "shell/browser/ui/cocoa/electron_preview_item.h"
#include "shell/browser/ui/cocoa/electron_touch_bar.h"
#include "shell/browser/ui/cocoa/root_view_mac.h"
#include "shell/browser/ui/cocoa/window_buttons_proxy.h"
#include "shell/browser/ui/drag_util.h"
#include "shell/browser/ui/inspectable_web_contents.h"
#include "shell/browser/window_list.h"
#include "shell/common/gin_converters/gfx_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/node_includes.h"
#include "shell/common/options_switches.h"
#include "skia/ext/skia_utils_mac.h"
#include "third_party/webrtc/modules/desktop_capture/mac/window_list_utils.h"
#include "ui/base/hit_test.h"
#include "ui/display/screen.h"
#include "ui/gl/gpu_switching_manager.h"
#include "ui/views/background.h"
#include "ui/views/cocoa/native_widget_mac_ns_window_host.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/native_frame_view_mac.h"

@interface ElectronProgressBar : NSProgressIndicator
@end

@implementation ElectronProgressBar

- (void)drawRect:(NSRect)dirtyRect {
  if (self.style != NSProgressIndicatorStyleBar)
    return;
  // Draw edges of rounded rect.
  NSRect rect = NSInsetRect([self bounds], 1.0, 1.0);
  CGFloat radius = rect.size.height / 2;
  NSBezierPath* bezier_path = [NSBezierPath bezierPathWithRoundedRect:rect
                                                              xRadius:radius
                                                              yRadius:radius];
  [bezier_path setLineWidth:2.0];
  [[NSColor grayColor] set];
  [bezier_path stroke];

  // Fill the rounded rect.
  rect = NSInsetRect(rect, 2.0, 2.0);
  radius = rect.size.height / 2;
  bezier_path = [NSBezierPath bezierPathWithRoundedRect:rect
                                                xRadius:radius
                                                yRadius:radius];
  [bezier_path setLineWidth:1.0];
  [bezier_path addClip];

  // Calculate the progress width.
  rect.size.width =
      floor(rect.size.width * ([self doubleValue] / [self maxValue]));

  // Fill the progress bar with color blue.
  [[NSColor colorWithSRGBRed:0.2 green:0.6 blue:1 alpha:1] set];
  NSRectFill(rect);
}

@end

namespace gin {

template <>
struct Converter<electron::NativeWindowMac::VisualEffectState> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     electron::NativeWindowMac::VisualEffectState* out) {
    using VisualEffectState = electron::NativeWindowMac::VisualEffectState;
    std::string visual_effect_state;
    if (!ConvertFromV8(isolate, val, &visual_effect_state))
      return false;
    if (visual_effect_state == "followWindow") {
      *out = VisualEffectState::kFollowWindow;
    } else if (visual_effect_state == "active") {
      *out = VisualEffectState::kActive;
    } else if (visual_effect_state == "inactive") {
      *out = VisualEffectState::kInactive;
    } else {
      return false;
    }
    return true;
  }
};

}  // namespace gin

namespace electron {

namespace {
// -[NSWindow orderWindow] does not handle reordering for children
// windows. Their order is fixed to the attachment order (the last attached
// window is on the top). Therefore, work around it by re-parenting in our
// desired order.
void ReorderChildWindowAbove(NSWindow* child_window, NSWindow* other_window) {
  NSWindow* parent = [child_window parentWindow];
  DCHECK(parent);

  // `ordered_children` sorts children windows back to front.
  NSArray<NSWindow*>* children = [[child_window parentWindow] childWindows];
  std::vector<std::pair<NSInteger, NSWindow*>> ordered_children;
  for (NSWindow* child in children)
    ordered_children.push_back({[child orderedIndex], child});
  std::sort(ordered_children.begin(), ordered_children.end(), std::greater<>());

  // If `other_window` is nullptr, place `child_window` in front of
  // all other children windows.
  if (other_window == nullptr)
    other_window = ordered_children.back().second;

  if (child_window == other_window)
    return;

  for (NSWindow* child in children)
    [parent removeChildWindow:child];

  const bool relative_to_parent = parent == other_window;
  if (relative_to_parent)
    [parent addChildWindow:child_window ordered:NSWindowAbove];

  // Re-parent children windows in the desired order.
  for (auto [ordered_index, child] : ordered_children) {
    if (child != child_window && child != other_window) {
      [parent addChildWindow:child ordered:NSWindowAbove];
    } else if (child == other_window && !relative_to_parent) {
      [parent addChildWindow:other_window ordered:NSWindowAbove];
      [parent addChildWindow:child_window ordered:NSWindowAbove];
    }
  }
}

}  // namespace

NativeWindowMac::NativeWindowMac(const gin_helper::Dictionary& options,
                                 NativeWindow* parent)
    : NativeWindow(options, parent), root_view_(new RootViewMac(this)) {
  ui::NativeTheme::GetInstanceForNativeUi()->AddObserver(this);
  display::Screen::GetScreen()->AddObserver(this);

  int width = 800, height = 600;
  options.Get(options::kWidth, &width);
  options.Get(options::kHeight, &height);

  NSRect main_screen_rect = [[[NSScreen screens] firstObject] frame];
  gfx::Rect bounds(round((NSWidth(main_screen_rect) - width) / 2),
                   round((NSHeight(main_screen_rect) - height) / 2), width,
                   height);

  bool resizable = true;
  options.Get(options::kResizable, &resizable);
  options.Get(options::kZoomToPageWidth, &zoom_to_page_width_);
  options.Get(options::kSimpleFullScreen, &always_simple_fullscreen_);
  options.GetOptional(options::kTrafficLightPosition, &traffic_light_position_);
  options.Get(options::kVisualEffectState, &visual_effect_state_);

  bool minimizable = true;
  options.Get(options::kMinimizable, &minimizable);

  bool maximizable = true;
  options.Get(options::kMaximizable, &maximizable);

  bool closable = true;
  options.Get(options::kClosable, &closable);

  std::string tabbingIdentifier;
  options.Get(options::kTabbingIdentifier, &tabbingIdentifier);

  std::string windowType;
  options.Get(options::kType, &windowType);

  bool hiddenInMissionControl = false;
  options.Get(options::kHiddenInMissionControl, &hiddenInMissionControl);

  bool useStandardWindow = true;
  // eventually deprecate separate "standardWindow" option in favor of
  // standard / textured window types
  options.Get(options::kStandardWindow, &useStandardWindow);
  if (windowType == "textured") {
    useStandardWindow = false;
  }

  // The window without titlebar is treated the same with frameless window.
  if (title_bar_style_ != TitleBarStyle::kNormal)
    set_has_frame(false);

  NSUInteger styleMask = NSWindowStyleMaskTitled;

  // The NSWindowStyleMaskFullSizeContentView style removes rounded corners
  // for frameless window.
  bool rounded_corner = true;
  options.Get(options::kRoundedCorners, &rounded_corner);
  if (!rounded_corner && !has_frame())
    styleMask = NSWindowStyleMaskBorderless;

  if (minimizable)
    styleMask |= NSWindowStyleMaskMiniaturizable;
  if (closable)
    styleMask |= NSWindowStyleMaskClosable;
  if (resizable)
    styleMask |= NSWindowStyleMaskResizable;
  if (!useStandardWindow || transparent() || !has_frame())
    styleMask |= NSWindowStyleMaskTexturedBackground;

  // Create views::Widget and assign window_ with it.
  // TODO(zcbenz): Get rid of the window_ in future.
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW);
  params.bounds = bounds;
  params.delegate = this;
  params.headless_mode = true;
  params.native_widget =
      new ElectronNativeWidgetMac(this, windowType, styleMask, widget());
  widget()->Init(std::move(params));
  widget()->SetNativeWindowProperty(kElectronNativeWindowKey, this);
  SetCanResize(resizable);
  window_ = static_cast<ElectronNSWindow*>(
      widget()->GetNativeWindow().GetNativeNSWindow());

  RegisterDeleteDelegateCallback(base::BindOnce(
      [](NativeWindowMac* window) {
        if (window->window_)
          window->window_ = nil;
        if (window->buttons_proxy_)
          window->buttons_proxy_ = nil;
      },
      this));

  [window_ setEnableLargerThanScreen:enable_larger_than_screen()];

  window_delegate_ = [[ElectronNSWindowDelegate alloc] initWithShell:this];
  [window_ setDelegate:window_delegate_];

  // Only use native parent window for non-modal windows.
  if (parent && !is_modal()) {
    SetParentWindow(parent);
  }

  if (transparent()) {
    // Setting the background color to clear will also hide the shadow.
    [window_ setBackgroundColor:[NSColor clearColor]];
  }

  if (windowType == "desktop") {
    [window_ setLevel:kCGDesktopWindowLevel - 1];
    [window_ setDisableKeyOrMainWindow:YES];
    [window_ setCollectionBehavior:(NSWindowCollectionBehaviorCanJoinAllSpaces |
                                    NSWindowCollectionBehaviorStationary |
                                    NSWindowCollectionBehaviorIgnoresCycle)];
  }

  if (windowType == "panel") {
    [window_ setLevel:NSFloatingWindowLevel];
  }

  bool focusable;
  if (options.Get(options::kFocusable, &focusable) && !focusable)
    [window_ setDisableKeyOrMainWindow:YES];

  if (transparent() || !has_frame()) {
    // Don't show title bar.
    [window_ setTitlebarAppearsTransparent:YES];
    [window_ setTitleVisibility:NSWindowTitleHidden];
    // Remove non-transparent corners, see
    // https://github.com/electron/electron/issues/517.
    [window_ setOpaque:NO];
    // Show window buttons if titleBarStyle is not "normal".
    if (title_bar_style_ == TitleBarStyle::kNormal) {
      InternalSetWindowButtonVisibility(false);
    } else {
      buttons_proxy_ = [[WindowButtonsProxy alloc] initWithWindow:window_];
      [buttons_proxy_ setHeight:titlebar_overlay_height()];
      if (traffic_light_position_) {
        [buttons_proxy_ setMargin:*traffic_light_position_];
      } else if (title_bar_style_ == TitleBarStyle::kHiddenInset) {
        // For macOS >= 11, while this value does not match official macOS apps
        // like Safari or Notes, it matches titleBarStyle's old implementation
        // before Electron <= 12.
        [buttons_proxy_ setMargin:gfx::Point(12, 11)];
      }
      if (title_bar_style_ == TitleBarStyle::kCustomButtonsOnHover) {
        [buttons_proxy_ setShowOnHover:YES];
      } else {
        // customButtonsOnHover does not show buttons initially.
        InternalSetWindowButtonVisibility(true);
      }
    }
  }

  // Create a tab only if tabbing identifier is specified and window has
  // a native title bar.
  if (tabbingIdentifier.empty() || transparent() || !has_frame()) {
    [window_ setTabbingMode:NSWindowTabbingModeDisallowed];
  } else {
    [window_ setTabbingIdentifier:base::SysUTF8ToNSString(tabbingIdentifier)];
  }

  // Resize to content bounds.
  bool use_content_size = false;
  options.Get(options::kUseContentSize, &use_content_size);
  if (!has_frame() || use_content_size)
    SetContentSize(gfx::Size(width, height));

  // Enable the NSView to accept first mouse event.
  bool acceptsFirstMouse = false;
  options.Get(options::kAcceptFirstMouse, &acceptsFirstMouse);
  [window_ setAcceptsFirstMouse:acceptsFirstMouse];

  // Disable auto-hiding cursor.
  bool disableAutoHideCursor = false;
  options.Get(options::kDisableAutoHideCursor, &disableAutoHideCursor);
  [window_ setDisableAutoHideCursor:disableAutoHideCursor];

  SetHiddenInMissionControl(hiddenInMissionControl);

  // Set maximizable state last to ensure zoom button does not get reset
  // by calls to other APIs.
  SetMaximizable(maximizable);

  // Default content view.
  SetContentView(new views::View());
  AddContentViewLayers();

  UpdateWindowOriginalFrame();
  original_level_ = [window_ level];
}

NativeWindowMac::~NativeWindowMac() = default;

void NativeWindowMac::SetContentView(views::View* view) {
  views::View* root_view = GetContentsView();
  if (content_view())
    root_view->RemoveChildView(content_view());

  set_content_view(view);
  root_view->AddChildView(content_view());

  root_view->DeprecatedLayoutImmediately();
}

void NativeWindowMac::Close() {
  if (!IsClosable()) {
    WindowList::WindowCloseCancelled(this);
    return;
  }

  if (fullscreen_transition_state() != FullScreenTransitionState::kNone) {
    SetHasDeferredWindowClose(true);
    return;
  }

  // Ensure we're detached from the parent window before closing.
  RemoveChildFromParentWindow();

  while (!child_windows_.empty()) {
    auto* child = child_windows_.back();
    child->RemoveChildFromParentWindow();
  }

  // If a sheet is attached to the window when we call
  // [window_ performClose:nil], the window won't close properly
  // even after the user has ended the sheet.
  // Ensure it's closed before calling [window_ performClose:nil].
  if ([window_ attachedSheet])
    [window_ endSheet:[window_ attachedSheet]];

  // window_ could be nil after performClose.
  bool should_notify = is_modal() && parent() && IsVisible();

  [window_ performClose:nil];

  // Closing a sheet doesn't trigger windowShouldClose,
  // so we need to manually call it ourselves here.
  if (should_notify) {
    NotifyWindowCloseButtonClicked();
  }
}

void NativeWindowMac::CloseImmediately() {
  // Ensure we're detached from the parent window before closing.
  RemoveChildFromParentWindow();

  while (!child_windows_.empty()) {
    auto* child = child_windows_.back();
    child->RemoveChildFromParentWindow();
  }

  [window_ close];
}

void NativeWindowMac::Focus(bool focus) {
  if (!IsVisible())
    return;

  if (focus) {
    // If we're a panel window, we do not want to activate the app,
    // which enables Electron-apps to build Spotlight-like experiences.
    if (!IsPanel()) {
      [[NSApplication sharedApplication] activateIgnoringOtherApps:NO];
    }
    [window_ makeKeyAndOrderFront:nil];
  } else {
    [window_ orderOut:nil];
    [window_ orderBack:nil];
  }
}

bool NativeWindowMac::IsFocused() const {
  return [window_ isKeyWindow];
}

void NativeWindowMac::Show() {
  if (is_modal() && parent()) {
    NSWindow* window = parent()->GetNativeWindow().GetNativeNSWindow();
    if ([window_ sheetParent] == nil)
      [window beginSheet:window_
          completionHandler:^(NSModalResponse){
          }];
    return;
  }

  set_wants_to_be_visible(true);

  // Reattach the window to the parent to actually show it.
  if (parent())
    InternalSetParentWindow(parent(), true);

  // Panels receive key focus when shown but should not activate the app.
  if (!IsPanel()) {
    [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
  }
  [window_ makeKeyAndOrderFront:nil];
}

void NativeWindowMac::ShowInactive() {
  set_wants_to_be_visible(true);

  // Reattach the window to the parent to actually show it.
  if (parent())
    InternalSetParentWindow(parent(), true);

  [window_ orderFrontKeepWindowKeyState];
}

void NativeWindowMac::Hide() {
  // If a sheet is attached to the window when we call [window_ orderOut:nil],
  // the sheet won't be able to show again on the same window.
  // Ensure it's closed before calling [window_ orderOut:nil].
  if ([window_ attachedSheet])
    [window_ endSheet:[window_ attachedSheet]];

  if (is_modal() && parent()) {
    [window_ orderOut:nil];
    [parent()->GetNativeWindow().GetNativeNSWindow() endSheet:window_];
    return;
  }

  // If the window wants to be visible and has a parent, then the parent may
  // order it back in (in the period between orderOut: and close).
  set_wants_to_be_visible(false);

  DetachChildren();

  // Detach the window from the parent before.
  if (parent())
    InternalSetParentWindow(parent(), false);

  [window_ orderOut:nil];
}

bool NativeWindowMac::IsVisible() const {
  bool occluded = [window_ occlusionState] == NSWindowOcclusionStateVisible;
  return [window_ isVisible] && !occluded && !IsMinimized();
}

bool NativeWindowMac::IsEnabled() const {
  return [window_ attachedSheet] == nil;
}

void NativeWindowMac::SetEnabled(bool enable) {
  if (!enable) {
    NSRect frame = [window_ frame];
    NSWindow* window =
        [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, frame.size.width,
                                                         frame.size.height)
                                    styleMask:NSWindowStyleMaskTitled
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    [window setAlphaValue:0.5];

    [window_ beginSheet:window
        completionHandler:^(NSModalResponse returnCode) {
          NSLog(@"main window disabled");
          return;
        }];
  } else if ([window_ attachedSheet]) {
    [window_ endSheet:[window_ attachedSheet]];
  }
}

void NativeWindowMac::Maximize() {
  const bool is_visible = [window_ isVisible];

  if (IsMaximized()) {
    if (!is_visible)
      ShowInactive();
    return;
  }

  // Take note of the current window size
  if (IsNormal())
    UpdateWindowOriginalFrame();
  [window_ zoom:nil];

  if (!is_visible) {
    ShowInactive();
    NotifyWindowMaximize();
  }
}

void NativeWindowMac::Unmaximize() {
  // Bail if the last user set bounds were the same size as the window
  // screen (e.g. the user set the window to maximized via setBounds)
  //
  // Per docs during zoom:
  // > If there’s no saved user state because there has been no previous
  // > zoom,the size and location of the window don’t change.
  //
  // However, in classic Apple fashion, this is not the case in practice,
  // and the frame inexplicably becomes very tiny. We should prevent
  // zoom from being called if the window is being unmaximized and its
  // unmaximized window bounds are themselves functionally maximized.
  if (!IsMaximized() || user_set_bounds_maximized_)
    return;

  [window_ zoom:nil];
}

bool NativeWindowMac::IsMaximized() const {
  // It's possible for [window_ isZoomed] to be true
  // when the window is minimized or fullscreened.
  if (IsMinimized() || IsFullscreen())
    return false;

  if (HasStyleMask(NSWindowStyleMaskResizable) != 0)
    return [window_ isZoomed];

  NSRect rectScreen = aspect_ratio() > 0.0
                          ? default_frame_for_zoom()
                          : [[NSScreen mainScreen] visibleFrame];

  return NSEqualRects([window_ frame], rectScreen);
}

void NativeWindowMac::Minimize() {
  if (IsMinimized())
    return;

  // Take note of the current window size
  if (IsNormal())
    UpdateWindowOriginalFrame();
  [window_ miniaturize:nil];
}

void NativeWindowMac::Restore() {
  [window_ deminiaturize:nil];
}

bool NativeWindowMac::IsMinimized() const {
  return [window_ isMiniaturized];
}

bool NativeWindowMac::IsPanel() {
  return [window_ isKindOfClass:[ElectronNSPanel class]];
}

bool NativeWindowMac::HandleDeferredClose() {
  if (has_deferred_window_close_) {
    SetHasDeferredWindowClose(false);
    Close();
    return true;
  }
  return false;
}

void NativeWindowMac::RemoveChildWindow(NativeWindow* child) {
  child_windows_.remove_if([&child](NativeWindow* w) { return (w == child); });

  [window_ removeChildWindow:child->GetNativeWindow().GetNativeNSWindow()];
}

void NativeWindowMac::RemoveChildFromParentWindow() {
  if (parent() && !is_modal()) {
    parent()->RemoveChildWindow(this);
    NativeWindow::SetParentWindow(nullptr);
  }
}

void NativeWindowMac::AttachChildren() {
  for (auto* child : child_windows_) {
    if (!static_cast<NativeWindowMac*>(child)->wants_to_be_visible())
      continue;

    auto* child_nswindow = child->GetNativeWindow().GetNativeNSWindow();
    if ([child_nswindow parentWindow] == window_)
      continue;

    // Attaching a window as a child window resets its window level, so
    // save and restore it afterwards.
    NSInteger level = window_.level;
    [window_ addChildWindow:child_nswindow ordered:NSWindowAbove];
    [window_ setLevel:level];
  }
}

void NativeWindowMac::DetachChildren() {
  // Hide all children before hiding/minimizing the window.
  // NativeWidgetNSWindowBridge::NotifyVisibilityChangeDown()
  // will DCHECK otherwise.
  for (auto* child : child_windows_) {
    [child->GetNativeWindow().GetNativeNSWindow() orderOut:nil];
  }
}

void NativeWindowMac::SetFullScreen(bool fullscreen) {
  // [NSWindow -toggleFullScreen] is an asynchronous operation, which means
  // that it's possible to call it while a fullscreen transition is currently
  // in process. This can create weird behavior (incl. phantom windows),
  // so we want to schedule a transition for when the current one has completed.
  if (fullscreen_transition_state() != FullScreenTransitionState::kNone) {
    if (!pending_transitions_.empty()) {
      bool last_pending = pending_transitions_.back();
      // Only push new transitions if they're different than the last transition
      // in the queue.
      if (last_pending != fullscreen)
        pending_transitions_.push(fullscreen);
    } else {
      pending_transitions_.push(fullscreen);
    }
    return;
  }

  if (fullscreen == IsFullscreen() || !IsFullScreenable())
    return;

  // Take note of the current window size
  if (IsNormal())
    UpdateWindowOriginalFrame();

  // This needs to be set here because it can be the case that
  // SetFullScreen is called by a user before windowWillEnterFullScreen
  // or windowWillExitFullScreen are invoked, and so a potential transition
  // could be dropped.
  fullscreen_transition_state_ = fullscreen
                                     ? FullScreenTransitionState::kEntering
                                     : FullScreenTransitionState::kExiting;

  if (![window_ toggleFullScreenMode:nil])
    fullscreen_transition_state_ = FullScreenTransitionState::kNone;
}

bool NativeWindowMac::IsFullscreen() const {
  return HasStyleMask(NSWindowStyleMaskFullScreen);
}

void NativeWindowMac::SetBounds(const gfx::Rect& bounds, bool animate) {
  // Do nothing if in fullscreen mode.
  if (IsFullscreen())
    return;

  // Check size constraints since setFrame does not check it.
  gfx::Size size = bounds.size();
  size.SetToMax(GetMinimumSize());
  gfx::Size max_size = GetMaximumSize();
  if (!max_size.IsEmpty())
    size.SetToMin(max_size);

  NSRect cocoa_bounds = NSMakeRect(bounds.x(), 0, size.width(), size.height());
  // Flip coordinates based on the primary screen.
  NSScreen* screen = [[NSScreen screens] firstObject];
  cocoa_bounds.origin.y = NSHeight([screen frame]) - size.height() - bounds.y();

  [window_ setFrame:cocoa_bounds display:YES animate:animate];
  user_set_bounds_maximized_ = IsMaximized() ? true : false;
  UpdateWindowOriginalFrame();
}

gfx::Rect NativeWindowMac::GetBounds() const {
  NSRect frame = [window_ frame];
  gfx::Rect bounds(frame.origin.x, 0, NSWidth(frame), NSHeight(frame));
  NSScreen* screen = [[NSScreen screens] firstObject];
  bounds.set_y(NSHeight([screen frame]) - NSMaxY(frame));
  return bounds;
}

bool NativeWindowMac::IsNormal() const {
  return NativeWindow::IsNormal() && !IsSimpleFullScreen();
}

gfx::Rect NativeWindowMac::GetNormalBounds() const {
  if (IsNormal()) {
    return GetBounds();
  }
  NSRect frame = original_frame_;
  gfx::Rect bounds(frame.origin.x, 0, NSWidth(frame), NSHeight(frame));
  NSScreen* screen = [[NSScreen screens] firstObject];
  bounds.set_y(NSHeight([screen frame]) - NSMaxY(frame));
  return bounds;
  // Works on OS_WIN !
  // return widget()->GetRestoredBounds();
}

void NativeWindowMac::SetSizeConstraints(
    const extensions::SizeConstraints& window_constraints) {
  // Apply the size constraints to NSWindow.
  if (window_constraints.HasMinimumSize())
    [window_ setMinSize:window_constraints.GetMinimumSize().ToCGSize()];
  if (window_constraints.HasMaximumSize())
    [window_ setMaxSize:window_constraints.GetMaximumSize().ToCGSize()];
  NativeWindow::SetSizeConstraints(window_constraints);
}

void NativeWindowMac::SetContentSizeConstraints(
    const extensions::SizeConstraints& size_constraints) {
  auto convertSize = [this](const gfx::Size& size) {
    // Our frameless window still has titlebar attached, so setting contentSize
    // will result in actual content size being larger.
    if (!has_frame()) {
      NSRect frame = NSMakeRect(0, 0, size.width(), size.height());
      NSRect content = [window_ originalContentRectForFrameRect:frame];
      return content.size;
    } else {
      return NSMakeSize(size.width(), size.height());
    }
  };

  // Apply the size constraints to NSWindow.
  NSView* content = [window_ contentView];
  if (size_constraints.HasMinimumSize()) {
    NSSize min_size = convertSize(size_constraints.GetMinimumSize());
    [window_ setContentMinSize:[content convertSize:min_size toView:nil]];
  }
  if (size_constraints.HasMaximumSize()) {
    NSSize max_size = convertSize(size_constraints.GetMaximumSize());
    [window_ setContentMaxSize:[content convertSize:max_size toView:nil]];
  }
  NativeWindow::SetContentSizeConstraints(size_constraints);
}

bool NativeWindowMac::MoveAbove(const std::string& sourceId) {
  const content::DesktopMediaID id = content::DesktopMediaID::Parse(sourceId);
  if (id.type != content::DesktopMediaID::TYPE_WINDOW)
    return false;

  // Check if the window source is valid.
  const CGWindowID window_id = id.id;
  if (!webrtc::GetWindowOwnerPid(window_id))
    return false;

  if (!parent() || is_modal()) {
    [window_ orderWindow:NSWindowAbove relativeTo:window_id];
  } else {
    NSWindow* other_window = [NSApp windowWithWindowNumber:window_id];
    ReorderChildWindowAbove(window_, other_window);
  }

  return true;
}

void NativeWindowMac::MoveTop() {
  if (!parent() || is_modal()) {
    [window_ orderWindow:NSWindowAbove relativeTo:0];
  } else {
    ReorderChildWindowAbove(window_, nullptr);
  }
}

void NativeWindowMac::SetResizable(bool resizable) {
  ScopedDisableResize disable_resize;
  SetStyleMask(resizable, NSWindowStyleMaskResizable);

  bool was_fullscreenable = IsFullScreenable();

  // Right now, resizable and fullscreenable are decoupled in
  // documentation and on Windows/Linux. Chromium disables
  // fullscreen collection behavior as well as the maximize traffic
  // light in SetCanResize if resizability is false on macOS unless
  // the window is both resizable and maximizable. We want consistent
  // cross-platform behavior, so if resizability is disabled we disable
  // the maximize button and ensure fullscreenability matches user setting.
  SetCanResize(resizable);
  SetFullScreenable(was_fullscreenable);
  UpdateZoomButton();
}

bool NativeWindowMac::IsResizable() const {
  bool in_fs_transition =
      fullscreen_transition_state() != FullScreenTransitionState::kNone;
  bool has_rs_mask = HasStyleMask(NSWindowStyleMaskResizable);
  return has_rs_mask && !IsFullscreen() && !in_fs_transition;
}

void NativeWindowMac::SetMovable(bool movable) {
  [window_ setMovable:movable];
}

bool NativeWindowMac::IsMovable() const {
  return [window_ isMovable];
}

void NativeWindowMac::SetMinimizable(bool minimizable) {
  SetStyleMask(minimizable, NSWindowStyleMaskMiniaturizable);
}

bool NativeWindowMac::IsMinimizable() const {
  return HasStyleMask(NSWindowStyleMaskMiniaturizable);
}

void NativeWindowMac::SetMaximizable(bool maximizable) {
  maximizable_ = maximizable;
  UpdateZoomButton();
}

bool NativeWindowMac::IsMaximizable() const {
  return [[window_ standardWindowButton:NSWindowZoomButton] isEnabled];
}

void NativeWindowMac::UpdateZoomButton() {
  [[window_ standardWindowButton:NSWindowZoomButton]
      setEnabled:HasStyleMask(NSWindowStyleMaskResizable) &&
                 (CanMaximize() || IsFullScreenable())];
}

void NativeWindowMac::SetFullScreenable(bool fullscreenable) {
  SetCollectionBehavior(fullscreenable,
                        NSWindowCollectionBehaviorFullScreenPrimary);
  // On EL Capitan this flag is required to hide fullscreen button.
  SetCollectionBehavior(!fullscreenable,
                        NSWindowCollectionBehaviorFullScreenAuxiliary);

  UpdateZoomButton();
}

bool NativeWindowMac::IsFullScreenable() const {
  NSUInteger collectionBehavior = [window_ collectionBehavior];
  return collectionBehavior & NSWindowCollectionBehaviorFullScreenPrimary;
}

void NativeWindowMac::SetClosable(bool closable) {
  SetStyleMask(closable, NSWindowStyleMaskClosable);
}

bool NativeWindowMac::IsClosable() const {
  return HasStyleMask(NSWindowStyleMaskClosable);
}

void NativeWindowMac::SetAlwaysOnTop(ui::ZOrderLevel z_order,
                                     const std::string& level_name,
                                     int relative_level) {
  if (z_order == ui::ZOrderLevel::kNormal) {
    SetWindowLevel(NSNormalWindowLevel);
    return;
  }

  int level = NSNormalWindowLevel;
  if (level_name == "floating") {
    level = NSFloatingWindowLevel;
  } else if (level_name == "torn-off-menu") {
    level = NSTornOffMenuWindowLevel;
  } else if (level_name == "modal-panel") {
    level = NSModalPanelWindowLevel;
  } else if (level_name == "main-menu") {
    level = NSMainMenuWindowLevel;
  } else if (level_name == "status") {
    level = NSStatusWindowLevel;
  } else if (level_name == "pop-up-menu") {
    level = NSPopUpMenuWindowLevel;
  } else if (level_name == "screen-saver") {
    level = NSScreenSaverWindowLevel;
  }

  SetWindowLevel(level + relative_level);
}

std::string NativeWindowMac::GetAlwaysOnTopLevel() const {
  std::string level_name = "normal";

  int level = [window_ level];
  if (level == NSFloatingWindowLevel) {
    level_name = "floating";
  } else if (level == NSTornOffMenuWindowLevel) {
    level_name = "torn-off-menu";
  } else if (level == NSModalPanelWindowLevel) {
    level_name = "modal-panel";
  } else if (level == NSMainMenuWindowLevel) {
    level_name = "main-menu";
  } else if (level == NSStatusWindowLevel) {
    level_name = "status";
  } else if (level == NSPopUpMenuWindowLevel) {
    level_name = "pop-up-menu";
  } else if (level == NSScreenSaverWindowLevel) {
    level_name = "screen-saver";
  }

  return level_name;
}

void NativeWindowMac::SetWindowLevel(int unbounded_level) {
  int level = std::min(
      std::max(unbounded_level, CGWindowLevelForKey(kCGMinimumWindowLevelKey)),
      CGWindowLevelForKey(kCGMaximumWindowLevelKey));
  ui::ZOrderLevel z_order_level = level == NSNormalWindowLevel
                                      ? ui::ZOrderLevel::kNormal
                                      : ui::ZOrderLevel::kFloatingWindow;
  bool did_z_order_level_change = z_order_level != GetZOrderLevel();

  was_maximizable_ = IsMaximizable();

  // We need to explicitly keep the NativeWidget up to date, since it stores the
  // window level in a local variable, rather than reading it from the NSWindow.
  // Unfortunately, it results in a second setLevel call. It's not ideal, but we
  // don't expect this to cause any user-visible jank.
  widget()->SetZOrderLevel(z_order_level);
  [window_ setLevel:level];

  // Set level will make the zoom button revert to default, probably
  // a bug of Cocoa or macOS.
  SetMaximizable(was_maximizable_);

  // This must be notified at the very end or IsAlwaysOnTop
  // will not yet have been updated to reflect the new status
  if (did_z_order_level_change)
    NativeWindow::NotifyWindowAlwaysOnTopChanged();
}

ui::ZOrderLevel NativeWindowMac::GetZOrderLevel() const {
  return widget()->GetZOrderLevel();
}

void NativeWindowMac::Center() {
  [window_ center];
}

void NativeWindowMac::Invalidate() {
  [[window_ contentView] setNeedsDisplay:YES];
}

void NativeWindowMac::SetTitle(const std::string& title) {
  [window_ setTitle:base::SysUTF8ToNSString(title)];
  if (buttons_proxy_)
    [buttons_proxy_ redraw];
}

std::string NativeWindowMac::GetTitle() const {
  return base::SysNSStringToUTF8([window_ title]);
}

void NativeWindowMac::FlashFrame(bool flash) {
  if (flash) {
    attention_request_id_ = [NSApp requestUserAttention:NSCriticalRequest];
  } else {
    [NSApp cancelUserAttentionRequest:attention_request_id_];
    attention_request_id_ = 0;
  }
}

void NativeWindowMac::SetSkipTaskbar(bool skip) {}

bool NativeWindowMac::IsExcludedFromShownWindowsMenu() const {
  NSWindow* window = GetNativeWindow().GetNativeNSWindow();
  return [window isExcludedFromWindowsMenu];
}

void NativeWindowMac::SetExcludedFromShownWindowsMenu(bool excluded) {
  NSWindow* window = GetNativeWindow().GetNativeNSWindow();
  [window setExcludedFromWindowsMenu:excluded];
}

void NativeWindowMac::OnDisplayMetricsChanged(const display::Display& display,
                                              uint32_t changed_metrics) {
  // We only want to force screen recalibration if we're in simpleFullscreen
  // mode.
  if (!is_simple_fullscreen_)
    return;

  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(&NativeWindow::UpdateFrame, GetWeakPtr()));
}

void NativeWindowMac::SetSimpleFullScreen(bool simple_fullscreen) {
  NSWindow* window = GetNativeWindow().GetNativeNSWindow();

  if (simple_fullscreen && !is_simple_fullscreen_) {
    is_simple_fullscreen_ = true;

    // Take note of the current window size and level
    if (IsNormal()) {
      UpdateWindowOriginalFrame();
      original_level_ = [window_ level];
    }

    simple_fullscreen_options_ = [NSApp currentSystemPresentationOptions];
    simple_fullscreen_mask_ = [window styleMask];

    // We can simulate the pre-Lion fullscreen by auto-hiding the dock and menu
    // bar
    NSApplicationPresentationOptions options =
        NSApplicationPresentationAutoHideDock |
        NSApplicationPresentationAutoHideMenuBar;
    [NSApp setPresentationOptions:options];

    was_maximizable_ = IsMaximizable();
    was_movable_ = IsMovable();

    NSRect fullscreenFrame = [window.screen frame];

    // If our app has dock hidden, set the window level higher so another app's
    // menu bar doesn't appear on top of our fullscreen app.
    if ([[NSRunningApplication currentApplication] activationPolicy] !=
        NSApplicationActivationPolicyRegular) {
      window.level = NSPopUpMenuWindowLevel;
    }

    // Always hide the titlebar in simple fullscreen mode.
    //
    // Note that we must remove the NSWindowStyleMaskTitled style instead of
    // using the [window_ setTitleVisibility:], as the latter would leave the
    // window with rounded corners.
    SetStyleMask(false, NSWindowStyleMaskTitled);

    if (!window_button_visibility_.has_value()) {
      // Lets keep previous behaviour - hide window controls in titled
      // fullscreen mode when not specified otherwise.
      InternalSetWindowButtonVisibility(false);
    }

    [window setFrame:fullscreenFrame display:YES animate:YES];

    // Fullscreen windows can't be resized, minimized, maximized, or moved
    SetMinimizable(false);
    SetResizable(false);
    SetMaximizable(false);
    SetMovable(false);
  } else if (!simple_fullscreen && is_simple_fullscreen_) {
    is_simple_fullscreen_ = false;

    [window setFrame:original_frame_ display:YES animate:YES];
    window.level = original_level_;

    [NSApp setPresentationOptions:simple_fullscreen_options_];

    // Restore original style mask
    ScopedDisableResize disable_resize;
    [window_ setStyleMask:simple_fullscreen_mask_];

    // Restore window manipulation abilities
    SetMaximizable(was_maximizable_);
    SetMovable(was_movable_);

    // Restore default window controls visibility state.
    if (!window_button_visibility_.has_value()) {
      bool visibility;
      if (has_frame())
        visibility = true;
      else
        visibility = title_bar_style_ != TitleBarStyle::kNormal;
      InternalSetWindowButtonVisibility(visibility);
    }

    if (buttons_proxy_)
      [buttons_proxy_ redraw];
  }
}

bool NativeWindowMac::IsSimpleFullScreen() const {
  return is_simple_fullscreen_;
}

void NativeWindowMac::SetKiosk(bool kiosk) {
  if (kiosk && !is_kiosk_) {
    kiosk_options_ = [NSApp currentSystemPresentationOptions];
    NSApplicationPresentationOptions options =
        NSApplicationPresentationHideDock |
        NSApplicationPresentationHideMenuBar |
        NSApplicationPresentationDisableAppleMenu |
        NSApplicationPresentationDisableProcessSwitching |
        NSApplicationPresentationDisableForceQuit |
        NSApplicationPresentationDisableSessionTermination |
        NSApplicationPresentationDisableHideApplication;
    [NSApp setPresentationOptions:options];
    is_kiosk_ = true;
    fullscreen_before_kiosk_ = IsFullscreen();
    if (!fullscreen_before_kiosk_)
      SetFullScreen(true);
  } else if (!kiosk && is_kiosk_) {
    is_kiosk_ = false;
    if (!fullscreen_before_kiosk_)
      SetFullScreen(false);

    // Set presentation options *after* asynchronously exiting
    // fullscreen to ensure they take effect.
    [NSApp setPresentationOptions:kiosk_options_];
  }
}

bool NativeWindowMac::IsKiosk() const {
  return is_kiosk_;
}

void NativeWindowMac::SetBackgroundColor(SkColor color) {
  base::apple::ScopedCFTypeRef<CGColorRef> cgcolor(
      skia::CGColorCreateFromSkColor(color));
  [[[window_ contentView] layer] setBackgroundColor:cgcolor.get()];
}

SkColor NativeWindowMac::GetBackgroundColor() const {
  CGColorRef color = [[[window_ contentView] layer] backgroundColor];
  if (!color)
    return SK_ColorTRANSPARENT;
  return skia::CGColorRefToSkColor(color);
}

void NativeWindowMac::SetHasShadow(bool has_shadow) {
  [window_ setHasShadow:has_shadow];
}

bool NativeWindowMac::HasShadow() const {
  return [window_ hasShadow];
}

void NativeWindowMac::InvalidateShadow() {
  [window_ invalidateShadow];
}

void NativeWindowMac::SetOpacity(const double opacity) {
  const double boundedOpacity = std::clamp(opacity, 0.0, 1.0);
  [window_ setAlphaValue:boundedOpacity];
}

double NativeWindowMac::GetOpacity() const {
  return [window_ alphaValue];
}

void NativeWindowMac::SetRepresentedFilename(const std::string& filename) {
  [window_ setRepresentedFilename:base::SysUTF8ToNSString(filename)];
  if (buttons_proxy_)
    [buttons_proxy_ redraw];
}

std::string NativeWindowMac::GetRepresentedFilename() const {
  return base::SysNSStringToUTF8([window_ representedFilename]);
}

void NativeWindowMac::SetDocumentEdited(bool edited) {
  [window_ setDocumentEdited:edited];
  if (buttons_proxy_)
    [buttons_proxy_ redraw];
}

bool NativeWindowMac::IsDocumentEdited() const {
  return [window_ isDocumentEdited];
}

bool NativeWindowMac::IsHiddenInMissionControl() const {
  NSUInteger collectionBehavior = [window_ collectionBehavior];
  return collectionBehavior & NSWindowCollectionBehaviorTransient;
}

void NativeWindowMac::SetHiddenInMissionControl(bool hidden) {
  SetCollectionBehavior(hidden, NSWindowCollectionBehaviorTransient);
}

void NativeWindowMac::SetIgnoreMouseEvents(bool ignore, bool forward) {
  [window_ setIgnoresMouseEvents:ignore];
  if (!ignore) {
    SetForwardMouseMessages(NO);
  } else {
    SetForwardMouseMessages(forward);
  }
}

void NativeWindowMac::SetContentProtection(bool enable) {
  [window_
      setSharingType:enable ? NSWindowSharingNone : NSWindowSharingReadOnly];
}

void NativeWindowMac::SetFocusable(bool focusable) {
  // No known way to unfocus the window if it had the focus. Here we do not
  // want to call Focus(false) because it moves the window to the back, i.e.
  // at the bottom in term of z-order.
  [window_ setDisableKeyOrMainWindow:!focusable];
}

bool NativeWindowMac::IsFocusable() const {
  return ![window_ disableKeyOrMainWindow];
}

void NativeWindowMac::SetParentWindow(NativeWindow* parent) {
  InternalSetParentWindow(parent, IsVisible());
}

gfx::NativeView NativeWindowMac::GetNativeView() const {
  return [window_ contentView];
}

gfx::NativeWindow NativeWindowMac::GetNativeWindow() const {
  return window_;
}

gfx::AcceleratedWidget NativeWindowMac::GetAcceleratedWidget() const {
  return [window_ windowNumber];
}

content::DesktopMediaID NativeWindowMac::GetDesktopMediaID() const {
  auto desktop_media_id = content::DesktopMediaID(
      content::DesktopMediaID::TYPE_WINDOW, GetAcceleratedWidget());
  // c.f.
  // https://source.chromium.org/chromium/chromium/src/+/main:chrome/browser/media/webrtc/native_desktop_media_list.cc;l=775-780;drc=79502ab47f61bff351426f57f576daef02b1a8dc
  // Refs https://github.com/electron/electron/pull/30507
  // TODO(deepak1556): Match upstream for `kWindowCaptureMacV2`
#if 0
    if (remote_cocoa::ScopedCGWindowID::Get(desktop_media_id.id)) {
      desktop_media_id.window_id = desktop_media_id.id;
    }
#endif
  return desktop_media_id;
}

NativeWindowHandle NativeWindowMac::GetNativeWindowHandle() const {
  return [window_ contentView];
}

void NativeWindowMac::SetProgressBar(double progress,
                                     const NativeWindow::ProgressState state) {
  NSDockTile* dock_tile = [NSApp dockTile];

  // Sometimes macOS would install a default contentView for dock, we must
  // verify whether NSProgressIndicator has been installed.
  bool first_time = !dock_tile.contentView ||
                    [[dock_tile.contentView subviews] count] == 0 ||
                    ![[[dock_tile.contentView subviews] lastObject]
                        isKindOfClass:[NSProgressIndicator class]];

  // For the first time API invoked, we need to create a ContentView in
  // DockTile.
  if (first_time) {
    NSImageView* image_view = [[NSImageView alloc] init];
    [image_view setImage:[NSApp applicationIconImage]];
    [dock_tile setContentView:image_view];

    NSRect frame = NSMakeRect(0.0f, 0.0f, dock_tile.size.width, 15.0);
    NSProgressIndicator* progress_indicator =
        [[ElectronProgressBar alloc] initWithFrame:frame];
    [progress_indicator setStyle:NSProgressIndicatorStyleBar];
    [progress_indicator setIndeterminate:NO];
    [progress_indicator setBezeled:YES];
    [progress_indicator setMinValue:0];
    [progress_indicator setMaxValue:1];
    [progress_indicator setHidden:NO];
    [dock_tile.contentView addSubview:progress_indicator];
  }

  NSProgressIndicator* progress_indicator = static_cast<NSProgressIndicator*>(
      [[[dock_tile contentView] subviews] lastObject]);
  if (progress < 0) {
    [progress_indicator setHidden:YES];
  } else if (progress > 1) {
    [progress_indicator setHidden:NO];
    [progress_indicator setIndeterminate:YES];
    [progress_indicator setDoubleValue:1];
  } else {
    [progress_indicator setHidden:NO];
    [progress_indicator setDoubleValue:progress];
  }
  [dock_tile display];
}

void NativeWindowMac::SetOverlayIcon(const gfx::Image& overlay,
                                     const std::string& description) {}

void NativeWindowMac::SetVisibleOnAllWorkspaces(bool visible,
                                                bool visibleOnFullScreen,
                                                bool skipTransformProcessType) {
  // In order for NSWindows to be visible on fullscreen we need to invoke
  // app.dock.hide() since Apple changed the underlying functionality of
  // NSWindows starting with 10.14 to disallow NSWindows from floating on top of
  // fullscreen apps.
  if (!skipTransformProcessType) {
    if (visibleOnFullScreen) {
      Browser::Get()->DockHide();
    } else {
      Browser::Get()->DockShow(JavascriptEnvironment::GetIsolate());
    }
  }

  SetCollectionBehavior(visible, NSWindowCollectionBehaviorCanJoinAllSpaces);
  SetCollectionBehavior(visibleOnFullScreen,
                        NSWindowCollectionBehaviorFullScreenAuxiliary);
}

bool NativeWindowMac::IsVisibleOnAllWorkspaces() const {
  NSUInteger collectionBehavior = [window_ collectionBehavior];
  return collectionBehavior & NSWindowCollectionBehaviorCanJoinAllSpaces;
}

void NativeWindowMac::SetAutoHideCursor(bool auto_hide) {
  [window_ setDisableAutoHideCursor:!auto_hide];
}

void NativeWindowMac::UpdateVibrancyRadii(bool fullscreen) {
  NSVisualEffectView* vibrantView = [window_ vibrantView];

  if (vibrantView != nil && !vibrancy_type_.empty()) {
    const bool no_rounded_corner = !HasStyleMask(NSWindowStyleMaskTitled);
    const int macos_version = base::mac::MacOSMajorVersion();
    const bool modal = is_modal();

    // If the window is modal, its corners are rounded on macOS >= 11 or higher
    // unless the user has explicitly passed noRoundedCorners.
    bool should_round_modal =
        !no_rounded_corner && macos_version >= 11 && modal;
    // If the window is nonmodal, its corners are rounded if it is frameless and
    // the user hasn't passed noRoundedCorners.
    bool should_round_nonmodal = !no_rounded_corner && !modal && !has_frame();

    if (should_round_nonmodal || should_round_modal) {
      CGFloat radius;
      if (fullscreen) {
        radius = 0.0f;
      } else if (macos_version >= 11) {
        radius = 9.0f;
      } else {
        // Smaller corner radius on versions prior to Big Sur.
        radius = 5.0f;
      }

      CGFloat dimension = 2 * radius + 1;
      NSSize size = NSMakeSize(dimension, dimension);
      NSImage* maskImage = [NSImage imageWithSize:size
                                          flipped:NO
                                   drawingHandler:^BOOL(NSRect rect) {
                                     NSBezierPath* bezierPath = [NSBezierPath
                                         bezierPathWithRoundedRect:rect
                                                           xRadius:radius
                                                           yRadius:radius];
                                     [[NSColor blackColor] set];
                                     [bezierPath fill];
                                     return YES;
                                   }];

      [maskImage setCapInsets:NSEdgeInsetsMake(radius, radius, radius, radius)];
      [maskImage setResizingMode:NSImageResizingModeStretch];
      [vibrantView setMaskImage:maskImage];
      [window_ setCornerMask:maskImage];
    }
  }
}

void NativeWindowMac::UpdateWindowOriginalFrame() {
  original_frame_ = [window_ frame];
}

void NativeWindowMac::SetVibrancy(const std::string& type) {
  NativeWindow::SetVibrancy(type);

  NSVisualEffectView* vibrantView = [window_ vibrantView];

  if (type.empty()) {
    if (vibrantView == nil)
      return;

    [vibrantView removeFromSuperview];
    [window_ setVibrantView:nil];

    return;
  }

  NSVisualEffectMaterial vibrancyType{};
  if (type == "titlebar") {
    vibrancyType = NSVisualEffectMaterialTitlebar;
  } else if (type == "selection") {
    vibrancyType = NSVisualEffectMaterialSelection;
  } else if (type == "menu") {
    vibrancyType = NSVisualEffectMaterialMenu;
  } else if (type == "popover") {
    vibrancyType = NSVisualEffectMaterialPopover;
  } else if (type == "sidebar") {
    vibrancyType = NSVisualEffectMaterialSidebar;
  } else if (type == "header") {
    vibrancyType = NSVisualEffectMaterialHeaderView;
  } else if (type == "sheet") {
    vibrancyType = NSVisualEffectMaterialSheet;
  } else if (type == "window") {
    vibrancyType = NSVisualEffectMaterialWindowBackground;
  } else if (type == "hud") {
    vibrancyType = NSVisualEffectMaterialHUDWindow;
  } else if (type == "fullscreen-ui") {
    vibrancyType = NSVisualEffectMaterialFullScreenUI;
  } else if (type == "tooltip") {
    vibrancyType = NSVisualEffectMaterialToolTip;
  } else if (type == "content") {
    vibrancyType = NSVisualEffectMaterialContentBackground;
  } else if (type == "under-window") {
    vibrancyType = NSVisualEffectMaterialUnderWindowBackground;
  } else if (type == "under-page") {
    vibrancyType = NSVisualEffectMaterialUnderPageBackground;
  }

  if (vibrancyType) {
    vibrancy_type_ = type;

    if (vibrantView == nil) {
      vibrantView = [[NSVisualEffectView alloc]
          initWithFrame:[[window_ contentView] bounds]];
      [window_ setVibrantView:vibrantView];

      [vibrantView
          setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
      [vibrantView setBlendingMode:NSVisualEffectBlendingModeBehindWindow];

      if (visual_effect_state_ == VisualEffectState::kActive) {
        [vibrantView setState:NSVisualEffectStateActive];
      } else if (visual_effect_state_ == VisualEffectState::kInactive) {
        [vibrantView setState:NSVisualEffectStateInactive];
      } else {
        [vibrantView setState:NSVisualEffectStateFollowsWindowActiveState];
      }

      [[window_ contentView] addSubview:vibrantView
                             positioned:NSWindowBelow
                             relativeTo:nil];

      UpdateVibrancyRadii(IsFullscreen());
    }

    [vibrantView setMaterial:vibrancyType];
  }
}

void NativeWindowMac::SetWindowButtonVisibility(bool visible) {
  window_button_visibility_ = visible;
  if (buttons_proxy_) {
    if (visible)
      [buttons_proxy_ redraw];
    [buttons_proxy_ setVisible:visible];
  }

  if (title_bar_style_ != TitleBarStyle::kCustomButtonsOnHover)
    InternalSetWindowButtonVisibility(visible);

  NotifyLayoutWindowControlsOverlay();
}

bool NativeWindowMac::GetWindowButtonVisibility() const {
  return ![window_ standardWindowButton:NSWindowZoomButton].hidden ||
         ![window_ standardWindowButton:NSWindowMiniaturizeButton].hidden ||
         ![window_ standardWindowButton:NSWindowCloseButton].hidden;
}

void NativeWindowMac::SetWindowButtonPosition(
    std::optional<gfx::Point> position) {
  traffic_light_position_ = std::move(position);
  if (buttons_proxy_) {
    [buttons_proxy_ setMargin:traffic_light_position_];
    NotifyLayoutWindowControlsOverlay();
  }
}

std::optional<gfx::Point> NativeWindowMac::GetWindowButtonPosition() const {
  return traffic_light_position_;
}

void NativeWindowMac::RedrawTrafficLights() {
  if (buttons_proxy_ && !IsFullscreen())
    [buttons_proxy_ redraw];
}

// In simpleFullScreen mode, update the frame for new bounds.
void NativeWindowMac::UpdateFrame() {
  NSWindow* window = GetNativeWindow().GetNativeNSWindow();
  NSRect fullscreenFrame = [window.screen frame];
  [window setFrame:fullscreenFrame display:YES animate:YES];
}

void NativeWindowMac::SetTouchBar(
    std::vector<gin_helper::PersistentDictionary> items) {
  touch_bar_ = [[ElectronTouchBar alloc] initWithDelegate:window_delegate_
                                                   window:this
                                                 settings:std::move(items)];
  [window_ setTouchBar:nil];
}

void NativeWindowMac::RefreshTouchBarItem(const std::string& item_id) {
  if (touch_bar_ && [window_ touchBar])
    [touch_bar_ refreshTouchBarItem:[window_ touchBar] id:item_id];
}

void NativeWindowMac::SetEscapeTouchBarItem(
    gin_helper::PersistentDictionary item) {
  if (touch_bar_ && [window_ touchBar])
    [touch_bar_ setEscapeTouchBarItem:std::move(item)
                          forTouchBar:[window_ touchBar]];
}

void NativeWindowMac::SelectPreviousTab() {
  [window_ selectPreviousTab:nil];
}

void NativeWindowMac::SelectNextTab() {
  [window_ selectNextTab:nil];
}

void NativeWindowMac::ShowAllTabs() {
  [window_ toggleTabOverview:nil];
}

void NativeWindowMac::MergeAllWindows() {
  [window_ mergeAllWindows:nil];
}

void NativeWindowMac::MoveTabToNewWindow() {
  [window_ moveTabToNewWindow:nil];
}

void NativeWindowMac::ToggleTabBar() {
  [window_ toggleTabBar:nil];
}

bool NativeWindowMac::AddTabbedWindow(NativeWindow* window) {
  if (window_ == window->GetNativeWindow().GetNativeNSWindow()) {
    return false;
  } else {
    [window_ addTabbedWindow:window->GetNativeWindow().GetNativeNSWindow()
                     ordered:NSWindowAbove];
  }
  return true;
}

std::optional<std::string> NativeWindowMac::GetTabbingIdentifier() const {
  if ([window_ tabbingMode] == NSWindowTabbingModeDisallowed)
    return std::nullopt;

  return base::SysNSStringToUTF8([window_ tabbingIdentifier]);
}

void NativeWindowMac::SetAspectRatio(double aspect_ratio,
                                     const gfx::Size& extra_size) {
  NativeWindow::SetAspectRatio(aspect_ratio, extra_size);

  // Reset the behaviour to default if aspect_ratio is set to 0 or less.
  if (aspect_ratio > 0.0) {
    NSSize aspect_ratio_size = NSMakeSize(aspect_ratio, 1.0);
    if (has_frame())
      [window_ setContentAspectRatio:aspect_ratio_size];
    else
      [window_ setAspectRatio:aspect_ratio_size];
  } else {
    [window_ setResizeIncrements:NSMakeSize(1.0, 1.0)];
  }
}

void NativeWindowMac::PreviewFile(const std::string& path,
                                  const std::string& display_name) {
  preview_item_ = [[ElectronPreviewItem alloc]
      initWithURL:[NSURL fileURLWithPath:base::SysUTF8ToNSString(path)]
            title:base::SysUTF8ToNSString(display_name)];
  [[QLPreviewPanel sharedPreviewPanel] makeKeyAndOrderFront:nil];
}

void NativeWindowMac::CloseFilePreview() {
  if ([QLPreviewPanel sharedPreviewPanelExists]) {
    [[QLPreviewPanel sharedPreviewPanel] close];
  }
}

gfx::Rect NativeWindowMac::ContentBoundsToWindowBounds(
    const gfx::Rect& bounds) const {
  if (has_frame()) {
    gfx::Rect window_bounds(
        [window_ frameRectForContentRect:bounds.ToCGRect()]);
    int frame_height = window_bounds.height() - bounds.height();
    window_bounds.set_y(window_bounds.y() - frame_height);
    return window_bounds;
  } else {
    return bounds;
  }
}

gfx::Rect NativeWindowMac::WindowBoundsToContentBounds(
    const gfx::Rect& bounds) const {
  if (has_frame()) {
    gfx::Rect content_bounds(
        [window_ contentRectForFrameRect:bounds.ToCGRect()]);
    int frame_height = bounds.height() - content_bounds.height();
    content_bounds.set_y(content_bounds.y() + frame_height);
    return content_bounds;
  } else {
    return bounds;
  }
}

void NativeWindowMac::NotifyWindowEnterFullScreen() {
  NativeWindow::NotifyWindowEnterFullScreen();
  // Restore the window title under fullscreen mode.
  if (buttons_proxy_)
    [window_ setTitleVisibility:NSWindowTitleVisible];

  if (transparent() || !has_frame())
    [window_ setTitlebarAppearsTransparent:NO];
}

void NativeWindowMac::NotifyWindowLeaveFullScreen() {
  NativeWindow::NotifyWindowLeaveFullScreen();
  // Restore window buttons.
  if (buttons_proxy_ && window_button_visibility_.value_or(true)) {
    [buttons_proxy_ redraw];
    [buttons_proxy_ setVisible:YES];
  }

  if (transparent() || !has_frame())
    [window_ setTitlebarAppearsTransparent:YES];
}

void NativeWindowMac::NotifyWindowWillEnterFullScreen() {
  UpdateVibrancyRadii(true);
}

void NativeWindowMac::NotifyWindowWillLeaveFullScreen() {
  if (buttons_proxy_) {
    // Hide window title when leaving fullscreen.
    [window_ setTitleVisibility:NSWindowTitleHidden];
    // Hide the container otherwise traffic light buttons jump.
    [buttons_proxy_ setVisible:NO];
  }
  UpdateVibrancyRadii(false);
}

void NativeWindowMac::SetActive(bool is_key) {
  is_active_ = is_key;
}

bool NativeWindowMac::IsActive() const {
  return is_active_;
}

void NativeWindowMac::Cleanup() {
  DCHECK(!IsClosed());
  ui::NativeTheme::GetInstanceForNativeUi()->RemoveObserver(this);
  display::Screen::GetScreen()->RemoveObserver(this);
}

class NativeAppWindowFrameViewMac : public views::NativeFrameViewMac {
 public:
  NativeAppWindowFrameViewMac(views::Widget* frame, NativeWindowMac* window)
      : views::NativeFrameViewMac(frame), native_window_(window) {}

  NativeAppWindowFrameViewMac(const NativeAppWindowFrameViewMac&) = delete;
  NativeAppWindowFrameViewMac& operator=(const NativeAppWindowFrameViewMac&) =
      delete;

  ~NativeAppWindowFrameViewMac() override = default;

  // NonClientFrameView:
  int NonClientHitTest(const gfx::Point& point) override {
    if (!bounds().Contains(point))
      return HTNOWHERE;

    if (GetWidget()->IsFullscreen())
      return HTCLIENT;

    // Check for possible draggable region in the client area for the frameless
    // window.
    int contents_hit_test = native_window_->NonClientHitTest(point);
    if (contents_hit_test != HTNOWHERE)
      return contents_hit_test;

    return HTCLIENT;
  }

 private:
  // Weak.
  raw_ptr<NativeWindowMac> const native_window_;
};

std::unique_ptr<views::NonClientFrameView>
NativeWindowMac::CreateNonClientFrameView(views::Widget* widget) {
  return std::make_unique<NativeAppWindowFrameViewMac>(widget, this);
}

bool NativeWindowMac::HasStyleMask(NSUInteger flag) const {
  return [window_ styleMask] & flag;
}

void NativeWindowMac::SetStyleMask(bool on, NSUInteger flag) {
  // Changing the styleMask of a frameless windows causes it to change size so
  // we explicitly disable resizing while setting it.
  ScopedDisableResize disable_resize;

  if (on)
    [window_ setStyleMask:[window_ styleMask] | flag];
  else
    [window_ setStyleMask:[window_ styleMask] & (~flag)];

  // Change style mask will make the zoom button revert to default, probably
  // a bug of Cocoa or macOS.
  SetMaximizable(maximizable_);
}

void NativeWindowMac::SetCollectionBehavior(bool on, NSUInteger flag) {
  if (on)
    [window_ setCollectionBehavior:[window_ collectionBehavior] | flag];
  else
    [window_ setCollectionBehavior:[window_ collectionBehavior] & (~flag)];

  // Change collectionBehavior will make the zoom button revert to default,
  // probably a bug of Cocoa or macOS.
  SetMaximizable(maximizable_);
}

views::View* NativeWindowMac::GetContentsView() {
  return root_view_.get();
}

bool NativeWindowMac::CanMaximize() const {
  return maximizable_;
}

void NativeWindowMac::OnNativeThemeUpdated(ui::NativeTheme* observed_theme) {
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(&NativeWindow::RedrawTrafficLights, GetWeakPtr()));
}

void NativeWindowMac::AddContentViewLayers() {
  // Make sure the bottom corner is rounded for non-modal windows:
  // http://crbug.com/396264.
  if (!is_modal()) {
    // For normal window, we need to explicitly set layer for contentView to
    // make setBackgroundColor work correctly.
    // There is no need to do so for frameless window, and doing so would make
    // titleBarStyle stop working.
    if (has_frame()) {
      CALayer* background_layer = [[CALayer alloc] init];
      [background_layer
          setAutoresizingMask:kCALayerWidthSizable | kCALayerHeightSizable];
      [[window_ contentView] setLayer:background_layer];
    }
    [[window_ contentView] setWantsLayer:YES];
  }
}

void NativeWindowMac::InternalSetWindowButtonVisibility(bool visible) {
  [[window_ standardWindowButton:NSWindowCloseButton] setHidden:!visible];
  [[window_ standardWindowButton:NSWindowMiniaturizeButton] setHidden:!visible];
  [[window_ standardWindowButton:NSWindowZoomButton] setHidden:!visible];
}

void NativeWindowMac::InternalSetParentWindow(NativeWindow* new_parent,
                                              bool attach) {
  if (is_modal())
    return;

  // Do not remove/add if we are already properly attached.
  if (attach && new_parent &&
      [window_ parentWindow] ==
          new_parent->GetNativeWindow().GetNativeNSWindow())
    return;

  // Remove current parent window.
  RemoveChildFromParentWindow();

  // Set new parent window.
  if (new_parent) {
    new_parent->add_child_window(this);
    if (attach)
      new_parent->AttachChildren();
  }

  NativeWindow::SetParentWindow(new_parent);
}

void NativeWindowMac::SetForwardMouseMessages(bool forward) {
  [window_ setAcceptsMouseMovedEvents:forward];
}

std::optional<gfx::Rect> NativeWindowMac::GetWindowControlsOverlayRect() {
  if (!titlebar_overlay_)
    return std::nullopt;

  // On macOS, when in fullscreen mode, window controls (the menu bar, title
  // bar, and toolbar) are attached to a separate NSView that slides down from
  // the top of the screen, independent of, and overlapping the WebContents.
  // Disable WCO when in fullscreen, because this space is inaccessible to
  // WebContents. https://crbug.com/915110.
  if (IsFullscreen())
    return gfx::Rect();

  if (buttons_proxy_ && window_button_visibility_.value_or(true)) {
    NSRect buttons = [buttons_proxy_ getButtonsContainerBounds];
    gfx::Rect overlay;
    overlay.set_width(GetContentSize().width() - NSWidth(buttons));
    overlay.set_height([buttons_proxy_ useCustomHeight]
                           ? titlebar_overlay_height()
                           : NSHeight(buttons));

    if (!base::i18n::IsRTL())
      overlay.set_x(NSMaxX(buttons));

    return overlay;
  }

  return std::nullopt;
}

// static
NativeWindow* NativeWindow::Create(const gin_helper::Dictionary& options,
                                   NativeWindow* parent) {
  return new NativeWindowMac(options, parent);
}

}  // namespace electron
