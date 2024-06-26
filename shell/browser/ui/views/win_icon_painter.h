// Copyright (c) 2022 Microsoft, Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_UI_VIEWS_WIN_ICON_PAINTER_H_
#define ELECTRON_SHELL_BROWSER_UI_VIEWS_WIN_ICON_PAINTER_H_

#include "base/memory/raw_ptr.h"
#include "ui/gfx/canvas.h"

namespace electron {

// Copied from chrome/browser/ui/views/frame/windows_icon_painter.h
class WinIconPainter {
 public:
  WinIconPainter();
  virtual ~WinIconPainter();

  WinIconPainter(const WinIconPainter&) = delete;
  WinIconPainter& operator=(const WinIconPainter&) = delete;

 public:
  // Paints the minimize icon for the button
  virtual void PaintMinimizeIcon(gfx::Canvas* canvas,
                                 const gfx::Rect& symbol_rect,
                                 cc::PaintFlags& flags);

  // Paints the maximize icon for the button
  virtual void PaintMaximizeIcon(gfx::Canvas* canvas,
                                 const gfx::Rect& symbol_rect,
                                 cc::PaintFlags& flags);

  // Paints the restore icon for the button
  virtual void PaintRestoreIcon(gfx::Canvas* canvas,
                                const gfx::Rect& symbol_rect,
                                cc::PaintFlags& flags);

  // Paints the close icon for the button
  virtual void PaintCloseIcon(gfx::Canvas* canvas,
                              const gfx::Rect& symbol_rect,
                              cc::PaintFlags& flags);
};

class Win11IconPainter : public WinIconPainter {
 public:
  Win11IconPainter();
  ~Win11IconPainter() override;

  Win11IconPainter(const Win11IconPainter&) = delete;
  Win11IconPainter& operator=(const Win11IconPainter&) = delete;

 public:
  // Paints the maximize icon for the button
  void PaintMaximizeIcon(gfx::Canvas* canvas,
                         const gfx::Rect& symbol_rect,
                         cc::PaintFlags& flags) override;

  // Paints the restore icon for the button
  void PaintRestoreIcon(gfx::Canvas* canvas,
                        const gfx::Rect& symbol_rect,
                        cc::PaintFlags& flags) override;
};
}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_VIEWS_WIN_ICON_PAINTER_H_
