// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_UI_TRAY_ICON_H_
#define ELECTRON_SHELL_BROWSER_UI_TRAY_ICON_H_

#include <string>
#include <vector>

#include "base/observer_list.h"
#include "shell/browser/ui/electron_menu_model.h"
#include "shell/browser/ui/tray_icon_observer.h"
#include "shell/common/gin_converters/guid_converter.h"
#include "ui/gfx/geometry/rect.h"

namespace electron {

class TrayIcon {
 public:
  static TrayIcon* Create(std::optional<UUID> guid);

#if BUILDFLAG(IS_WIN)
  using ImageType = HICON;
#else
  using ImageType = const gfx::Image&;
#endif

  virtual ~TrayIcon();

  // disable copy
  TrayIcon(const TrayIcon&) = delete;
  TrayIcon& operator=(const TrayIcon&) = delete;

  // Sets the image associated with this status icon.
  virtual void SetImage(ImageType image) = 0;

  // Sets the image associated with this status icon when pressed.
  virtual void SetPressedImage(ImageType image) {}

  // Sets the hover text for this status icon. This is also used as the label
  // for the menu item which is created as a replacement for the status icon
  // click action on platforms that do not support custom click actions for the
  // status icon (e.g. Ubuntu Unity).
  virtual void SetToolTip(const std::string& tool_tip) = 0;

#if BUILDFLAG(IS_MAC)
  // Set/Get flag determining whether to ignore double click events.
  virtual void SetIgnoreDoubleClickEvents(bool ignore) = 0;
  virtual bool GetIgnoreDoubleClickEvents() = 0;

  struct TitleOptions {
    std::string font_type;
  };

  // Set/Get title displayed next to status icon in the status bar.
  virtual void SetTitle(const std::string& title,
                        const TitleOptions& options) = 0;
  virtual std::string GetTitle() = 0;
#endif

  enum class IconType { kNone, kInfo, kWarning, kError, kCustom };

  struct BalloonOptions {
    IconType icon_type = IconType::kCustom;
#if BUILDFLAG(IS_WIN)
    HICON icon = nullptr;
#else
    gfx::Image icon;
#endif
    std::u16string title;
    std::u16string content;
    bool large_icon = true;
    bool no_sound = false;
    bool respect_quiet_time = false;

    BalloonOptions();
  };

  // Displays a notification balloon with the specified contents.
  // Depending on the platform it might not appear by the icon tray.
  virtual void DisplayBalloon(const BalloonOptions& options) {}

  // Removes the notification balloon.
  virtual void RemoveBalloon() {}

  // Returns focus to the taskbar notification area.
  virtual void Focus() {}

  // Popups the menu.
  virtual void PopUpContextMenu(const gfx::Point& pos,
                                base::WeakPtr<ElectronMenuModel> menu_model) {}

  virtual void CloseContextMenu() {}

  // Set the context menu for this icon.
  virtual void SetContextMenu(raw_ptr<ElectronMenuModel> menu_model) = 0;

  // Returns the bounds of tray icon.
  virtual gfx::Rect GetBounds();

  void AddObserver(TrayIconObserver* obs) { observers_.AddObserver(obs); }
  void RemoveObserver(TrayIconObserver* obs) { observers_.RemoveObserver(obs); }

  void NotifyClicked(const gfx::Rect& = gfx::Rect(),
                     const gfx::Point& location = gfx::Point(),
                     int modifiers = 0);
  void NotifyDoubleClicked(const gfx::Rect& = gfx::Rect(), int modifiers = 0);
  void NotifyMiddleClicked(const gfx::Rect& = gfx::Rect(), int modifiers = 0);
  void NotifyBalloonShow();
  void NotifyBalloonClicked();
  void NotifyBalloonClosed();
  void NotifyRightClicked(const gfx::Rect& bounds = gfx::Rect(),
                          int modifiers = 0);
  void NotifyDrop();
  void NotifyDropFiles(const std::vector<std::string>& files);
  void NotifyDropText(const std::string& text);
  void NotifyDragEntered();
  void NotifyDragExited();
  void NotifyDragEnded();
  void NotifyMouseUp(const gfx::Point& location = gfx::Point(),
                     int modifiers = 0);
  void NotifyMouseDown(const gfx::Point& location = gfx::Point(),
                       int modifiers = 0);
  void NotifyMouseEntered(const gfx::Point& location = gfx::Point(),
                          int modifiers = 0);
  void NotifyMouseExited(const gfx::Point& location = gfx::Point(),
                         int modifiers = 0);
  void NotifyMouseMoved(const gfx::Point& location = gfx::Point(),
                        int modifiers = 0);

 protected:
  TrayIcon();

 private:
  base::ObserverList<TrayIconObserver> observers_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_TRAY_ICON_H_
