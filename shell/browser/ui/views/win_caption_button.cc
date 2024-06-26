// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Modified from chrome/browser/ui/views/frame/windows_caption_button.cc

#include "shell/browser/ui/views/win_caption_button.h"

#include <utility>

#include "base/i18n/rtl.h"
#include "base/numerics/safe_conversions.h"
#include "base/win/windows_version.h"
#include "chrome/grit/theme_resources.h"
#include "shell/browser/ui/views/win_frame_view.h"
#include "shell/common/color_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/theme_provider.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/scoped_canvas.h"

namespace electron {

WinCaptionButton::WinCaptionButton(PressedCallback callback,
                                   WinFrameView* frame_view,
                                   ViewID button_type,
                                   const std::u16string& accessible_name)
    : views::Button(std::move(callback)),
      frame_view_(frame_view),
      icon_painter_(CreateIconPainter()),
      button_type_(button_type) {
  SetAnimateOnStateChange(true);
  // Not focusable by default, only for accessibility.
  SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  SetAccessibleName(accessible_name);
}

WinCaptionButton::~WinCaptionButton() = default;

std::unique_ptr<WinIconPainter> WinCaptionButton::CreateIconPainter() {
  if (base::win::GetVersion() >= base::win::Version::WIN11) {
    return std::make_unique<Win11IconPainter>();
  }
  return std::make_unique<WinIconPainter>();
}

gfx::Size WinCaptionButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  // TODO(bsep): The sizes in this function are for 1x device scale and don't
  // match Windows button sizes at hidpi.

  return gfx::Size(base_width_ + GetBetweenButtonSpacing(), height_);
}

void WinCaptionButton::OnPaintBackground(gfx::Canvas* canvas) {
  // Paint the background of the button (the semi-transparent rectangle that
  // appears when you hover or press the button).

  const SkColor bg_color = frame_view_->window()->overlay_button_color();
  const SkAlpha theme_alpha = SkColorGetA(bg_color);

  gfx::Rect bounds = GetContentsBounds();
  bounds.Inset(gfx::Insets::TLBR(0, GetBetweenButtonSpacing(), 0, 0));

  if (theme_alpha > 0)
    canvas->FillRect(bounds, SkColorSetA(bg_color, theme_alpha));

  SkColor base_color;
  SkAlpha hovered_alpha, pressed_alpha;
  if (button_type_ == VIEW_ID_CLOSE_BUTTON) {
    base_color = SkColorSetRGB(0xE8, 0x11, 0x23);
    hovered_alpha = SK_AlphaOPAQUE;
    pressed_alpha = 0x98;
  } else {
    // Match the native buttons.
    base_color = frame_view_->GetReadableFeatureColor(bg_color);
    hovered_alpha = 0x1A;
    pressed_alpha = 0x33;

    if (theme_alpha > 0) {
      // Theme buttons have slightly increased opacity to make them stand out
      // against a visually-busy frame image.
      constexpr float kAlphaScale = 1.3f;
      hovered_alpha = base::ClampRound<SkAlpha>(hovered_alpha * kAlphaScale);
      pressed_alpha = base::ClampRound<SkAlpha>(pressed_alpha * kAlphaScale);
    }
  }

  SkAlpha alpha;
  if (GetState() == STATE_PRESSED)
    alpha = pressed_alpha;
  else
    alpha = gfx::Tween::IntValueBetween(hover_animation().GetCurrentValue(),
                                        SK_AlphaTRANSPARENT, hovered_alpha);
  canvas->FillRect(bounds, SkColorSetA(base_color, alpha));
}

void WinCaptionButton::PaintButtonContents(gfx::Canvas* canvas) {
  PaintSymbol(canvas);
}

gfx::Size WinCaptionButton::GetSize() const {
  return gfx::Size(base_width_, height_);
}

void WinCaptionButton::SetSize(gfx::Size size) {
  int width = size.width();
  int height = size.height();

  if (width > 0)
    base_width_ = width;
  if (height > 0)
    height_ = height;

  InvalidateLayout();
}

int WinCaptionButton::GetBetweenButtonSpacing() const {
  const int display_order_index = GetButtonDisplayOrderIndex();
  return display_order_index == 0
             ? 0
             : WindowFrameUtil::kWindowsCaptionButtonVisualSpacing;
}

int WinCaptionButton::GetButtonDisplayOrderIndex() const {
  int button_display_order = 0;
  switch (button_type_) {
    case VIEW_ID_MINIMIZE_BUTTON:
      button_display_order = 0;
      break;
    case VIEW_ID_MAXIMIZE_BUTTON:
    case VIEW_ID_RESTORE_BUTTON:
      button_display_order = 1;
      break;
    case VIEW_ID_CLOSE_BUTTON:
      button_display_order = 2;
      break;
    default:
      NOTREACHED();
  }

  // Reverse the ordering if we're in RTL mode
  if (base::i18n::IsRTL())
    button_display_order = 2 - button_display_order;

  return button_display_order;
}

void WinCaptionButton::PaintSymbol(gfx::Canvas* canvas) {
  SkColor symbol_color = frame_view_->window()->overlay_symbol_color();

  if (button_type_ == VIEW_ID_CLOSE_BUTTON &&
      hover_animation().is_animating()) {
    symbol_color = gfx::Tween::ColorValueBetween(
        hover_animation().GetCurrentValue(), symbol_color, SK_ColorWHITE);
  } else if (button_type_ == VIEW_ID_CLOSE_BUTTON &&
             (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED)) {
    symbol_color = SK_ColorWHITE;
  }

  gfx::ScopedCanvas scoped_canvas(canvas);
  const float scale = canvas->UndoDeviceScaleFactor();

  const int symbol_size_pixels = base::ClampRound(10 * scale);
  gfx::RectF bounds_rect(GetContentsBounds());
  bounds_rect.Scale(scale);
  gfx::Rect symbol_rect(gfx::ToEnclosingRect(bounds_rect));
  symbol_rect.ClampToCenteredSize(
      gfx::Size(symbol_size_pixels, symbol_size_pixels));

  cc::PaintFlags flags;
  flags.setAntiAlias(false);
  flags.setColor(symbol_color);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  const int stroke_width = base::ClampRound(scale);
  flags.setStrokeWidth(stroke_width);

  switch (button_type_) {
    case VIEW_ID_MINIMIZE_BUTTON: {
      icon_painter_->PaintMinimizeIcon(canvas, symbol_rect, flags);
      return;
    }

    case VIEW_ID_MAXIMIZE_BUTTON: {
      icon_painter_->PaintMaximizeIcon(canvas, symbol_rect, flags);
      return;
    }

    case VIEW_ID_RESTORE_BUTTON: {
      icon_painter_->PaintRestoreIcon(canvas, symbol_rect, flags);
      return;
    }

    case VIEW_ID_CLOSE_BUTTON: {
      // The close button's X is surrounded by a "halo" of transparent pixels.
      // When the X is white, the transparent pixels need to be a bit brighter
      // to be visible.
      const float stroke_halo =
          stroke_width * (symbol_color == SK_ColorWHITE ? 0.1f : 0.05f);
      flags.setStrokeWidth(stroke_width + stroke_halo);

      icon_painter_->PaintCloseIcon(canvas, symbol_rect, flags);
      return;
    }

    default:
      NOTREACHED();
  }
}

BEGIN_METADATA(WinCaptionButton)
END_METADATA

}  // namespace electron
