// Copyright (c) 2021 Ryan Gonzalez.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_UI_VIEWS_CLIENT_FRAME_VIEW_LINUX_H_
#define ELECTRON_SHELL_BROWSER_UI_VIEWS_CLIENT_FRAME_VIEW_LINUX_H_

#include <array>
#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/scoped_observation.h"
#include "shell/browser/ui/views/frameless_view.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/ui_base_types.h"
#include "ui/linux/linux_ui.h"
#include "ui/linux/nav_button_provider.h"
#include "ui/linux/window_button_order_observer.h"
#include "ui/linux/window_frame_provider.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/frame_buttons.h"

namespace electron {

class ClientFrameViewLinux : public FramelessView,
                             private ui::NativeThemeObserver,
                             private ui::WindowButtonOrderObserver {
  METADATA_HEADER(ClientFrameViewLinux, FramelessView)

 public:
  ClientFrameViewLinux();
  ~ClientFrameViewLinux() override;

  void Init(NativeWindowViews* window, views::Widget* frame) override;

  // These are here for ElectronDesktopWindowTreeHostLinux to use.
  gfx::Insets GetBorderDecorationInsets() const;
  gfx::Insets GetInputInsets() const;
  gfx::Rect GetWindowContentBounds() const;
  SkRRect GetRoundedWindowContentBounds() const;
  int GetTranslucentTopAreaHeight() const;
  // Returns which edges of the frame are tiled.
  const ui::WindowTiledEdges& tiled_edges() const { return tiled_edges_; }
  void set_tiled_edges(ui::WindowTiledEdges tiled_edges) {
    tiled_edges_ = tiled_edges;
  }

 protected:
  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

  // views::WindowButtonOrderObserver:
  void OnWindowButtonOrderingChange() override;

  // Overridden from FramelessView:
  int ResizingBorderHitTest(const gfx::Point& point) override;

  // Overridden from views::NonClientFrameView:
  gfx::Rect GetBoundsForClientView() const override;
  gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& client_bounds) const override;
  int NonClientHitTest(const gfx::Point& point) override;
  void GetWindowMask(const gfx::Size& size, SkPath* window_mask) override;
  void UpdateWindowTitle() override;
  void SizeConstraintsChanged() override;

  // Overridden from View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  gfx::Size GetMinimumSize() const override;
  gfx::Size GetMaximumSize() const override;
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;

  // Overriden from views::ViewTargeterDelegate
  views::View* TargetForRect(views::View* root, const gfx::Rect& rect) override;

 private:
  static constexpr int kNavButtonCount = 4;

  struct NavButton {
    ui::NavButtonProvider::FrameButtonDisplayType type;
    views::FrameButton frame_button;
    void (views::Widget::*callback)();
    int accessibility_id;
    int hit_test_id;
    RAW_PTR_EXCLUSION views::ImageButton* button{nullptr};
  };

  struct ThemeValues {
    float window_border_radius;

    int titlebar_min_height;
    gfx::Insets titlebar_padding;

    SkColor title_color;
    gfx::Insets title_padding;

    int button_min_size;
    gfx::Insets button_padding;
  };

  void PaintAsActiveChanged();

  void UpdateThemeValues();

  enum class ButtonSide { kLeading, kTrailing };

  ui::NavButtonProvider::FrameButtonDisplayType GetButtonTypeToSkip() const;
  void UpdateButtonImages();
  void LayoutButtons();
  void LayoutButtonsOnSide(ButtonSide side,
                           gfx::Rect* remaining_content_bounds);

  gfx::Rect GetTitlebarBounds() const;
  gfx::Insets GetTitlebarContentInsets() const;
  gfx::Rect GetTitlebarContentBounds() const;

  gfx::Size SizeWithDecorations(gfx::Size size) const;

  raw_ptr<ui::NativeTheme> theme_;
  ThemeValues theme_values_;

  RAW_PTR_EXCLUSION views::Label* title_;

  std::unique_ptr<ui::NavButtonProvider> nav_button_provider_;
  std::array<NavButton, kNavButtonCount> nav_buttons_;

  std::vector<views::FrameButton> leading_frame_buttons_;
  std::vector<views::FrameButton> trailing_frame_buttons_;

  bool host_supports_client_frame_shadow_ = false;

  raw_ptr<ui::WindowFrameProvider> frame_provider_;

  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observer_{this};
  base::ScopedObservation<ui::LinuxUi, ui::WindowButtonOrderObserver>
      window_button_order_observer_{this};

  base::CallbackListSubscription paint_as_active_changed_subscription_;

  ui::WindowTiledEdges tiled_edges_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_VIEWS_CLIENT_FRAME_VIEW_LINUX_H_
