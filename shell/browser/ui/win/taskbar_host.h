// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_UI_WIN_TASKBAR_HOST_H_
#define ELECTRON_SHELL_BROWSER_UI_WIN_TASKBAR_HOST_H_

#include <shobjidl.h>
#include <wrl/client.h>

#include <map>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "shell/browser/native_window.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image.h"

namespace electron {

class TaskbarHost {
 public:
  struct ThumbarButton {
    std::string tooltip;
    gfx::Image icon;
    std::vector<std::string> flags;
    base::RepeatingClosure clicked_callback;

    ThumbarButton();
    ThumbarButton(const ThumbarButton&);
    ~ThumbarButton();
  };

  TaskbarHost();
  virtual ~TaskbarHost();

  // disable copy
  TaskbarHost(const TaskbarHost&) = delete;
  TaskbarHost& operator=(const TaskbarHost&) = delete;

  // Add or update the buttons in thumbar.
  bool SetThumbarButtons(HWND window,
                         const std::vector<ThumbarButton>& buttons);

  void RestoreThumbarButtons(HWND window);

  // Set the progress state in taskbar.
  bool SetProgressBar(HWND window,
                      double value,
                      const NativeWindow::ProgressState state);

  // Set the overlay icon in taskbar.
  bool SetOverlayIcon(HWND window,
                      const SkBitmap& bitmap,
                      const std::string& text);

  // Set the region of the window to show as a thumbnail in taskbar.
  bool SetThumbnailClip(HWND window, const gfx::Rect& region);

  // Set the tooltip for the thumbnail in taskbar.
  bool SetThumbnailToolTip(HWND window, const std::string& tooltip);

  // Called by the window that there is a button in thumbar clicked.
  bool HandleThumbarButtonEvent(int button_id);

  void SetThumbarButtonsAdded(bool added) { thumbar_buttons_added_ = added; }

 private:
  // Initialize the taskbar object.
  bool InitializeTaskbar();

  using CallbackMap = std::map<int, base::RepeatingClosure>;
  CallbackMap callback_map_;

  std::vector<ThumbarButton> last_buttons_;

  // The COM object of taskbar.
  Microsoft::WRL::ComPtr<ITaskbarList3> taskbar_;

  // Whether we have already added the buttons to thumbar.
  bool thumbar_buttons_added_ = false;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_WIN_TASKBAR_HOST_H_
