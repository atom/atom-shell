// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_UI_VIEWS_NATIVE_FRAME_VIEW_H_
#define ELECTRON_SHELL_BROWSER_UI_VIEWS_NATIVE_FRAME_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/window/native_frame_view.h"

namespace electron {

class NativeWindow;

// Like the views::NativeFrameView, but returns the min/max size from the
// NativeWindowViews.
class NativeFrameView : public views::NativeFrameView {
  METADATA_HEADER(NativeFrameView, views::NativeFrameView)

 public:
  NativeFrameView(NativeWindow* window, views::Widget* widget);

  // disable copy
  NativeFrameView(const NativeFrameView&) = delete;
  NativeFrameView& operator=(const NativeFrameView&) = delete;

 protected:
  // views::View:
  gfx::Size GetMinimumSize() const override;
  gfx::Size GetMaximumSize() const override;

 private:
  raw_ptr<NativeWindow> window_;  // weak ref.
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_VIEWS_NATIVE_FRAME_VIEW_H_
