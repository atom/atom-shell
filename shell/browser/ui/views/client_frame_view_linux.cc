// Copyright (c) 2021 Ryan Gonzalez.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/views/client_frame_view_linux.h"

#include <algorithm>

#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_filter.h"
#include "cc/paint/paint_flags.h"
#include "shell/browser/native_window_views.h"
#include "shell/browser/ui/electron_desktop_window_tree_host_linux.h"
#include "shell/browser/ui/views/frameless_view.h"
#include "ui/base/hit_test.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/skia_util.h"
#include "ui/gfx/text_constants.h"
#include "ui/gtk/gtk_compat.h"  // nogncheck
#include "ui/gtk/gtk_util.h"    // nogncheck
#include "ui/linux/linux_ui.h"
#include "ui/linux/nav_button_provider.h"
#include "ui/native_theme/native_theme.h"
#include "ui/strings/grit/ui_strings.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/frame_buttons.h"
#include "ui/views/window/window_button_order_provider.h"

namespace electron {

namespace {

// These values should be the same as Chromium uses.
constexpr int kResizeOutsideBorderSize = 10;
constexpr int kResizeInsideBoundsSize = 5;

ui::NavButtonProvider::ButtonState ButtonStateToNavButtonProviderState(
    views::Button::ButtonState state) {
  switch (state) {
    case views::Button::STATE_NORMAL:
      return ui::NavButtonProvider::ButtonState::kNormal;
    case views::Button::STATE_HOVERED:
      return ui::NavButtonProvider::ButtonState::kHovered;
    case views::Button::STATE_PRESSED:
      return ui::NavButtonProvider::ButtonState::kPressed;
    case views::Button::STATE_DISABLED:
      return ui::NavButtonProvider::ButtonState::kDisabled;

    case views::Button::STATE_COUNT:
    default:
      NOTREACHED();
  }
}

}  // namespace

ClientFrameViewLinux::ClientFrameViewLinux()
    : theme_(ui::NativeTheme::GetInstanceForNativeUi()),
      nav_button_provider_(
          ui::LinuxUiTheme::GetForProfile(nullptr)->CreateNavButtonProvider()),
      nav_buttons_{
          NavButton{ui::NavButtonProvider::FrameButtonDisplayType::kClose,
                    views::FrameButton::kClose, &views::Widget::Close,
                    IDS_APP_ACCNAME_CLOSE, HTCLOSE},
          NavButton{ui::NavButtonProvider::FrameButtonDisplayType::kMaximize,
                    views::FrameButton::kMaximize, &views::Widget::Maximize,
                    IDS_APP_ACCNAME_MAXIMIZE, HTMAXBUTTON},
          NavButton{ui::NavButtonProvider::FrameButtonDisplayType::kRestore,
                    views::FrameButton::kMaximize, &views::Widget::Restore,
                    IDS_APP_ACCNAME_RESTORE, HTMAXBUTTON},
          NavButton{ui::NavButtonProvider::FrameButtonDisplayType::kMinimize,
                    views::FrameButton::kMinimize, &views::Widget::Minimize,
                    IDS_APP_ACCNAME_MINIMIZE, HTMINBUTTON},
      },
      trailing_frame_buttons_{views::FrameButton::kMinimize,
                              views::FrameButton::kMaximize,
                              views::FrameButton::kClose} {
  for (auto& button : nav_buttons_) {
    button.button = new views::ImageButton();
    button.button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    button.button->SetAccessibleName(
        l10n_util::GetStringUTF16(button.accessibility_id));
    AddChildView(button.button);
  }

  title_ = new views::Label();
  title_->SetSubpixelRenderingEnabled(false);
  title_->SetAutoColorReadabilityEnabled(false);
  title_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  title_->SetVerticalAlignment(gfx::ALIGN_MIDDLE);
  title_->SetTextStyle(views::style::STYLE_TAB_ACTIVE);
  AddChildView(title_);

  native_theme_observer_.Observe(theme_);

  if (auto* ui = ui::LinuxUi::instance()) {
    ui->AddWindowButtonOrderObserver(this);
    OnWindowButtonOrderingChange();
  }
}

ClientFrameViewLinux::~ClientFrameViewLinux() {
  if (auto* ui = ui::LinuxUi::instance())
    ui->RemoveWindowButtonOrderObserver(this);
  theme_->RemoveObserver(this);
}

void ClientFrameViewLinux::Init(NativeWindowViews* window,
                                views::Widget* frame) {
  FramelessView::Init(window, frame);

  // Unretained() is safe because the subscription is saved into an instance
  // member and thus will be cancelled upon the instance's destruction.
  paint_as_active_changed_subscription_ =
      frame_->RegisterPaintAsActiveChangedCallback(base::BindRepeating(
          &ClientFrameViewLinux::PaintAsActiveChanged, base::Unretained(this)));

  auto* tree_host = static_cast<ElectronDesktopWindowTreeHostLinux*>(
      ElectronDesktopWindowTreeHostLinux::GetHostForWidget(
          window->GetAcceleratedWidget()));
  host_supports_client_frame_shadow_ = tree_host->SupportsClientFrameShadow();

  bool tiled = tiled_edges().top || tiled_edges().left ||
               tiled_edges().bottom || tiled_edges().right;
  frame_provider_ =
      ui::LinuxUiTheme::GetForProfile(nullptr)->GetWindowFrameProvider(
          !host_supports_client_frame_shadow_, tiled, frame_->IsMaximized());

  UpdateWindowTitle();

  for (auto& button : nav_buttons_) {
    // Unretained() is safe because the buttons are added as children to, and
    // thus owned by, this view. Thus, the buttons themselves will be destroyed
    // when this view is destroyed, and the frame's life must never outlive the
    // view.
    button.button->SetCallback(
        base::BindRepeating(button.callback, base::Unretained(frame)));
  }

  UpdateThemeValues();
}

gfx::Insets ClientFrameViewLinux::GetBorderDecorationInsets() const {
  const auto insets = frame_provider_->GetFrameThicknessDip();
  // We shouldn't draw frame decorations for the tiled edges.
  // See https://wayland.app/protocols/xdg-shell#xdg_toplevel:enum:state
  return gfx::Insets::TLBR(tiled_edges().top ? 0 : insets.top(),
                           tiled_edges().left ? 0 : insets.left(),
                           tiled_edges().bottom ? 0 : insets.bottom(),
                           tiled_edges().right ? 0 : insets.right());
}

gfx::Insets ClientFrameViewLinux::GetInputInsets() const {
  return gfx::Insets(
      host_supports_client_frame_shadow_ ? -kResizeOutsideBorderSize : 0);
}

gfx::Rect ClientFrameViewLinux::GetWindowContentBounds() const {
  gfx::Rect content_bounds = bounds();
  content_bounds.Inset(GetBorderDecorationInsets());
  return content_bounds;
}

SkRRect ClientFrameViewLinux::GetRoundedWindowContentBounds() const {
  SkRect rect = gfx::RectToSkRect(GetWindowContentBounds());
  SkRRect rrect;

  if (!frame_->IsMaximized()) {
    SkPoint round_point{theme_values_.window_border_radius,
                        theme_values_.window_border_radius};
    SkPoint radii[] = {round_point, round_point, {}, {}};
    rrect.setRectRadii(rect, radii);
  } else {
    rrect.setRect(rect);
  }

  return rrect;
}

void ClientFrameViewLinux::OnNativeThemeUpdated(
    ui::NativeTheme* observed_theme) {
  UpdateThemeValues();
}

void ClientFrameViewLinux::OnWindowButtonOrderingChange() {
  auto* provider = views::WindowButtonOrderProvider::GetInstance();
  leading_frame_buttons_ = provider->leading_buttons();
  trailing_frame_buttons_ = provider->trailing_buttons();

  InvalidateLayout();
}

int ClientFrameViewLinux::ResizingBorderHitTest(const gfx::Point& point) {
  return ResizingBorderHitTestImpl(
      point,
      GetBorderDecorationInsets() + gfx::Insets(kResizeInsideBoundsSize));
}

gfx::Rect ClientFrameViewLinux::GetBoundsForClientView() const {
  gfx::Rect client_bounds = bounds();
  if (!frame_->IsFullscreen()) {
    client_bounds.Inset(GetBorderDecorationInsets());
    client_bounds.Inset(
        gfx::Insets::TLBR(GetTitlebarBounds().height(), 0, 0, 0));
  }
  return client_bounds;
}

gfx::Rect ClientFrameViewLinux::GetWindowBoundsForClientBounds(
    const gfx::Rect& client_bounds) const {
  gfx::Insets insets = bounds().InsetsFrom(GetBoundsForClientView());
  return gfx::Rect(std::max(0, client_bounds.x() - insets.left()),
                   std::max(0, client_bounds.y() - insets.top()),
                   client_bounds.width() + insets.width(),
                   client_bounds.height() + insets.height());
}

int ClientFrameViewLinux::NonClientHitTest(const gfx::Point& point) {
  int component = ResizingBorderHitTest(point);
  if (component != HTNOWHERE) {
    return component;
  }

  for (auto& button : nav_buttons_) {
    if (button.button->GetVisible() &&
        button.button->GetMirroredBounds().Contains(point)) {
      return button.hit_test_id;
    }
  }

  if (GetTitlebarBounds().Contains(point)) {
    return HTCAPTION;
  }

  return FramelessView::NonClientHitTest(point);
}

void ClientFrameViewLinux::GetWindowMask(const gfx::Size& size,
                                         SkPath* window_mask) {
  // Nothing to do here, as transparency is used for decorations, not masks.
}

void ClientFrameViewLinux::UpdateWindowTitle() {
  title_->SetText(base::UTF8ToUTF16(window_->GetTitle()));
}

void ClientFrameViewLinux::SizeConstraintsChanged() {
  InvalidateLayout();
}

gfx::Size ClientFrameViewLinux::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return SizeWithDecorations(
      FramelessView::CalculatePreferredSize(available_size));
}

gfx::Size ClientFrameViewLinux::GetMinimumSize() const {
  return SizeWithDecorations(FramelessView::GetMinimumSize());
}

gfx::Size ClientFrameViewLinux::GetMaximumSize() const {
  return SizeWithDecorations(FramelessView::GetMaximumSize());
}

void ClientFrameViewLinux::Layout(PassKey) {
  LayoutSuperclass<FramelessView>(this);

  if (frame_->IsFullscreen()) {
    // Just hide everything and return.
    for (NavButton& button : nav_buttons_) {
      button.button->SetVisible(false);
    }

    title_->SetVisible(false);
    return;
  }

  bool tiled = tiled_edges().top || tiled_edges().left ||
               tiled_edges().bottom || tiled_edges().right;
  frame_provider_ =
      ui::LinuxUiTheme::GetForProfile(nullptr)->GetWindowFrameProvider(
          !host_supports_client_frame_shadow_, tiled, frame_->IsMaximized());

  UpdateButtonImages();
  LayoutButtons();

  gfx::Rect title_bounds(GetTitlebarContentBounds());
  title_bounds.Inset(theme_values_.title_padding);

  title_->SetVisible(true);
  title_->SetBounds(title_bounds.x(), title_bounds.y(), title_bounds.width(),
                    title_bounds.height());
}

void ClientFrameViewLinux::OnPaint(gfx::Canvas* canvas) {
  if (!frame_->IsFullscreen()) {
    frame_provider_->PaintWindowFrame(canvas, GetLocalBounds(),
                                      GetTitlebarBounds().bottom(),
                                      ShouldPaintAsActive(), GetInputInsets());
  }
}

void ClientFrameViewLinux::PaintAsActiveChanged() {
  UpdateThemeValues();
}

void ClientFrameViewLinux::UpdateThemeValues() {
  gtk::GtkCssContext window_context =
      gtk::AppendCssNodeToStyleContext({}, "window.background.csd");
  gtk::GtkCssContext headerbar_context = gtk::AppendCssNodeToStyleContext(
      {}, "headerbar.default-decoration.titlebar");
  gtk::GtkCssContext title_context =
      gtk::AppendCssNodeToStyleContext(headerbar_context, "label.title");
  gtk::GtkCssContext button_context = gtk::AppendCssNodeToStyleContext(
      headerbar_context, "button.image-button");

  gtk_style_context_set_parent(headerbar_context, window_context);
  gtk_style_context_set_parent(title_context, headerbar_context);
  gtk_style_context_set_parent(button_context, headerbar_context);

  // ShouldPaintAsActive asks the widget, so assume active if the widget is not
  // set yet.
  if (GetWidget() != nullptr && !ShouldPaintAsActive()) {
    gtk_style_context_set_state(window_context, GTK_STATE_FLAG_BACKDROP);
    gtk_style_context_set_state(headerbar_context, GTK_STATE_FLAG_BACKDROP);
    gtk_style_context_set_state(title_context, GTK_STATE_FLAG_BACKDROP);
    gtk_style_context_set_state(button_context, GTK_STATE_FLAG_BACKDROP);
  }

  theme_values_.window_border_radius = frame_provider_->GetTopCornerRadiusDip();

  gtk::GtkStyleContextGet(headerbar_context, "min-height",
                          &theme_values_.titlebar_min_height, nullptr);
  theme_values_.titlebar_padding =
      gtk::GtkStyleContextGetPadding(headerbar_context);

  theme_values_.title_color = gtk::GtkStyleContextGetColor(title_context);
  theme_values_.title_padding = gtk::GtkStyleContextGetPadding(title_context);

  gtk::GtkStyleContextGet(button_context, "min-height",
                          &theme_values_.button_min_size, nullptr);
  theme_values_.button_padding = gtk::GtkStyleContextGetPadding(button_context);

  title_->SetEnabledColor(theme_values_.title_color);

  InvalidateLayout();
  SchedulePaint();
}

ui::NavButtonProvider::FrameButtonDisplayType
ClientFrameViewLinux::GetButtonTypeToSkip() const {
  return frame_->IsMaximized()
             ? ui::NavButtonProvider::FrameButtonDisplayType::kMaximize
             : ui::NavButtonProvider::FrameButtonDisplayType::kRestore;
}

void ClientFrameViewLinux::UpdateButtonImages() {
  nav_button_provider_->RedrawImages(theme_values_.button_min_size,
                                     frame_->IsMaximized(),
                                     ShouldPaintAsActive());

  ui::NavButtonProvider::FrameButtonDisplayType skip_type =
      GetButtonTypeToSkip();

  for (NavButton& button : nav_buttons_) {
    if (button.type == skip_type) {
      continue;
    }

    for (size_t state_id = 0; state_id < views::Button::STATE_COUNT;
         state_id++) {
      views::Button::ButtonState state =
          static_cast<views::Button::ButtonState>(state_id);
      button.button->SetImageModel(
          state, ui::ImageModel::FromImageSkia(nav_button_provider_->GetImage(
                     button.type, ButtonStateToNavButtonProviderState(state))));
    }
  }
}

void ClientFrameViewLinux::LayoutButtons() {
  for (NavButton& button : nav_buttons_) {
    button.button->SetVisible(false);
  }

  gfx::Rect remaining_content_bounds = GetTitlebarContentBounds();
  LayoutButtonsOnSide(ButtonSide::kLeading, &remaining_content_bounds);
  LayoutButtonsOnSide(ButtonSide::kTrailing, &remaining_content_bounds);
}

void ClientFrameViewLinux::LayoutButtonsOnSide(
    ButtonSide side,
    gfx::Rect* remaining_content_bounds) {
  ui::NavButtonProvider::FrameButtonDisplayType skip_type =
      GetButtonTypeToSkip();

  std::vector<views::FrameButton> frame_buttons;

  switch (side) {
    case ButtonSide::kLeading:
      frame_buttons = leading_frame_buttons_;
      break;
    case ButtonSide::kTrailing:
      frame_buttons = trailing_frame_buttons_;
      // We always lay buttons out going from the edge towards the center, but
      // they are given to us as left-to-right, so reverse them.
      std::reverse(frame_buttons.begin(), frame_buttons.end());
      break;
    default:
      NOTREACHED();
  }

  for (views::FrameButton frame_button : frame_buttons) {
    auto* button = std::find_if(
        nav_buttons_.begin(), nav_buttons_.end(), [&](const NavButton& test) {
          return test.type != skip_type && test.frame_button == frame_button;
        });
    CHECK(button != nav_buttons_.end())
        << "Failed to find frame button: " << static_cast<int>(frame_button);

    if (button->type == skip_type) {
      continue;
    }

    button->button->SetVisible(true);

    int button_width = theme_values_.button_min_size;
    int next_button_offset =
        button_width + nav_button_provider_->GetInterNavButtonSpacing();

    int x_position = 0;
    gfx::Insets inset_after_placement;

    switch (side) {
      case ButtonSide::kLeading:
        x_position = remaining_content_bounds->x();
        inset_after_placement.set_left(next_button_offset);
        break;
      case ButtonSide::kTrailing:
        x_position = remaining_content_bounds->right() - button_width;
        inset_after_placement.set_right(next_button_offset);
        break;
      default:
        NOTREACHED();
    }

    button->button->SetBounds(x_position, remaining_content_bounds->y(),
                              button_width, remaining_content_bounds->height());
    remaining_content_bounds->Inset(inset_after_placement);
  }
}

gfx::Rect ClientFrameViewLinux::GetTitlebarBounds() const {
  if (frame_->IsFullscreen()) {
    return gfx::Rect();
  }

  int font_height = gfx::FontList().GetHeight();
  int titlebar_height =
      std::max(font_height, theme_values_.titlebar_min_height) +
      GetTitlebarContentInsets().height();

  gfx::Insets decoration_insets = GetBorderDecorationInsets();

  // We add the inset height here, so the .Inset() that follows won't reduce it
  // to be too small.
  gfx::Rect titlebar(width(), titlebar_height + decoration_insets.height());
  titlebar.Inset(decoration_insets);
  return titlebar;
}

gfx::Insets ClientFrameViewLinux::GetTitlebarContentInsets() const {
  return theme_values_.titlebar_padding +
         nav_button_provider_->GetTopAreaSpacing();
}

gfx::Rect ClientFrameViewLinux::GetTitlebarContentBounds() const {
  gfx::Rect titlebar(GetTitlebarBounds());
  titlebar.Inset(GetTitlebarContentInsets());
  return titlebar;
}

gfx::Size ClientFrameViewLinux::SizeWithDecorations(gfx::Size size) const {
  gfx::Insets decoration_insets = GetBorderDecorationInsets();

  size.Enlarge(0, GetTitlebarBounds().height());
  size.Enlarge(decoration_insets.width(), decoration_insets.height());
  return size;
}

views::View* ClientFrameViewLinux::TargetForRect(views::View* root,
                                                 const gfx::Rect& rect) {
  return views::NonClientFrameView::TargetForRect(root, rect);
}

int ClientFrameViewLinux::GetTranslucentTopAreaHeight() const {
  return 0;
}

BEGIN_METADATA(ClientFrameViewLinux) END_METADATA

}  // namespace electron
