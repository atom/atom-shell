// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/native_window.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/containers/contains.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "content/public/browser/web_contents_user_data.h"
#include "include/core/SkColor.h"
#include "shell/browser/background_throttling_source.h"
#include "shell/browser/browser.h"
#include "shell/browser/draggable_region_provider.h"
#include "shell/browser/native_window_features.h"
#include "shell/browser/ui/drag_util.h"
#include "shell/browser/window_list.h"
#include "shell/common/color_util.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/persistent_dictionary.h"
#include "shell/common/options_switches.h"
#include "ui/base/hit_test.h"
#include "ui/compositor/compositor.h"
#include "ui/views/widget/widget.h"

#if !BUILDFLAG(IS_MAC)
#include "shell/browser/ui/views/frameless_view.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "ui/display/win/screen_win.h"
#endif

#if defined(USE_OZONE)
#include "ui/base/ui_base_features.h"
#include "ui/ozone/public/ozone_platform.h"
#endif

namespace gin {

template <>
struct Converter<electron::NativeWindow::TitleBarStyle> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     electron::NativeWindow::TitleBarStyle* out) {
    using TitleBarStyle = electron::NativeWindow::TitleBarStyle;
    std::string title_bar_style;
    if (!ConvertFromV8(isolate, val, &title_bar_style))
      return false;
    if (title_bar_style == "hidden") {
      *out = TitleBarStyle::kHidden;
#if BUILDFLAG(IS_MAC)
    } else if (title_bar_style == "hiddenInset") {
      *out = TitleBarStyle::kHiddenInset;
    } else if (title_bar_style == "customButtonsOnHover") {
      *out = TitleBarStyle::kCustomButtonsOnHover;
#endif
    } else {
      return false;
    }
    return true;
  }
};

}  // namespace gin

namespace electron {

namespace {

#if BUILDFLAG(IS_WIN)
gfx::Size GetExpandedWindowSize(const NativeWindow* window, gfx::Size size) {
  if (!window->transparent())
    return size;

  gfx::Size min_size = display::win::ScreenWin::ScreenToDIPSize(
      window->GetAcceleratedWidget(), gfx::Size(64, 64));

  // Some AMD drivers can't display windows that are less than 64x64 pixels,
  // so expand them to be at least that size. http://crbug.com/286609
  gfx::Size expanded(std::max(size.width(), min_size.width()),
                     std::max(size.height(), min_size.height()));
  return expanded;
}
#endif

}  // namespace

const char kElectronNativeWindowKey[] = "__ELECTRON_NATIVE_WINDOW__";

NativeWindow::NativeWindow(const gin_helper::Dictionary& options,
                           NativeWindow* parent)
    : widget_(std::make_unique<views::Widget>()), parent_(parent) {
  ++next_id_;

  options.Get(options::kFrame, &has_frame_);
  options.Get(options::kTransparent, &transparent_);
  options.Get(options::kEnableLargerThanScreen, &enable_larger_than_screen_);
  options.Get(options::kTitleBarStyle, &title_bar_style_);
#if BUILDFLAG(IS_WIN)
  options.Get(options::kBackgroundMaterial, &background_material_);
#elif BUILDFLAG(IS_MAC)
  options.Get(options::kVibrancyType, &vibrancy_);
#endif

  v8::Local<v8::Value> titlebar_overlay;
  if (options.Get(options::ktitleBarOverlay, &titlebar_overlay)) {
    if (titlebar_overlay->IsBoolean()) {
      options.Get(options::ktitleBarOverlay, &titlebar_overlay_);
    } else if (titlebar_overlay->IsObject()) {
      titlebar_overlay_ = true;

      auto titlebar_overlay_dict =
          gin_helper::Dictionary::CreateEmpty(options.isolate());
      options.Get(options::ktitleBarOverlay, &titlebar_overlay_dict);
      int height;
      if (titlebar_overlay_dict.Get(options::kOverlayHeight, &height))
        titlebar_overlay_height_ = height;
    }
  }

  if (parent)
    options.Get("modal", &is_modal_);

#if defined(USE_OZONE)
  // Ozone X11 likes to prefer custom frames, but we don't need them unless
  // on Wayland.
  if (base::FeatureList::IsEnabled(features::kWaylandWindowDecorations) &&
      !ui::OzonePlatform::GetInstance()
           ->GetPlatformRuntimeProperties()
           .supports_server_side_window_decorations) {
    has_client_frame_ = true;
  }
#endif

  WindowList::AddWindow(this);
}

NativeWindow::~NativeWindow() {
  // It's possible that the windows gets destroyed before it's closed, in that
  // case we need to ensure the Widget delegate gets destroyed and
  // OnWindowClosed message is still notified.
  if (widget_->widget_delegate())
    widget_->OnNativeWidgetDestroyed();
  NotifyWindowClosed();
}

void NativeWindow::InitFromOptions(const gin_helper::Dictionary& options) {
  // Setup window from options.
  int x = -1, y = -1;
  bool center;
  if (options.Get(options::kX, &x) && options.Get(options::kY, &y)) {
    SetPosition(gfx::Point(x, y));

#if BUILDFLAG(IS_WIN)
    // FIXME(felixrieseberg): Dirty, dirty workaround for
    // https://github.com/electron/electron/issues/10862
    // Somehow, we need to call `SetBounds` twice to get
    // usable results. The root cause is still unknown.
    SetPosition(gfx::Point(x, y));
#endif
  } else if (options.Get(options::kCenter, &center) && center) {
    Center();
  }

  bool use_content_size = false;
  options.Get(options::kUseContentSize, &use_content_size);

  // On Linux and Window we may already have maximum size defined.
  extensions::SizeConstraints size_constraints(
      use_content_size ? GetContentSizeConstraints() : GetSizeConstraints());

  int min_width = size_constraints.GetMinimumSize().width();
  int min_height = size_constraints.GetMinimumSize().height();
  options.Get(options::kMinWidth, &min_width);
  options.Get(options::kMinHeight, &min_height);
  size_constraints.set_minimum_size(gfx::Size(min_width, min_height));

  gfx::Size max_size = size_constraints.GetMaximumSize();
  int max_width = max_size.width() > 0 ? max_size.width() : INT_MAX;
  int max_height = max_size.height() > 0 ? max_size.height() : INT_MAX;
  bool have_max_width = options.Get(options::kMaxWidth, &max_width);
  if (have_max_width && max_width <= 0)
    max_width = INT_MAX;
  bool have_max_height = options.Get(options::kMaxHeight, &max_height);
  if (have_max_height && max_height <= 0)
    max_height = INT_MAX;

  // By default the window has a default maximum size that prevents it
  // from being resized larger than the screen, so we should only set this
  // if the user has passed in values.
  if (have_max_height || have_max_width || !max_size.IsEmpty())
    size_constraints.set_maximum_size(gfx::Size(max_width, max_height));

  if (use_content_size) {
    SetContentSizeConstraints(size_constraints);
  } else {
    SetSizeConstraints(size_constraints);
  }
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
  bool closable;
  if (options.Get(options::kClosable, &closable)) {
    SetClosable(closable);
  }
#endif
  bool movable;
  if (options.Get(options::kMovable, &movable)) {
    SetMovable(movable);
  }
  bool has_shadow;
  if (options.Get(options::kHasShadow, &has_shadow)) {
    SetHasShadow(has_shadow);
  }
  double opacity;
  if (options.Get(options::kOpacity, &opacity)) {
    SetOpacity(opacity);
  }
  bool top;
  if (options.Get(options::kAlwaysOnTop, &top) && top) {
    SetAlwaysOnTop(ui::ZOrderLevel::kFloatingWindow);
  }

  bool fullscreenable = true;
  bool fullscreen = false;
  if (options.Get(options::kFullscreen, &fullscreen) && !fullscreen) {
    // Disable fullscreen button if 'fullscreen' is specified to false.
#if BUILDFLAG(IS_MAC)
    fullscreenable = false;
#endif
  }

  options.Get(options::kFullScreenable, &fullscreenable);
  SetFullScreenable(fullscreenable);

  if (fullscreen)
    SetFullScreen(true);

  bool resizable;
  if (options.Get(options::kResizable, &resizable)) {
    SetResizable(resizable);
  }

  bool skip;
  if (options.Get(options::kSkipTaskbar, &skip)) {
    SetSkipTaskbar(skip);
  }
  bool kiosk;
  if (options.Get(options::kKiosk, &kiosk) && kiosk) {
    SetKiosk(kiosk);
  }
#if BUILDFLAG(IS_MAC)
  std::string type;
  if (options.Get(options::kVibrancyType, &type)) {
    SetVibrancy(type);
  }
#elif BUILDFLAG(IS_WIN)
  std::string material;
  if (options.Get(options::kBackgroundMaterial, &material)) {
    SetBackgroundMaterial(material);
  }
#endif

  SkColor background_color = SK_ColorWHITE;
  if (std::string color; options.Get(options::kBackgroundColor, &color)) {
    background_color = ParseCSSColor(color);
  } else if (IsTranslucent()) {
    background_color = SK_ColorTRANSPARENT;
  }
  SetBackgroundColor(background_color);

  std::string title(Browser::Get()->GetName());
  options.Get(options::kTitle, &title);
  SetTitle(title);

  // Then show it.
  bool show = true;
  options.Get(options::kShow, &show);
  if (show)
    Show();
}

bool NativeWindow::IsClosed() const {
  return is_closed_;
}

void NativeWindow::SetSize(const gfx::Size& size, bool animate) {
  SetBounds(gfx::Rect(GetPosition(), size), animate);
}

gfx::Size NativeWindow::GetSize() const {
  return GetBounds().size();
}

void NativeWindow::SetPosition(const gfx::Point& position, bool animate) {
  SetBounds(gfx::Rect(position, GetSize()), animate);
}

gfx::Point NativeWindow::GetPosition() const {
  return GetBounds().origin();
}

void NativeWindow::SetContentSize(const gfx::Size& size, bool animate) {
  SetSize(ContentBoundsToWindowBounds(gfx::Rect(size)).size(), animate);
}

gfx::Size NativeWindow::GetContentSize() const {
  return GetContentBounds().size();
}

void NativeWindow::SetContentBounds(const gfx::Rect& bounds, bool animate) {
  SetBounds(ContentBoundsToWindowBounds(bounds), animate);
}

gfx::Rect NativeWindow::GetContentBounds() const {
  return WindowBoundsToContentBounds(GetBounds());
}

bool NativeWindow::IsNormal() const {
  return !IsMinimized() && !IsMaximized() && !IsFullscreen();
}

void NativeWindow::SetSizeConstraints(
    const extensions::SizeConstraints& window_constraints) {
  size_constraints_ = window_constraints;
  content_size_constraints_.reset();
}

extensions::SizeConstraints NativeWindow::GetSizeConstraints() const {
  if (size_constraints_)
    return *size_constraints_;
  if (!content_size_constraints_)
    return extensions::SizeConstraints();
  // Convert content size constraints to window size constraints.
  extensions::SizeConstraints constraints;
  if (content_size_constraints_->HasMaximumSize()) {
    gfx::Rect max_bounds = ContentBoundsToWindowBounds(
        gfx::Rect(content_size_constraints_->GetMaximumSize()));
    constraints.set_maximum_size(max_bounds.size());
  }
  if (content_size_constraints_->HasMinimumSize()) {
    gfx::Rect min_bounds = ContentBoundsToWindowBounds(
        gfx::Rect(content_size_constraints_->GetMinimumSize()));
    constraints.set_minimum_size(min_bounds.size());
  }
  return constraints;
}

void NativeWindow::SetContentSizeConstraints(
    const extensions::SizeConstraints& size_constraints) {
  content_size_constraints_ = size_constraints;
  size_constraints_.reset();
}

// Windows/Linux:
// The return value of GetContentSizeConstraints will be passed to Chromium
// to set min/max sizes of window. Note that we are returning content size
// instead of window size because that is what Chromium expects, see the
// comment of |WidgetSizeIsClientSize| in Chromium's codebase to learn more.
//
// macOS:
// The min/max sizes are set directly by calling NSWindow's methods.
extensions::SizeConstraints NativeWindow::GetContentSizeConstraints() const {
  if (content_size_constraints_)
    return *content_size_constraints_;
  if (!size_constraints_)
    return extensions::SizeConstraints();
  // Convert window size constraints to content size constraints.
  // Note that we are not caching the results, because Chromium reccalculates
  // window frame size everytime when min/max sizes are passed, and we must
  // do the same otherwise the resulting size with frame included will be wrong.
  extensions::SizeConstraints constraints;
  if (size_constraints_->HasMaximumSize()) {
    gfx::Rect max_bounds = WindowBoundsToContentBounds(
        gfx::Rect(size_constraints_->GetMaximumSize()));
    constraints.set_maximum_size(max_bounds.size());
  }
  if (size_constraints_->HasMinimumSize()) {
    gfx::Rect min_bounds = WindowBoundsToContentBounds(
        gfx::Rect(size_constraints_->GetMinimumSize()));
    constraints.set_minimum_size(min_bounds.size());
  }
  return constraints;
}

void NativeWindow::SetMinimumSize(const gfx::Size& size) {
  extensions::SizeConstraints size_constraints = GetSizeConstraints();
  size_constraints.set_minimum_size(size);
  SetSizeConstraints(size_constraints);
}

gfx::Size NativeWindow::GetMinimumSize() const {
  return GetSizeConstraints().GetMinimumSize();
}

void NativeWindow::SetMaximumSize(const gfx::Size& size) {
  extensions::SizeConstraints size_constraints = GetSizeConstraints();
  size_constraints.set_maximum_size(size);
  SetSizeConstraints(size_constraints);
}

gfx::Size NativeWindow::GetMaximumSize() const {
  return GetSizeConstraints().GetMaximumSize();
}

gfx::Size NativeWindow::GetContentMinimumSize() const {
  return GetContentSizeConstraints().GetMinimumSize();
}

gfx::Size NativeWindow::GetContentMaximumSize() const {
  gfx::Size maximum_size = GetContentSizeConstraints().GetMaximumSize();
#if BUILDFLAG(IS_WIN)
  return GetContentSizeConstraints().HasMaximumSize()
             ? GetExpandedWindowSize(this, maximum_size)
             : maximum_size;
#else
  return maximum_size;
#endif
}

void NativeWindow::SetSheetOffset(const double offsetX, const double offsetY) {
  sheet_offset_x_ = offsetX;
  sheet_offset_y_ = offsetY;
}

double NativeWindow::GetSheetOffsetX() const {
  return sheet_offset_x_;
}

double NativeWindow::GetSheetOffsetY() const {
  return sheet_offset_y_;
}

bool NativeWindow::IsTabletMode() const {
  return false;
}

std::string NativeWindow::GetRepresentedFilename() const {
  return "";
}

bool NativeWindow::IsDocumentEdited() const {
  return false;
}

bool NativeWindow::IsFocusable() const {
  return false;
}

void NativeWindow::SetParentWindow(NativeWindow* parent) {
  parent_ = parent;
}

bool NativeWindow::AddTabbedWindow(NativeWindow* window) {
  return true;  // for non-Mac platforms
}

std::optional<std::string> NativeWindow::GetTabbingIdentifier() const {
  return "";  // for non-Mac platforms
}

void NativeWindow::SetVibrancy(const std::string& type) {
  vibrancy_ = type;
}

void NativeWindow::SetBackgroundMaterial(const std::string& type) {
  background_material_ = type;
}

void NativeWindow::SetTouchBar(
    std::vector<gin_helper::PersistentDictionary> items) {}

void NativeWindow::SetEscapeTouchBarItem(
    gin_helper::PersistentDictionary item) {}

bool NativeWindow::IsMenuBarAutoHide() const {
  return false;
}

bool NativeWindow::IsMenuBarVisible() const {
  return true;
}

void NativeWindow::SetAspectRatio(double aspect_ratio,
                                  const gfx::Size& extra_size) {
  aspect_ratio_ = aspect_ratio;
  aspect_ratio_extraSize_ = extra_size;
}

std::optional<gfx::Rect> NativeWindow::GetWindowControlsOverlayRect() {
  return overlay_rect_;
}

void NativeWindow::SetWindowControlsOverlayRect(const gfx::Rect& overlay_rect) {
  overlay_rect_ = overlay_rect;
}

void NativeWindow::NotifyWindowRequestPreferredWidth(int* width) {
  for (NativeWindowObserver& observer : observers_)
    observer.RequestPreferredWidth(width);
}

void NativeWindow::NotifyWindowCloseButtonClicked() {
  // First ask the observers whether we want to close.
  bool prevent_default = false;
  for (NativeWindowObserver& observer : observers_)
    observer.WillCloseWindow(&prevent_default);
  if (prevent_default) {
    WindowList::WindowCloseCancelled(this);
    return;
  }

  // Then ask the observers how should we close the window.
  for (NativeWindowObserver& observer : observers_)
    observer.OnCloseButtonClicked(&prevent_default);
  if (prevent_default)
    return;

  CloseImmediately();
}

void NativeWindow::NotifyWindowClosed() {
  if (is_closed_)
    return;

  is_closed_ = true;
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowClosed();

  WindowList::RemoveWindow(this);
}

void NativeWindow::NotifyWindowEndSession() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowEndSession();
}

void NativeWindow::NotifyWindowBlur() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowBlur();
}

void NativeWindow::NotifyWindowFocus() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowFocus();
}

void NativeWindow::NotifyWindowIsKeyChanged(bool is_key) {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowIsKeyChanged(is_key);
}

void NativeWindow::NotifyWindowShow() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowShow();
}

void NativeWindow::NotifyWindowHide() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowHide();
}

void NativeWindow::NotifyWindowMaximize() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowMaximize();
}

void NativeWindow::NotifyWindowUnmaximize() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowUnmaximize();
}

void NativeWindow::NotifyWindowMinimize() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowMinimize();
}

void NativeWindow::NotifyWindowRestore() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowRestore();
}

void NativeWindow::NotifyWindowWillResize(const gfx::Rect& new_bounds,
                                          const gfx::ResizeEdge& edge,
                                          bool* prevent_default) {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowWillResize(new_bounds, edge, prevent_default);
}

void NativeWindow::NotifyWindowWillMove(const gfx::Rect& new_bounds,
                                        bool* prevent_default) {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowWillMove(new_bounds, prevent_default);
}

void NativeWindow::NotifyWindowResize() {
  NotifyLayoutWindowControlsOverlay();
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowResize();
}

void NativeWindow::NotifyWindowResized() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowResized();
}

void NativeWindow::NotifyWindowMove() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowMove();
}

void NativeWindow::NotifyWindowMoved() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowMoved();
}

void NativeWindow::NotifyWindowEnterFullScreen() {
  NotifyLayoutWindowControlsOverlay();
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowEnterFullScreen();
}

void NativeWindow::NotifyWindowSwipe(const std::string& direction) {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowSwipe(direction);
}

void NativeWindow::NotifyWindowRotateGesture(float rotation) {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowRotateGesture(rotation);
}

void NativeWindow::NotifyWindowSheetBegin() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowSheetBegin();
}

void NativeWindow::NotifyWindowSheetEnd() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowSheetEnd();
}

void NativeWindow::NotifyWindowLeaveFullScreen() {
  NotifyLayoutWindowControlsOverlay();
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowLeaveFullScreen();
}

void NativeWindow::NotifyWindowEnterHtmlFullScreen() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowEnterHtmlFullScreen();
}

void NativeWindow::NotifyWindowLeaveHtmlFullScreen() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowLeaveHtmlFullScreen();
}

void NativeWindow::NotifyWindowAlwaysOnTopChanged() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowAlwaysOnTopChanged();
}

void NativeWindow::NotifyWindowExecuteAppCommand(const std::string& command) {
  for (NativeWindowObserver& observer : observers_)
    observer.OnExecuteAppCommand(command);
}

void NativeWindow::NotifyTouchBarItemInteraction(const std::string& item_id,
                                                 base::Value::Dict details) {
  for (NativeWindowObserver& observer : observers_)
    observer.OnTouchBarItemResult(item_id, details);
}

void NativeWindow::NotifyNewWindowForTab() {
  for (NativeWindowObserver& observer : observers_)
    observer.OnNewWindowForTab();
}

void NativeWindow::NotifyWindowSystemContextMenu(int x,
                                                 int y,
                                                 bool* prevent_default) {
  for (NativeWindowObserver& observer : observers_)
    observer.OnSystemContextMenu(x, y, prevent_default);
}

void NativeWindow::NotifyLayoutWindowControlsOverlay() {
  auto bounding_rect = GetWindowControlsOverlayRect();
  if (bounding_rect.has_value()) {
    for (NativeWindowObserver& observer : observers_)
      observer.UpdateWindowControlsOverlay(bounding_rect.value());
  }
}

#if BUILDFLAG(IS_WIN)
void NativeWindow::NotifyWindowMessage(UINT message,
                                       WPARAM w_param,
                                       LPARAM l_param) {
  for (NativeWindowObserver& observer : observers_)
    observer.OnWindowMessage(message, w_param, l_param);
}
#endif

int NativeWindow::NonClientHitTest(const gfx::Point& point) {
#if !BUILDFLAG(IS_MAC)
  // We need to ensure we account for resizing borders on Windows and Linux.
  if ((!has_frame() || has_client_frame()) && IsResizable()) {
    auto* frame =
        static_cast<FramelessView*>(widget()->non_client_view()->frame_view());
    int border_hit = frame->ResizingBorderHitTest(point);
    if (border_hit != HTNOWHERE)
      return border_hit;
  }
#endif

  // This is to disable dragging in HTML5 full screen mode.
  // Details: https://github.com/electron/electron/issues/41002
  if (GetWidget()->IsFullscreen())
    return HTNOWHERE;

  for (auto* provider : draggable_region_providers_) {
    int hit = provider->NonClientHitTest(point);
    if (hit != HTNOWHERE)
      return hit;
  }
  return HTNOWHERE;
}

void NativeWindow::AddDraggableRegionProvider(
    DraggableRegionProvider* provider) {
  if (!base::Contains(draggable_region_providers_, provider)) {
    draggable_region_providers_.push_back(provider);
  }
}

void NativeWindow::RemoveDraggableRegionProvider(
    DraggableRegionProvider* provider) {
  draggable_region_providers_.remove_if(
      [&provider](DraggableRegionProvider* p) { return p == provider; });
}

void NativeWindow::AddBackgroundThrottlingSource(
    BackgroundThrottlingSource* source) {
  auto result = background_throttling_sources_.insert(source);
  DCHECK(result.second) << "Added already stored BackgroundThrottlingSource.";
  UpdateBackgroundThrottlingState();
}

void NativeWindow::RemoveBackgroundThrottlingSource(
    BackgroundThrottlingSource* source) {
  auto result = background_throttling_sources_.erase(source);
  DCHECK(result == 1)
      << "Tried to remove non existing BackgroundThrottlingSource.";
  UpdateBackgroundThrottlingState();
}

void NativeWindow::UpdateBackgroundThrottlingState() {
  if (!GetWidget() || !GetWidget()->GetCompositor()) {
    return;
  }
  bool enable_background_throttling = true;
  for (const auto* background_throttling_source :
       background_throttling_sources_) {
    if (!background_throttling_source->GetBackgroundThrottling()) {
      enable_background_throttling = false;
      break;
    }
  }
  GetWidget()->GetCompositor()->SetBackgroundThrottling(
      enable_background_throttling);
}

views::Widget* NativeWindow::GetWidget() {
  return widget();
}

const views::Widget* NativeWindow::GetWidget() const {
  return widget();
}

std::u16string NativeWindow::GetAccessibleWindowTitle() const {
  if (accessible_title_.empty()) {
    return views::WidgetDelegate::GetAccessibleWindowTitle();
  }

  return accessible_title_;
}

void NativeWindow::SetAccessibleTitle(const std::string& title) {
  accessible_title_ = base::UTF8ToUTF16(title);
}

std::string NativeWindow::GetAccessibleTitle() {
  return base::UTF16ToUTF8(accessible_title_);
}

void NativeWindow::HandlePendingFullscreenTransitions() {
  if (pending_transitions_.empty()) {
    set_fullscreen_transition_type(FullScreenTransitionType::kNone);
    return;
  }

  bool next_transition = pending_transitions_.front();
  pending_transitions_.pop();
  SetFullScreen(next_transition);
}

// static
int32_t NativeWindow::next_id_ = 0;

bool NativeWindow::IsTranslucent() const {
  // Transparent windows are translucent
  if (transparent()) {
    return true;
  }

#if BUILDFLAG(IS_MAC)
  // Windows with vibrancy set are translucent
  if (!vibrancy().empty()) {
    return true;
  }
#endif

#if BUILDFLAG(IS_WIN)
  // Windows with certain background materials may be translucent
  const std::string& bg_material = background_material();
  if (!bg_material.empty() && bg_material != "none") {
    return true;
  }
#endif

  return false;
}

// static
void NativeWindowRelay::CreateForWebContents(
    content::WebContents* web_contents,
    base::WeakPtr<NativeWindow> window) {
  DCHECK(web_contents);
  if (!web_contents->GetUserData(UserDataKey())) {
    web_contents->SetUserData(
        UserDataKey(),
        base::WrapUnique(new NativeWindowRelay(web_contents, window)));
  }
}

NativeWindowRelay::NativeWindowRelay(content::WebContents* web_contents,
                                     base::WeakPtr<NativeWindow> window)
    : content::WebContentsUserData<NativeWindowRelay>(*web_contents),
      native_window_(window) {}

NativeWindowRelay::~NativeWindowRelay() = default;

WEB_CONTENTS_USER_DATA_KEY_IMPL(NativeWindowRelay);

}  // namespace electron
