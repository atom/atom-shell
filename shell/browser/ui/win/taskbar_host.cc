// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/win/taskbar_host.h"

#include <objbase.h>
#include <string>

#include "base/stl_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/scoped_gdi_object.h"
#include "shell/browser/native_window.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/display/win/screen_win.h"
#include "ui/gfx/icon_util.h"

namespace electron {

namespace {

// From MSDN:
// https://msdn.microsoft.com/en-us/library/windows/desktop/dd378460(v=vs.85).aspx#thumbbars
// The thumbnail toolbar has a maximum of seven buttons due to the limited room.
const size_t kMaxButtonsCount = 7;

// The base id of Thumbar button.
const int kButtonIdBase = 40001;

bool GetThumbarButtonFlags(const std::vector<std::string>& flags,
                           THUMBBUTTONFLAGS* out) {
  THUMBBUTTONFLAGS result = THBF_ENABLED;  // THBF_ENABLED == 0
  for (const auto& flag : flags) {
    if (flag == "disabled")
      result |= THBF_DISABLED;
    else if (flag == "dismissonclick")
      result |= THBF_DISMISSONCLICK;
    else if (flag == "nobackground")
      result |= THBF_NOBACKGROUND;
    else if (flag == "hidden")
      result |= THBF_HIDDEN;
    else if (flag == "noninteractive")
      result |= THBF_NONINTERACTIVE;
    else
      return false;
  }
  *out = result;
  return true;
}

}  // namespace

TaskbarHost::ThumbarButton::ThumbarButton() = default;
TaskbarHost::ThumbarButton::ThumbarButton(const TaskbarHost::ThumbarButton&) =
    default;
TaskbarHost::ThumbarButton::~ThumbarButton() = default;

TaskbarHost::TaskbarHost() = default;

TaskbarHost::~TaskbarHost() = default;

bool TaskbarHost::SetThumbarButtons(HWND window,
                                    const std::vector<ThumbarButton>& buttons) {
  if (buttons.size() > kMaxButtonsCount || !InitializeTaskbar())
    return false;

  callback_map_.clear();

  // The number of buttons in thumbar can not be changed once it is created,
  // so we have to claim kMaxButtonsCount buttons initially in case users add
  // more buttons later.
  base::win::ScopedHICON icons[kMaxButtonsCount] = {};
  THUMBBUTTON thumb_buttons[kMaxButtonsCount] = {};

  for (size_t i = 0; i < kMaxButtonsCount; ++i) {
    THUMBBUTTON& thumb_button = thumb_buttons[i];

    // Set ID.
    thumb_button.iId = kButtonIdBase + i;
    thumb_button.dwMask = THB_FLAGS;

    if (i >= buttons.size()) {
      // This button is used to occupy the place in toolbar, and it does not
      // show.
      thumb_button.dwFlags = THBF_HIDDEN;
      continue;
    }

    // This button is user's button.
    const ThumbarButton& button = buttons[i];

    // Generate flags.
    thumb_button.dwFlags = THBF_ENABLED;
    if (!GetThumbarButtonFlags(button.flags, &thumb_button.dwFlags))
      return false;

    // Set icon.
    if (!button.icon.IsEmpty()) {
      thumb_button.dwMask |= THB_ICON;
      icons[i] = IconUtil::CreateHICONFromSkBitmap(button.icon.AsBitmap());
      thumb_button.hIcon = icons[i].get();
    }

    // Set tooltip.
    if (!button.tooltip.empty()) {
      thumb_button.dwMask |= THB_TOOLTIP;
      wcsncpy_s(thumb_button.szTip, base::UTF8ToWide(button.tooltip).c_str(),
                _TRUNCATE);
    }

    // Save callback.
    callback_map_[thumb_button.iId] = button.clicked_callback;
  }

  // Finally add them to taskbar.
  HRESULT r;
  if (thumbar_buttons_added_) {
    r = taskbar_->ThumbBarUpdateButtons(window, kMaxButtonsCount,
                                        thumb_buttons);
  } else {
    r = taskbar_->ThumbBarAddButtons(window, kMaxButtonsCount, thumb_buttons);
  }

  thumbar_buttons_added_ = true;
  last_buttons_ = buttons;
  return SUCCEEDED(r);
}

void TaskbarHost::RestoreThumbarButtons(HWND window) {
  if (thumbar_buttons_added_) {
    thumbar_buttons_added_ = false;
    SetThumbarButtons(window, last_buttons_);
  }
}

bool TaskbarHost::SetProgressBar(HWND window,
                                 double value,
                                 const NativeWindow::ProgressState state) {
  if (!InitializeTaskbar())
    return false;

  bool success;
  if (value > 1.0 || state == NativeWindow::ProgressState::kIndeterminate) {
    success = SUCCEEDED(taskbar_->SetProgressState(window, TBPF_INDETERMINATE));
  } else if (value < 0 || state == NativeWindow::ProgressState::kNone) {
    success = SUCCEEDED(taskbar_->SetProgressState(window, TBPF_NOPROGRESS));
  } else {
    // Unless SetProgressState set a blocking state (TBPF_ERROR, TBPF_PAUSED)
    // for the window, a call to SetProgressValue assumes the TBPF_NORMAL
    // state even if it is not explicitly set.
    // SetProgressValue overrides and clears the TBPF_INDETERMINATE state.
    if (state == NativeWindow::ProgressState::kError) {
      success = SUCCEEDED(taskbar_->SetProgressState(window, TBPF_ERROR));
    } else if (state == NativeWindow::ProgressState::kPaused) {
      success = SUCCEEDED(taskbar_->SetProgressState(window, TBPF_PAUSED));
    } else {
      success = SUCCEEDED(taskbar_->SetProgressState(window, TBPF_NORMAL));
    }

    if (success) {
      int val = static_cast<int>(value * 100);
      success = SUCCEEDED(taskbar_->SetProgressValue(window, val, 100));
    }
  }

  return success;
}

// Adapted from SetOverlayIcon in
// chrome/browser/taskbar/taskbar_decorator_win.cc.
bool TaskbarHost::SetOverlayIcon(HWND window,
                                 const SkBitmap& bitmap,
                                 const std::string& text) {
  if (!InitializeTaskbar())
    return false;

  base::win::ScopedGDIObject<HICON> icon;
  if (!bitmap.isNull()) {
    DCHECK_GE(bitmap.width(), bitmap.height());

    constexpr int kOverlayIconSize = 16;

    // Maintain aspect ratio on resize, but prefer more square.
    // (We used to round down here, but rounding up produces nicer results.)
    const int resized_height =
        base::ClampCeil(kOverlayIconSize *
                        (static_cast<float>(bitmap.height()) / bitmap.width()));

    DCHECK_GE(kOverlayIconSize, resized_height);
    // Since the target size is so small, we use our best resizer.
    SkBitmap sk_icon = skia::ImageOperations::Resize(
        bitmap, skia::ImageOperations::RESIZE_LANCZOS3, kOverlayIconSize,
        resized_height);

    // Paint the resized icon onto a 16x16 canvas otherwise Windows will badly
    // hammer it to 16x16. We'll use a circular clip to be consistent with the
    // way profile icons are rendered in the profile switcher.
    SkBitmap offscreen_bitmap;
    offscreen_bitmap.allocN32Pixels(kOverlayIconSize, kOverlayIconSize);
    SkCanvas offscreen_canvas(offscreen_bitmap, SkSurfaceProps{});
    offscreen_canvas.clear(SK_ColorTRANSPARENT);

    static const SkRRect overlay_icon_clip =
        SkRRect::MakeOval(SkRect::MakeWH(kOverlayIconSize, kOverlayIconSize));
    offscreen_canvas.clipRRect(overlay_icon_clip, true);

    // Note: the original code used kOverlayIconSize - resized_height, but in
    // order to center the icon in the circle clip area, we're going to center
    // it in the paintable region instead, rounding up to the closest pixel to
    // avoid smearing.
    const int y_offset = std::ceilf((kOverlayIconSize - resized_height) / 2.0f);
    offscreen_canvas.drawImage(sk_icon.asImage(), 0, y_offset);

    icon = IconUtil::CreateHICONFromSkBitmap(offscreen_bitmap);
    if (!icon.is_valid())
      return false;
  }

  return SUCCEEDED(taskbar_->SetOverlayIcon(window, icon.get(),
                                            base::UTF8ToWide(text).c_str()));
}

bool TaskbarHost::SetThumbnailClip(HWND window, const gfx::Rect& region) {
  if (!InitializeTaskbar())
    return false;

  if (region.IsEmpty()) {
    return SUCCEEDED(taskbar_->SetThumbnailClip(window, nullptr));
  } else {
    RECT rect =
        display::win::ScreenWin::DIPToScreenRect(window, region).ToRECT();
    return SUCCEEDED(taskbar_->SetThumbnailClip(window, &rect));
  }
}

bool TaskbarHost::SetThumbnailToolTip(HWND window, const std::string& tooltip) {
  if (!InitializeTaskbar())
    return false;

  return SUCCEEDED(
      taskbar_->SetThumbnailTooltip(window, base::UTF8ToWide(tooltip).c_str()));
}

bool TaskbarHost::HandleThumbarButtonEvent(int button_id) {
  const auto iter = callback_map_.find(button_id);
  if (iter != std::end(callback_map_)) {
    auto callback = iter->second;
    if (!callback.is_null())
      callback.Run();
    return true;
  }
  return false;
}

bool TaskbarHost::InitializeTaskbar() {
  if (taskbar_)
    return true;

  if (FAILED(::CoCreateInstance(CLSID_TaskbarList, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&taskbar_))) ||
      FAILED(taskbar_->HrInit())) {
    taskbar_.Reset();
    return false;
  } else {
    return true;
  }
}

}  // namespace electron
