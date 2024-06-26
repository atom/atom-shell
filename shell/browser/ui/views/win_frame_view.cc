// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.
//
// Portions of this file are sourced from
// chrome/browser/ui/views/frame/glass_browser_frame_view.cc,
// Copyright (c) 2012 The Chromium Authors,
// which is governed by a BSD-style license

#include "shell/browser/ui/views/win_frame_view.h"

#include <dwmapi.h>
#include <memory>

#include "base/win/windows_version.h"
#include "shell/browser/native_window_views.h"
#include "shell/browser/ui/views/win_caption_button_container.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/win/hwnd_metrics.h"
#include "ui/display/win/dpi.h"
#include "ui/display/win/screen_win.h"
#include "ui/gfx/geometry/dip_util.h"
#include "ui/views/background.h"
#include "ui/views/widget/widget.h"
#include "ui/views/win/hwnd_util.h"

namespace electron {

WinFrameView::WinFrameView() = default;

WinFrameView::~WinFrameView() = default;

void WinFrameView::Init(NativeWindowViews* window, views::Widget* frame) {
  window_ = window;
  frame_ = frame;

  if (window->IsWindowControlsOverlayEnabled()) {
    caption_button_container_ =
        AddChildView(std::make_unique<WinCaptionButtonContainer>(this));
  } else {
    caption_button_container_ = nullptr;
  }
}

SkColor WinFrameView::GetReadableFeatureColor(SkColor background_color) {
  // color_utils::GetColorWithMaxContrast()/IsDark() aren't used here because
  // they switch based on the Chrome light/dark endpoints, while we want to use
  // the system native behavior below.
  const auto windows_luma = [](SkColor c) {
    return 0.25f * SkColorGetR(c) + 0.625f * SkColorGetG(c) +
           0.125f * SkColorGetB(c);
  };
  return windows_luma(background_color) <= 128.0f ? SK_ColorWHITE
                                                  : SK_ColorBLACK;
}

void WinFrameView::InvalidateCaptionButtons() {
  if (!caption_button_container_)
    return;

  caption_button_container_->UpdateBackground();
  caption_button_container_->InvalidateLayout();
  caption_button_container_->SchedulePaint();
}

gfx::Rect WinFrameView::GetWindowBoundsForClientBounds(
    const gfx::Rect& client_bounds) const {
  return views::GetWindowBoundsForClientBounds(
      static_cast<views::View*>(const_cast<WinFrameView*>(this)),
      client_bounds);
}

int WinFrameView::FrameBorderThickness() const {
  return (IsMaximized() || frame()->IsFullscreen())
             ? 0
             : display::win::ScreenWin::GetSystemMetricsInDIP(SM_CXSIZEFRAME);
}

views::View* WinFrameView::TargetForRect(views::View* root,
                                         const gfx::Rect& rect) {
  if (NonClientHitTest(rect.origin()) != HTCLIENT) {
    // Custom system titlebar returns non HTCLIENT value, however event should
    // be handled by the view, not by the system, because there are no system
    // buttons underneath.
    if (!ShouldCustomDrawSystemTitlebar()) {
      return this;
    }
    auto local_point = rect.origin();
    ConvertPointToTarget(parent(), caption_button_container_, &local_point);
    if (!caption_button_container_->HitTestPoint(local_point)) {
      return this;
    }
  }

  return NonClientFrameView::TargetForRect(root, rect);
}

int WinFrameView::NonClientHitTest(const gfx::Point& point) {
  if (window_->has_frame())
    return frame_->client_view()->NonClientHitTest(point);

  if (ShouldCustomDrawSystemTitlebar()) {
    // See if the point is within any of the window controls.
    if (caption_button_container_) {
      gfx::Point local_point = point;

      ConvertPointToTarget(parent(), caption_button_container_, &local_point);
      if (caption_button_container_->HitTestPoint(local_point)) {
        const int hit_test_result =
            caption_button_container_->NonClientHitTest(local_point);
        if (hit_test_result != HTNOWHERE)
          return hit_test_result;
      }
    }

    // On Windows 8+, the caption buttons are almost butted up to the top right
    // corner of the window. This code ensures the mouse isn't set to a size
    // cursor while hovering over the caption buttons, thus giving the incorrect
    // impression that the user can resize the window.
    RECT button_bounds = {0};
    if (SUCCEEDED(DwmGetWindowAttribute(
            views::HWNDForWidget(frame()), DWMWA_CAPTION_BUTTON_BOUNDS,
            &button_bounds, sizeof(button_bounds)))) {
      gfx::RectF button_bounds_in_dips = gfx::ConvertRectToDips(
          gfx::Rect(button_bounds), display::win::GetDPIScale());
      // TODO(crbug.com/1131681): GetMirroredRect() requires an integer rect,
      // but the size in DIPs may not be an integer with a fractional device
      // scale factor. If we want to keep using integers, the choice to use
      // ToFlooredRectDeprecated() seems to be doing the wrong thing given the
      // comment below about insetting 1 DIP instead of 1 physical pixel. We
      // should probably use ToEnclosedRect() and then we could have inset 1
      // physical pixel here.
      gfx::Rect buttons =
          GetMirroredRect(gfx::ToFlooredRectDeprecated(button_bounds_in_dips));

      // There is a small one-pixel strip right above the caption buttons in
      // which the resize border "peeks" through.
      constexpr int kCaptionButtonTopInset = 1;
      // The sizing region at the window edge above the caption buttons is
      // 1 px regardless of scale factor. If we inset by 1 before converting
      // to DIPs, the precision loss might eliminate this region entirely. The
      // best we can do is to inset after conversion. This guarantees we'll
      // show the resize cursor when resizing is possible. The cost of which
      // is also maybe showing it over the portion of the DIP that isn't the
      // outermost pixel.
      buttons.Inset(gfx::Insets::TLBR(0, kCaptionButtonTopInset, 0, 0));
      if (buttons.Contains(point))
        return HTNOWHERE;
    }

    int top_border_thickness = FrameTopBorderThickness(false);
    // At the window corners the resize area is not actually bigger, but the 16
    // pixels at the end of the top and bottom edges trigger diagonal resizing.
    constexpr int kResizeCornerWidth = 16;
    int window_component = GetHTComponentForFrame(
        point, gfx::Insets::TLBR(top_border_thickness, 0, 0, 0),
        top_border_thickness, kResizeCornerWidth - FrameBorderThickness(),
        frame()->widget_delegate()->CanResize());
    if (window_component != HTNOWHERE)
      return window_component;
  }

  // Use the parent class's hittest last
  return FramelessView::NonClientHitTest(point);
}

bool WinFrameView::IsMaximized() const {
  return frame()->IsMaximized();
}

bool WinFrameView::ShouldCustomDrawSystemTitlebar() const {
  return window()->IsWindowControlsOverlayEnabled();
}

void WinFrameView::Layout(PassKey) {
  LayoutCaptionButtons();
  if (window()->IsWindowControlsOverlayEnabled()) {
    LayoutWindowControlsOverlay();
  }
  LayoutSuperclass<NonClientFrameView>(this);
}

int WinFrameView::FrameTopBorderThickness(bool restored) const {
  // Mouse and touch locations are floored but GetSystemMetricsInDIP is rounded,
  // so we need to floor instead or else the difference will cause the hittest
  // to fail when it ought to succeed.
  return std::floor(
      FrameTopBorderThicknessPx(restored) /
      display::win::ScreenWin::GetScaleFactorForHWND(HWNDForView(this)));
}

int WinFrameView::FrameTopBorderThicknessPx(bool restored) const {
  // Distinct from FrameBorderThickness() because we can't inset the top
  // border, otherwise Windows will give us a standard titlebar.
  // For maximized windows this is not true, and the top border must be
  // inset in order to avoid overlapping the monitor above.

  // See comments in BrowserDesktopWindowTreeHostWin::GetClientAreaInsets().
  const bool needs_no_border =
      (ShouldCustomDrawSystemTitlebar() && frame()->IsMaximized()) ||
      frame()->IsFullscreen();
  if (needs_no_border && !restored)
    return 0;

  // Note that this method assumes an equal resize handle thickness on all
  // sides of the window.
  // TODO(dfried): Consider having it return a gfx::Insets object instead.
  return ui::GetFrameThickness(
      MonitorFromWindow(HWNDForView(this), MONITOR_DEFAULTTONEAREST));
}

int WinFrameView::TitlebarMaximizedVisualHeight() const {
  int maximized_height =
      display::win::ScreenWin::GetSystemMetricsInDIP(SM_CYCAPTION);
  return maximized_height;
}

// NOTE(@mlaurencin): Usage of IsWebUITabStrip simplified out from Chromium
int WinFrameView::TitlebarHeight(int custom_height) const {
  if (frame()->IsFullscreen() && !IsMaximized())
    return 0;

  int height = TitlebarMaximizedVisualHeight() +
               FrameTopBorderThickness(false) - WindowTopY();
  if (custom_height > TitlebarMaximizedVisualHeight())
    height = custom_height - WindowTopY();

  return height;
}

// NOTE(@mlaurencin): Usage of IsWebUITabStrip simplified out from Chromium
int WinFrameView::WindowTopY() const {
  // The window top is SM_CYSIZEFRAME pixels when maximized (see the comment in
  // FrameTopBorderThickness()) and floor(system dsf) pixels when restored.
  // Unfortunately we can't represent either of those at hidpi without using
  // non-integral dips, so we return the closest reasonable values instead.
  if (IsMaximized())
    return FrameTopBorderThickness(false);

  return 1;
}

void WinFrameView::LayoutCaptionButtons() {
  if (!caption_button_container_)
    return;

  // Non-custom system titlebar already contains caption buttons.
  if (!ShouldCustomDrawSystemTitlebar()) {
    caption_button_container_->SetVisible(false);
    return;
  }

  caption_button_container_->SetVisible(true);
  const gfx::Size preferred_size =
      caption_button_container_->GetPreferredSize();

  int custom_height = window()->titlebar_overlay_height();
  int height = TitlebarHeight(custom_height);

  // TODO(mlaurencin): This -1 creates a 1 pixel margin between the right
  // edge of the button container and the edge of the window, allowing for this
  // edge portion to return the correct hit test and be manually resized
  // properly. Alternatives can be explored, but the differences in view
  // structures between Electron and Chromium may result in this as the best
  // option.
  int variable_width =
      IsMaximized() ? preferred_size.width() : preferred_size.width() - 1;
  caption_button_container_->SetBounds(width() - preferred_size.width(),
                                       WindowTopY(), variable_width, height);

  // Needed for heights larger than default
  caption_button_container_->SetButtonSize(gfx::Size(0, height));
}

void WinFrameView::LayoutWindowControlsOverlay() {
  int overlay_height = window()->titlebar_overlay_height();
  if (overlay_height == 0) {
    // Accounting for the 1 pixel margin at the top of the button container
    overlay_height = IsMaximized()
                         ? caption_button_container_->size().height()
                         : caption_button_container_->size().height() + 1;
  }
  int overlay_width = caption_button_container_->size().width();
  int bounding_rect_width = width() - overlay_width;
  auto bounding_rect =
      GetMirroredRect(gfx::Rect(0, 0, bounding_rect_width, overlay_height));

  window()->SetWindowControlsOverlayRect(bounding_rect);
  window()->NotifyLayoutWindowControlsOverlay();
}

BEGIN_METADATA(WinFrameView)
END_METADATA

}  // namespace electron
