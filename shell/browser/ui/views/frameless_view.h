// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_UI_VIEWS_FRAMELESS_VIEW_H_
#define ELECTRON_SHELL_BROWSER_UI_VIEWS_FRAMELESS_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/window/non_client_view.h"

namespace views {
class Widget;
}

namespace electron {

class NativeWindowViews;

class FramelessView : public views::NonClientFrameView {
  METADATA_HEADER(FramelessView, views::NonClientFrameView)

 public:
  FramelessView();
  ~FramelessView() override;

  // disable copy
  FramelessView(const FramelessView&) = delete;
  FramelessView& operator=(const FramelessView&) = delete;

  virtual void Init(NativeWindowViews* window, views::Widget* frame);

  // Returns whether the |point| is on frameless window's resizing border.
  virtual int ResizingBorderHitTest(const gfx::Point& point);

 protected:
  // Helper function for subclasses to implement ResizingBorderHitTest with a
  // custom resize inset.
  int ResizingBorderHitTestImpl(const gfx::Point& point,
                                const gfx::Insets& resize_border);

  // views::NonClientFrameView:
  gfx::Rect GetBoundsForClientView() const override;
  gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& client_bounds) const override;
  int NonClientHitTest(const gfx::Point& point) override;
  void GetWindowMask(const gfx::Size& size, SkPath* window_mask) override;
  void ResetWindowControls() override;
  void UpdateWindowIcon() override;
  void UpdateWindowTitle() override;
  void SizeConstraintsChanged() override;

  // views::ViewTargeterDelegate:
  views::View* TargetForRect(views::View* root, const gfx::Rect& rect) override;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  gfx::Size GetMinimumSize() const override;
  gfx::Size GetMaximumSize() const override;

  // Not owned.
  raw_ptr<NativeWindowViews> window_ = nullptr;
  raw_ptr<views::Widget> frame_ = nullptr;

  friend class NativeWindowsViews;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_VIEWS_FRAMELESS_VIEW_H_
