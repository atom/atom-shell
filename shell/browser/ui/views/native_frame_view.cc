// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/views/native_frame_view.h"

#include "shell/browser/native_window.h"
#include "ui/base/metadata/metadata_impl_macros.h"

namespace electron {

NativeFrameView::NativeFrameView(NativeWindow* window, views::Widget* widget)
    : views::NativeFrameView(widget), window_(window) {}

gfx::Size NativeFrameView::GetMinimumSize() const {
  return window_->GetMinimumSize();
}

gfx::Size NativeFrameView::GetMaximumSize() const {
  gfx::Size size = window_->GetMaximumSize();
  // Electron public APIs returns (0, 0) when maximum size is not set, but it
  // would break internal window APIs like HWNDMessageHandler::SetAspectRatio.
  return size.IsEmpty() ? gfx::Size(INT_MAX, INT_MAX) : size;
}

BEGIN_METADATA(NativeFrameView)
END_METADATA

}  // namespace electron
