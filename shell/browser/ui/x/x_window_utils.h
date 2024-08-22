// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_UI_X_X_WINDOW_UTILS_H_
#define ELECTRON_SHELL_BROWSER_UI_X_X_WINDOW_UTILS_H_

#include <string>

#include "ui/gfx/x/xproto.h"

namespace electron {

// Sets the _NET_WM_WINDOW_TYPE of window.
void SetWindowType(x11::Window window, const std::string& type);

// Returns true if the bus name "com.canonical.AppMenu.Registrar" is available.
bool ShouldUseGlobalMenuBar();

// Bring the given window to the front regardless of focus.
void MoveWindowToForeground(x11::Window window);

// Move a given window above the other one.
void MoveWindowAbove(x11::Window window, x11::Window other_window);

// Return true is the given window exists, false otherwise.
bool IsWindowValid(x11::Window window);

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_X_X_WINDOW_UTILS_H_
