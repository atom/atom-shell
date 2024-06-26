// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/cocoa/electron_ns_window.h"

#include "base/strings/sys_string_conversions.h"
#include "shell/browser/native_window_mac.h"
#include "shell/browser/ui/cocoa/electron_preview_item.h"
#include "shell/browser/ui/cocoa/electron_touch_bar.h"
#include "shell/browser/ui/cocoa/root_view_mac.h"
#include "ui/base/cocoa/window_size_constants.h"

#import <objc/message.h>
#import <objc/runtime.h>

namespace electron {

int ScopedDisableResize::disable_resize_ = 0;

}  // namespace electron

@interface NSWindow (PrivateAPI)
- (NSImage*)_cornerMask;
- (int64_t)_resizeDirectionForMouseLocation:(CGPoint)location;
@end

// See components/remote_cocoa/app_shim/native_widget_mac_nswindow.mm
@interface NSView (CRFrameViewAdditions)
- (void)cr_mouseDownOnFrameView:(NSEvent*)event;
@end

typedef void (*MouseDownImpl)(id, SEL, NSEvent*);

namespace {
MouseDownImpl g_nsthemeframe_mousedown;
MouseDownImpl g_nsnextstepframe_mousedown;
}  // namespace

// This class is never instantiated, it's just a container for our swizzled
// mouseDown method.
@interface SwizzledMethodsClass : NSView
@end

@implementation SwizzledMethodsClass
- (void)swiz_nsthemeframe_mouseDown:(NSEvent*)event {
  if ([self.window respondsToSelector:@selector(shell)]) {
    electron::NativeWindowMac* shell =
        (electron::NativeWindowMac*)[(id)self.window shell];
    if (shell && !shell->has_frame())
      [self cr_mouseDownOnFrameView:event];
    g_nsthemeframe_mousedown(self, @selector(mouseDown:), event);
  }
}

- (void)swiz_nsnextstepframe_mouseDown:(NSEvent*)event {
  if ([self.window respondsToSelector:@selector(shell)]) {
    electron::NativeWindowMac* shell =
        (electron::NativeWindowMac*)[(id)self.window shell];
    if (shell && !shell->has_frame()) {
      [self cr_mouseDownOnFrameView:event];
    }
    g_nsnextstepframe_mousedown(self, @selector(mouseDown:), event);
  }
}

- (void)swiz_nsview_swipeWithEvent:(NSEvent*)event {
  if ([self.window respondsToSelector:@selector(shell)]) {
    electron::NativeWindowMac* shell =
        (electron::NativeWindowMac*)[(id)self.window shell];
    if (shell) {
      if (event.deltaY == 1.0) {
        shell->NotifyWindowSwipe("up");
      } else if (event.deltaX == -1.0) {
        shell->NotifyWindowSwipe("right");
      } else if (event.deltaY == -1.0) {
        shell->NotifyWindowSwipe("down");
      } else if (event.deltaX == 1.0) {
        shell->NotifyWindowSwipe("left");
      }
    }
  }
}
@end

namespace {
#if IS_MAS_BUILD()
void SwizzleMouseDown(NSView* frame_view,
                      SEL swiz_selector,
                      MouseDownImpl* orig_impl) {
  Method original_mousedown =
      class_getInstanceMethod([frame_view class], @selector(mouseDown:));
  *orig_impl = (MouseDownImpl)method_getImplementation(original_mousedown);
  Method new_mousedown =
      class_getInstanceMethod([SwizzledMethodsClass class], swiz_selector);
  method_setImplementation(original_mousedown,
                           method_getImplementation(new_mousedown));
}
#else
// components/remote_cocoa/app_shim/bridged_content_view.h overrides
// swipeWithEvent, so we can't just override the implementation
// in ElectronNSWindow like we do with for ex. rotateWithEvent.
void SwizzleSwipeWithEvent(NSView* view, SEL swiz_selector) {
  Method original_swipe_with_event =
      class_getInstanceMethod([view class], @selector(swipeWithEvent:));
  Method new_swipe_with_event =
      class_getInstanceMethod([SwizzledMethodsClass class], swiz_selector);
  method_setImplementation(original_swipe_with_event,
                           method_getImplementation(new_swipe_with_event));
}
#endif
}  // namespace

@implementation ElectronNSWindow

@synthesize acceptsFirstMouse;
@synthesize enableLargerThanScreen;
@synthesize disableAutoHideCursor;
@synthesize disableKeyOrMainWindow;
@synthesize vibrantView;
@synthesize cornerMask;

- (id)initWithShell:(electron::NativeWindowMac*)shell
          styleMask:(NSUInteger)styleMask {
  if ((self = [super initWithContentRect:ui::kWindowSizeDeterminedLater
                               styleMask:styleMask
                                 backing:NSBackingStoreBuffered
                                   defer:NO])) {
#if IS_MAS_BUILD()
    // The first time we create a frameless window, we swizzle the
    // implementation of -[NSNextStepFrame mouseDown:], replacing it with our
    // own.
    // This is only necessary on MAS where we can't directly refer to
    // NSNextStepFrame or NSThemeFrame, as they are private APIs.
    // See components/remote_cocoa/app_shim/native_widget_mac_nswindow.mm for
    // the non-MAS-compatible way of doing this.
    if (styleMask & NSWindowStyleMaskTitled) {
      if (!g_nsthemeframe_mousedown) {
        NSView* theme_frame = [[self contentView] superview];
        DCHECK(strcmp(class_getName([theme_frame class]), "NSThemeFrame") == 0)
            << "Expected NSThemeFrame but was "
            << class_getName([theme_frame class]);
        SwizzleMouseDown(theme_frame, @selector(swiz_nsthemeframe_mouseDown:),
                         &g_nsthemeframe_mousedown);
      }
    } else {
      if (!g_nsnextstepframe_mousedown) {
        NSView* nextstep_frame = [[self contentView] superview];
        DCHECK(strcmp(class_getName([nextstep_frame class]),
                      "NSNextStepFrame") == 0)
            << "Expected NSNextStepFrame but was "
            << class_getName([nextstep_frame class]);
        SwizzleMouseDown(nextstep_frame,
                         @selector(swiz_nsnextstepframe_mouseDown:),
                         &g_nsnextstepframe_mousedown);
      }
    }
#else
    NSView* view = [[self contentView] superview];
    SwizzleSwipeWithEvent(view, @selector(swiz_nsview_swipeWithEvent:));
#endif  // IS_MAS_BUILD
    shell_ = shell;
  }
  return self;
}

- (electron::NativeWindowMac*)shell {
  return shell_;
}

- (id)accessibilityFocusedUIElement {
  views::Widget* widget = shell_->widget();
  id superFocus = [super accessibilityFocusedUIElement];
  if (!widget || shell_->IsFocused())
    return superFocus;
  return nil;
}
- (NSRect)originalContentRectForFrameRect:(NSRect)frameRect {
  return [super contentRectForFrameRect:frameRect];
}

- (NSTouchBar*)makeTouchBar {
  if (shell_->touch_bar())
    return [shell_->touch_bar() makeTouchBar];
  else
    return nil;
}

// NSWindow overrides.

- (void)rotateWithEvent:(NSEvent*)event {
  shell_->NotifyWindowRotateGesture(event.rotation);
}

- (NSRect)contentRectForFrameRect:(NSRect)frameRect {
  if (shell_->has_frame())
    return [super contentRectForFrameRect:frameRect];
  else
    return frameRect;
}

- (NSRect)constrainFrameRect:(NSRect)frameRect toScreen:(NSScreen*)screen {
  // We initialize the window in headless mode to allow painting before it is
  // shown, but we don't want the headless behavior of allowing the window to be
  // placed unconstrained.
  self.isHeadless = false;

  // Resizing is disabled.
  if (electron::ScopedDisableResize::IsResizeDisabled())
    return [self frame];

  NSRect result = [super constrainFrameRect:frameRect toScreen:screen];
  // Enable the window to be larger than screen.
  if ([self enableLargerThanScreen]) {
    // If we have a frame, ensure that we only position the window
    // somewhere where the user can move or resize it (and not
    // behind the menu bar, for instance)
    //
    // If there's no frame, put the window wherever the developer
    // wanted it to go
    if (shell_->has_frame()) {
      result.size = frameRect.size;
    } else {
      result = frameRect;
    }
  }

  return result;
}

- (void)setFrame:(NSRect)windowFrame display:(BOOL)displayViews {
  // constrainFrameRect is not called on hidden windows so disable adjusting
  // the frame directly when resize is disabled
  if (!electron::ScopedDisableResize::IsResizeDisabled())
    [super setFrame:windowFrame display:displayViews];
}

- (id)accessibilityAttributeValue:(NSString*)attribute {
  if ([attribute isEqual:NSAccessibilityEnabledAttribute])
    return [NSNumber numberWithBool:YES];
  if (![attribute isEqualToString:@"AXChildren"])
    return [super accessibilityAttributeValue:attribute];

  // We want to remove the window title (also known as
  // NSAccessibilityReparentingCellProxy), which VoiceOver already sees.
  // * when VoiceOver is disabled, this causes Cmd+C to be used for TTS but
  //   still leaves the buttons available in the accessibility tree.
  // * when VoiceOver is enabled, the full accessibility tree is used.
  // Without removing the title and with VO disabled, the TTS would always read
  // the window title instead of using Cmd+C to get the selected text.
  NSPredicate* predicate =
      [NSPredicate predicateWithFormat:@"(self.className != %@)",
                                       @"NSAccessibilityReparentingCellProxy"];

  NSArray* children = [super accessibilityAttributeValue:attribute];
  NSMutableArray* mutableChildren = [children mutableCopy];
  [mutableChildren filterUsingPredicate:predicate];

  return mutableChildren;
}

- (NSString*)accessibilityTitle {
  return base::SysUTF8ToNSString(shell_->GetTitle());
}

- (BOOL)canBecomeMainWindow {
  return !self.disableKeyOrMainWindow;
}

- (BOOL)canBecomeKeyWindow {
  return !self.disableKeyOrMainWindow;
}

- (NSView*)frameView {
  return [[self contentView] superview];
}

- (BOOL)validateUserInterfaceItem:(id<NSValidatedUserInterfaceItem>)item {
  // By default "Close Window" is always disabled when window has no title, to
  // support closing a window without title we need to manually do menu item
  // validation. This code path is used by the "roundedCorners" option.
  if ([item action] == @selector(performClose:))
    return shell_->IsClosable();
  return [super validateUserInterfaceItem:item];
}

// By overriding this built-in method the corners of the vibrant view (if set)
// will be smooth.
- (NSImage*)_cornerMask {
  if (self.vibrantView != nil) {
    return [self cornerMask];
  } else {
    return [super _cornerMask];
  }
}

// Quicklook methods

- (BOOL)acceptsPreviewPanelControl:(QLPreviewPanel*)panel {
  return YES;
}

- (void)beginPreviewPanelControl:(QLPreviewPanel*)panel {
  panel.dataSource = static_cast<id<QLPreviewPanelDataSource>>([self delegate]);
}

- (void)endPreviewPanelControl:(QLPreviewPanel*)panel {
  panel.dataSource = nil;
}

// Custom window button methods

- (BOOL)windowShouldClose:(id)sender {
  return YES;
}

- (void)performClose:(id)sender {
  if (shell_->title_bar_style() ==
      electron::NativeWindowMac::TitleBarStyle::kCustomButtonsOnHover) {
    [[self delegate] windowShouldClose:self];
  } else if (!([self styleMask] & NSWindowStyleMaskTitled)) {
    // performClose does not work for windows without title, so we have to
    // emulate its behavior. This code path is used by "simpleFullscreen" and
    // "roundedCorners" options.
    if ([[self delegate] respondsToSelector:@selector(windowShouldClose:)]) {
      if (![[self delegate] windowShouldClose:self])
        return;
    } else if ([self respondsToSelector:@selector(windowShouldClose:)]) {
      if (![self windowShouldClose:self])
        return;
    }
    [self close];
  } else if (shell_->is_modal() && shell_->parent() && shell_->IsVisible()) {
    // We don't want to actually call [window close] here since
    // we've already called endSheet on the modal sheet.
    return;
  } else {
    [super performClose:sender];
  }
}

- (BOOL)toggleFullScreenMode:(id)sender {
  if (!shell_->has_frame() && !shell_->HasStyleMask(NSWindowStyleMaskTitled))
    return NO;

  bool is_simple_fs = shell_->IsSimpleFullScreen();
  bool always_simple_fs = shell_->always_simple_fullscreen();

  // If we're in simple fullscreen mode and trying to exit it
  // we need to ensure we exit it properly to prevent a crash
  // with NSWindowStyleMaskTitled mode.
  if (is_simple_fs || always_simple_fs) {
    shell_->SetSimpleFullScreen(!is_simple_fs);
  } else {
    if (shell_->IsVisible()) {
      // Until 10.13, AppKit would obey a call to -toggleFullScreen: made inside
      // windowDidEnterFullScreen & windowDidExitFullScreen. Starting in 10.13,
      // it behaves as though the transition is still in progress and just emits
      // "not in a fullscreen state" when trying to exit fullscreen in the same
      // runloop that entered it. To handle this, invoke -toggleFullScreen:
      // asynchronously.
      [super performSelector:@selector(toggleFullScreen:)
                  withObject:nil
                  afterDelay:0];
    } else {
      [super toggleFullScreen:sender];
    }

    // Exiting fullscreen causes Cocoa to redraw the NSWindow, which resets
    // the enabled state for NSWindowZoomButton. We need to persist it.
    bool maximizable = shell_->IsMaximizable();
    shell_->SetMaximizable(maximizable);
  }

  return YES;
}

- (void)performMiniaturize:(id)sender {
  if (shell_->title_bar_style() ==
      electron::NativeWindowMac::TitleBarStyle::kCustomButtonsOnHover)
    [self miniaturize:self];
  else
    [super performMiniaturize:sender];
}

@end
