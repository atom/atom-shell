// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#include "shell/browser/ui/cocoa/electron_inspectable_web_contents_view.h"

#include "content/public/browser/render_widget_host_view.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/ui/cocoa/event_dispatching_window.h"
#include "shell/browser/ui/inspectable_web_contents.h"
#include "shell/browser/ui/inspectable_web_contents_view_delegate.h"
#include "shell/browser/ui/inspectable_web_contents_view_mac.h"
#include "ui/base/cocoa/base_view.h"
#include "ui/gfx/mac/scoped_cocoa_disable_screen_updates.h"

@implementation ElectronInspectableWebContentsView

- (instancetype)initWithInspectableWebContentsViewMac:
    (InspectableWebContentsViewMac*)view {
  self = [super init];
  if (!self)
    return nil;

  inspectableWebContentsView_ = view;
  devtools_visible_ = NO;
  devtools_docked_ = NO;
  devtools_is_first_responder_ = NO;
  attached_to_window_ = NO;

  if (inspectableWebContentsView_->inspectable_web_contents()->is_guest()) {
    fake_view_ = [[NSView alloc] init];
    [fake_view_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [self addSubview:fake_view_];
  } else {
    auto* contents = inspectableWebContentsView_->inspectable_web_contents()
                         ->GetWebContents();
    auto* contentsView = contents->GetNativeView().GetNativeNSView();
    [contentsView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [self addSubview:contentsView];
  }

  // See https://code.google.com/p/chromium/issues/detail?id=348490.
  [self setWantsLayer:YES];

  return self;
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)resizeSubviewsWithOldSize:(NSSize)oldBoundsSize {
  [self adjustSubviews];
}

- (void)viewDidMoveToWindow {
  if (attached_to_window_ && !self.window) {
    attached_to_window_ = NO;
    [[NSNotificationCenter defaultCenter] removeObserver:self];
  } else if (!attached_to_window_ && self.window) {
    attached_to_window_ = YES;
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(viewDidBecomeFirstResponder:)
               name:kViewDidBecomeFirstResponder
             object:nil];
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(parentWindowBecameMain:)
               name:NSWindowDidBecomeMainNotification
             object:nil];
  }
}

- (IBAction)showDevTools:(id)sender {
  inspectableWebContentsView_->inspectable_web_contents()->ShowDevTools(true);
}

- (void)notifyDevToolsFocused {
  if (inspectableWebContentsView_->GetDelegate())
    inspectableWebContentsView_->GetDelegate()->DevToolsFocused();
}

- (void)notifyDevToolsResized {
  // When devtools is opened, resizing devtools would not trigger
  // UpdateDraggableRegions for WebContents, so we have to notify the window
  // to do an update of draggable regions.
  if (inspectableWebContentsView_->GetDelegate())
    inspectableWebContentsView_->GetDelegate()->DevToolsResized();
}

- (void)setDevToolsVisible:(BOOL)visible activate:(BOOL)activate {
  if (visible == devtools_visible_)
    return;

  auto* inspectable_web_contents =
      inspectableWebContentsView_->inspectable_web_contents();
  auto* devToolsWebContents =
      inspectable_web_contents->GetDevToolsWebContents();
  auto* devToolsView = devToolsWebContents->GetNativeView().GetNativeNSView();

  devtools_visible_ = visible;
  if (devtools_docked_) {
    if (visible) {
      // Place the devToolsView under contentsView, notice that we didn't set
      // sizes for them until the setContentsResizingStrategy message.
      [self addSubview:devToolsView positioned:NSWindowBelow relativeTo:nil];
      [self adjustSubviews];

      // Focus on web view.
      devToolsWebContents->RestoreFocus();
    } else {
      gfx::ScopedCocoaDisableScreenUpdates disabler;
      [devToolsView removeFromSuperview];
      [self adjustSubviews];
      [self notifyDevToolsResized];
    }
  } else {
    if (visible) {
      if (activate) {
        [devtools_window_ makeKeyAndOrderFront:nil];
      } else {
        [devtools_window_ orderBack:nil];
      }
    } else {
      [devtools_window_ setDelegate:nil];
      [devtools_window_ close];
      devtools_window_ = nil;
    }
  }
}

- (BOOL)isDevToolsVisible {
  return devtools_visible_;
}

- (BOOL)isDevToolsFocused {
  if (devtools_docked_) {
    return [[self window] isKeyWindow] && devtools_is_first_responder_;
  } else {
    return [devtools_window_ isKeyWindow];
  }
}

- (void)setIsDocked:(BOOL)docked activate:(BOOL)activate {
  // Revert to no-devtools state.
  [self setDevToolsVisible:NO activate:NO];

  // Switch to new state.
  devtools_docked_ = docked;
  auto* inspectable_web_contents =
      inspectableWebContentsView_->inspectable_web_contents();
  auto* devToolsWebContents =
      inspectable_web_contents->GetDevToolsWebContents();
  auto devToolsView = devToolsWebContents->GetNativeView().GetNativeNSView();
  if (!docked) {
    auto styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                     NSWindowStyleMaskMiniaturizable |
                     NSWindowStyleMaskResizable |
                     NSWindowStyleMaskTexturedBackground |
                     NSWindowStyleMaskUnifiedTitleAndToolbar;
    devtools_window_ = [[EventDispatchingWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 800, 600)
                  styleMask:styleMask
                    backing:NSBackingStoreBuffered
                      defer:YES];
    [devtools_window_ setDelegate:self];
    [devtools_window_ setFrameAutosaveName:@"electron.devtools"];
    [devtools_window_ setTitle:@"Developer Tools"];
    [devtools_window_ setReleasedWhenClosed:NO];
    [devtools_window_ setAutorecalculatesContentBorderThickness:NO
                                                        forEdge:NSMaxYEdge];
    [devtools_window_ setContentBorderThickness:24 forEdge:NSMaxYEdge];

    NSView* contentView = [devtools_window_ contentView];
    devToolsView.frame = contentView.bounds;
    devToolsView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    [contentView addSubview:devToolsView];
    [devToolsView setMouseDownCanMoveWindow:NO];
  } else {
    [devToolsView setMouseDownCanMoveWindow:YES];
  }
  [self setDevToolsVisible:YES activate:activate];
}

- (void)setContentsResizingStrategy:
    (const DevToolsContentsResizingStrategy&)strategy {
  strategy_.CopyFrom(strategy);
  [self adjustSubviews];
}

- (void)adjustSubviews {
  if (![[self subviews] count])
    return;

  if (![self isDevToolsVisible] || devtools_window_) {
    DCHECK_EQ(1u, [[self subviews] count]);
    NSView* contents = [[self subviews] objectAtIndex:0];
    [contents setFrame:[self bounds]];
    return;
  }

  NSView* devToolsView = [[self subviews] objectAtIndex:0];
  NSView* contentsView = [[self subviews] objectAtIndex:1];

  DCHECK_EQ(2u, [[self subviews] count]);

  gfx::Rect new_devtools_bounds;
  gfx::Rect new_contents_bounds;
  ApplyDevToolsContentsResizingStrategy(
      strategy_, gfx::Size(NSSizeToCGSize([self bounds].size)),
      &new_devtools_bounds, &new_contents_bounds);
  [devToolsView setFrame:[self flipRectToNSRect:new_devtools_bounds]];
  [contentsView setFrame:[self flipRectToNSRect:new_contents_bounds]];

  // Move mask to the devtools area to exclude it from dragging.
  NSRect cf = contentsView.frame;
  NSRect sb = [self bounds];
  NSRect devtools_frame;
  if (cf.size.height < sb.size.height) {  // bottom docked
    devtools_frame.origin.x = 0;
    devtools_frame.origin.y = 0;
    devtools_frame.size.width = sb.size.width;
    devtools_frame.size.height = sb.size.height - cf.size.height;
  } else {                // left or right docked
    if (cf.origin.x > 0)  // left docked
      devtools_frame.origin.x = 0;
    else  // right docked.
      devtools_frame.origin.x = cf.size.width;
    devtools_frame.origin.y = 0;
    devtools_frame.size.width = sb.size.width - cf.size.width;
    devtools_frame.size.height = sb.size.height;
  }

  [self notifyDevToolsResized];
}

- (void)setTitle:(NSString*)title {
  [devtools_window_ setTitle:title];
}

- (NSString*)getTitle {
  return [devtools_window_ title];
}

- (void)viewDidBecomeFirstResponder:(NSNotification*)notification {
  auto* inspectable_web_contents =
      inspectableWebContentsView_->inspectable_web_contents();
  DCHECK(inspectable_web_contents);
  auto* webContents = inspectable_web_contents->GetWebContents();
  if (!webContents)
    return;
  auto* webContentsView = webContents->GetNativeView().GetNativeNSView();

  NSView* view = [notification object];
  if ([[webContentsView subviews] containsObject:view]) {
    devtools_is_first_responder_ = NO;
    return;
  }

  auto* devToolsWebContents =
      inspectable_web_contents->GetDevToolsWebContents();
  if (!devToolsWebContents)
    return;
  auto devToolsView = devToolsWebContents->GetNativeView().GetNativeNSView();

  if ([[devToolsView subviews] containsObject:view]) {
    devtools_is_first_responder_ = YES;
    [self notifyDevToolsFocused];
  }
}

- (void)parentWindowBecameMain:(NSNotification*)notification {
  NSWindow* parentWindow = [notification object];
  if ([self window] == parentWindow && devtools_docked_ &&
      devtools_is_first_responder_)
    [self notifyDevToolsFocused];
}

- (void)redispatchContextMenuEvent:(base::apple::OwnedNSEvent)event {
  DCHECK(event.Get().type == NSEventTypeRightMouseDown ||
         (event.Get().type == NSEventTypeLeftMouseDown &&
          (event.Get().modifierFlags & NSEventModifierFlagControl)));
  content::WebContents* contents =
      inspectableWebContentsView_->inspectable_web_contents()->GetWebContents();
  electron::api::WebContents* api_contents =
      electron::api::WebContents::From(contents);
  if (api_contents) {
    // Temporarily pretend that the WebContents is fully non-draggable while we
    // re-send the mouse event. This allows the re-dispatched event to "land"
    // on the WebContents, instead of "falling through" back to the window.
    auto* rwhv = contents->GetRenderWidgetHostView();
    if (rwhv) {
      api_contents->SetForceNonDraggable(true);
      BaseView* contentsView =
          (BaseView*)rwhv->GetNativeView().GetNativeNSView();
      [contentsView mouseEvent:event.Get()];
      api_contents->SetForceNonDraggable(false);
    }
  }
}

#pragma mark - NSWindowDelegate

- (void)windowWillClose:(NSNotification*)notification {
  inspectableWebContentsView_->inspectable_web_contents()->CloseDevTools();
}

- (void)windowDidBecomeMain:(NSNotification*)notification {
  content::WebContents* web_contents =
      inspectableWebContentsView_->inspectable_web_contents()
          ->GetDevToolsWebContents();
  if (!web_contents)
    return;

  web_contents->RestoreFocus();

  content::RenderWidgetHostView* rwhv = web_contents->GetRenderWidgetHostView();
  if (rwhv)
    rwhv->SetActive(true);

  [self notifyDevToolsFocused];
}

- (void)windowDidResignMain:(NSNotification*)notification {
  content::WebContents* web_contents =
      inspectableWebContentsView_->inspectable_web_contents()
          ->GetDevToolsWebContents();
  if (!web_contents)
    return;

  web_contents->StoreFocus();

  content::RenderWidgetHostView* rwhv = web_contents->GetRenderWidgetHostView();
  if (rwhv)
    rwhv->SetActive(false);
}

@end
