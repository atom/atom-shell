// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Modified from chrome/browser/ui/views/frame/windows_10_caption_button.h

#ifndef ELECTRON_SHELL_BROWSER_UI_VIEWS_WIN_CAPTION_BUTTON_H_
#define ELECTRON_SHELL_BROWSER_UI_VIEWS_WIN_CAPTION_BUTTON_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/frame/window_frame_util.h"
#include "chrome/browser/ui/view_ids.h"
#include "shell/browser/ui/views/win_icon_painter.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/views/controls/button/button.h"

namespace electron {

class WinFrameView;

class WinCaptionButton : public views::Button {
  METADATA_HEADER(WinCaptionButton, views::Button)

 public:
  WinCaptionButton(PressedCallback callback,
                   WinFrameView* frame_view,
                   ViewID button_type,
                   const std::u16string& accessible_name);
  ~WinCaptionButton() override;

  WinCaptionButton(const WinCaptionButton&) = delete;
  WinCaptionButton& operator=(const WinCaptionButton&) = delete;

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void PaintButtonContents(gfx::Canvas* canvas) override;

  gfx::Size GetSize() const;
  void SetSize(gfx::Size size);

 private:
  std::unique_ptr<WinIconPainter> CreateIconPainter();
  // Returns the amount we should visually reserve on the left (right in RTL)
  // for spacing between buttons. We do this instead of repositioning the
  // buttons to avoid the sliver of deadspace that would result.
  int GetBetweenButtonSpacing() const;

  // Returns the order in which this button will be displayed (with 0 being
  // drawn farthest to the left, and larger indices being drawn to the right of
  // smaller indices).
  int GetButtonDisplayOrderIndex() const;

  // The base color to use for the button symbols and background blending. Uses
  // the more readable of black and white.
  SkColor GetBaseColor() const;

  // Paints the minimize/maximize/restore/close icon for the button.
  void PaintSymbol(gfx::Canvas* canvas);

  raw_ptr<WinFrameView> frame_view_;
  std::unique_ptr<WinIconPainter> icon_painter_;
  ViewID button_type_;

  int base_width_ = WindowFrameUtil::kWindowsCaptionButtonWidth;
  int height_ = WindowFrameUtil::kWindowsCaptionButtonHeightRestored;
};
}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_VIEWS_WIN_CAPTION_BUTTON_H_
