// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "content/public/browser/render_widget_host_view.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/ui/cocoa/event_dispatching_window.h"
#include "shell/browser/web_contents_preferences.h"
#include "ui/base/cocoa/command_dispatcher.h"
#include "ui/base/cocoa/nsmenu_additions.h"
#include "ui/base/cocoa/nsmenuitem_additions.h"
#include "ui/events/keycodes/keyboard_codes.h"

#import <Cocoa/Cocoa.h>

@interface NSWindow (EventDispatchingWindow)
- (void)redispatchKeyEvent:(NSEvent*)event;
@end

namespace electron::api {

bool WebContents::IsFocused() const {
  auto* view = web_contents()->GetRenderWidgetHostView();
  if (!view)
    return false;

  if (type() != Type::kBackgroundPage) {
    auto window = [web_contents()->GetNativeView().GetNativeNSView() window];
    // On Mac the render widget host view does not lose focus when the window
    // loses focus so check if the top level window is the key window.
    if (window && ![window isKeyWindow])
      return false;
  }

  return view->HasFocus();
}

bool WebContents::PlatformHandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (event.skip_if_unhandled ||
      event.GetType() == input::NativeWebKeyboardEvent::Type::kChar)
    return false;

  // Check if the webContents has preferences and to ignore shortcuts
  auto* web_preferences = WebContentsPreferences::From(source);
  if (web_preferences && web_preferences->ShouldIgnoreMenuShortcuts())
    return false;

  NSEvent* ns_event = event.os_event.Get();
  // Send the event to the menu before sending it to the window
  if (ns_event.type == NSEventTypeKeyDown) {
    // If the keyboard event is a system shortcut, it's already sent to the
    // NSMenu instance in
    // content/app_shim_remote_cocoa/render_widget_host_view_cocoa.mm via
    // keyEvent:(NSEvent*)theEvent wasKeyEquivalent:(BOOL)equiv. If we let the
    // NSMenu handle it here as well, it'll be sent twice with unexpected side
    // effects.
    bool is_a_keyboard_shortcut_event =
        [[NSApp mainMenu] cr_menuItemForKeyEquivalentEvent:ns_event] != nil;
    bool is_a_system_shortcut_event =
        is_a_keyboard_shortcut_event &&
        (ui::cocoa::ModifierMaskForKeyEvent(ns_event) &
         NSEventModifierFlagFunction) != 0;
    if (is_a_system_shortcut_event)
      return false;

    if ([[NSApp mainMenu] performKeyEquivalent:ns_event])
      return true;
  }

  // Let the window redispatch the OS event
  if (ns_event.window &&
      [ns_event.window isKindOfClass:[EventDispatchingWindow class]]) {
    [ns_event.window redispatchKeyEvent:ns_event];
    // FIXME(nornagon): this isn't the right return value; we should implement
    // devtools windows as Widgets in order to take advantage of the
    // preexisting redispatch code in bridged_native_widget.
    return false;
  } else if (ns_event.window &&
             [ns_event.window
                 conformsToProtocol:@protocol(CommandDispatchingWindow)]) {
    NSObject<CommandDispatchingWindow>* window =
        static_cast<NSObject<CommandDispatchingWindow>*>(ns_event.window);
    [[window commandDispatcher] redispatchKeyEvent:ns_event];
    // FIXME(clavin): Not exactly sure what to return here, likely the same
    // situation as the branch above. If a future refactor removes
    // |EventDispatchingWindow| then only this branch will need to remain.
    return false;
  }

  return false;
}

}  // namespace electron::api
