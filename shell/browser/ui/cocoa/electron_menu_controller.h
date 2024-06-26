// Copyright (c) 2013 GitHub, Inc.
// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_UI_COCOA_ELECTRON_MENU_CONTROLLER_H_
#define ELECTRON_SHELL_BROWSER_UI_COCOA_ELECTRON_MENU_CONTROLLER_H_

#import <Cocoa/Cocoa.h>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"

namespace electron {
class ElectronMenuModel;
}

// A controller for the cross-platform menu model. The menu that's created
// has the tag and represented object set for each menu item. The object is a
// NSValue holding a pointer to the model for that level of the menu (to
// allow for hierarchical menus). The tag is the index into that model for
// that particular item. It is important that the model outlives this object
// as it only maintains weak references.
@interface ElectronMenuController
    : NSObject <NSMenuDelegate, NSSharingServiceDelegate> {
 @protected
  base::WeakPtr<electron::ElectronMenuModel> model_;
  NSMenu* __strong menu_;
  BOOL isMenuOpen_;
  BOOL useDefaultAccelerator_;
  base::OnceClosure closeCallback;
}

// Builds a NSMenu from the pre-built model (must not be nil). Changes made
// to the contents of the model after calling this will not be noticed.
- (id)initWithModel:(electron::ElectronMenuModel*)model
    useDefaultAccelerator:(BOOL)use;

- (void)setCloseCallback:(base::OnceClosure)callback;

// Populate current NSMenu with |model|.
- (void)populateWithModel:(electron::ElectronMenuModel*)model;

// Programmatically close the constructed menu.
- (void)cancel;

- (electron::ElectronMenuModel*)model;
- (void)setModel:(electron::ElectronMenuModel*)model;

// Access to the constructed menu if the complex initializer was used. If the
// default initializer was used, then this will create the menu on first call.
- (NSMenu*)menu;

- (NSMenuItem*)makeMenuItemForIndex:(NSInteger)index
                          fromModel:(electron::ElectronMenuModel*)model;

// Whether the menu is currently open.
- (BOOL)isMenuOpen;

// NSMenuDelegate methods this class implements. Subclasses should call super
// if extending the behavior.
- (void)menuWillOpen:(NSMenu*)menu;
- (void)menuDidClose:(NSMenu*)menu;

@end

#endif  // ELECTRON_SHELL_BROWSER_UI_COCOA_ELECTRON_MENU_CONTROLLER_H_
