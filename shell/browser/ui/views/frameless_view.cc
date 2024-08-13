// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/views/frameless_view.h"

#include "shell/browser/native_window_views.h"
#include "ui/base/hit_test.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace electron {

namespace {

const int kResizeInsideBoundsSize = 5;
const int kResizeAreaCornerSize = 16;

}  // namespace

FramelessView::FramelessView() = default;

FramelessView::~FramelessView() = default;

void FramelessView::Init(NativeWindowViews* window, views::Widget* frame) {
  window_ = window;
  frame_ = frame;
}

int FramelessView::ResizingBorderHitTest(const gfx::Point& point) {
  return ResizingBorderHitTestImpl(point, gfx::Insets(kResizeInsideBoundsSize));
}

int FramelessView::ResizingBorderHitTestImpl(const gfx::Point& point,
                                             const gfx::Insets& resize_border) {
  // to be used for resize handles.
  bool can_ever_resize = frame_->widget_delegate()
                             ? frame_->widget_delegate()->CanResize()
                             : false;

  // https://github.com/electron/electron/issues/611
  // If window isn't resizable, we should always return HTNOWHERE, otherwise the
  // hover state of DOM will not be cleared probably.
  if (!can_ever_resize)
    return HTNOWHERE;

  // Don't allow overlapping resize handles when the window is maximized or
  // fullscreen, as it can't be resized in those states.
  bool allow_overlapping_handles =
      !frame_->IsMaximized() && !frame_->IsFullscreen();
  return GetHTComponentForFrame(
      point, allow_overlapping_handles ? resize_border : gfx::Insets(),
      kResizeAreaCornerSize, kResizeAreaCornerSize, can_ever_resize);
}

gfx::Rect FramelessView::GetBoundsForClientView() const {
  return bounds();
}

gfx::Rect FramelessView::GetWindowBoundsForClientBounds(
    const gfx::Rect& client_bounds) const {
  gfx::Rect window_bounds = client_bounds;
  // Enforce minimum size (1, 1) in case that client_bounds is passed with
  // empty size. This could occur when the frameless window is being
  // initialized.
  if (window_bounds.IsEmpty()) {
    window_bounds.set_width(1);
    window_bounds.set_height(1);
  }
  return window_bounds;
}

int FramelessView::NonClientHitTest(const gfx::Point& point) {
  if (frame_->IsFullscreen())
    return HTCLIENT;

  int contents_hit_test = window_->NonClientHitTest(point);
  if (contents_hit_test != HTNOWHERE)
    return contents_hit_test;

  // Support resizing frameless window by dragging the border.
  int frame_component = ResizingBorderHitTest(point);
  if (frame_component != HTNOWHERE)
    return frame_component;

  return HTCLIENT;
}

views::View* FramelessView::TargetForRect(views::View* root,
                                          const gfx::Rect& rect) {
  CHECK_EQ(root, this);

  if (NonClientHitTest(rect.origin()) != HTCLIENT)
    return this;

  return NonClientFrameView::TargetForRect(root, rect);
}

gfx::Size FramelessView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return frame_->non_client_view()
      ->GetWindowBoundsForClientBounds(gfx::Rect(
          frame_->client_view()->CalculatePreferredSize(available_size)))
      .size();
}

gfx::Size FramelessView::GetMinimumSize() const {
  return window_->GetContentMinimumSize();
}

gfx::Size FramelessView::GetMaximumSize() const {
  gfx::Size size = window_->GetContentMaximumSize();
  // Electron public APIs returns (0, 0) when maximum size is not set, but it
  // would break internal window APIs like HWNDMessageHandler::SetAspectRatio.
  return size.IsEmpty() ? gfx::Size(INT_MAX, INT_MAX) : size;
}

BEGIN_METADATA(FramelessView)
END_METADATA

}  // namespace electron
