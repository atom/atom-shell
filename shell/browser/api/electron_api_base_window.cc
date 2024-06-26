// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_base_window.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/contains.h"
#include "base/task/single_thread_task_runner.h"
#include "content/public/common/color_parser.h"
#include "electron/buildflags/buildflags.h"
#include "gin/dictionary.h"
#include "shell/browser/api/electron_api_menu.h"
#include "shell/browser/api/electron_api_view.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/javascript_environment.h"
#include "shell/common/color_util.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/file_path_converter.h"
#include "shell/common/gin_converters/gfx_converter.h"
#include "shell/common/gin_converters/image_converter.h"
#include "shell/common/gin_converters/native_window_converter.h"
#include "shell/common/gin_converters/optional_converter.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/object_template_builder.h"
#include "shell/common/gin_helper/persistent_dictionary.h"
#include "shell/common/node_includes.h"
#include "shell/common/options_switches.h"

#if defined(TOOLKIT_VIEWS)
#include "shell/browser/native_window_views.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "shell/browser/ui/views/win_frame_view.h"
#include "shell/browser/ui/win/taskbar_host.h"
#include "ui/base/win/shell.h"
#endif

#if BUILDFLAG(IS_WIN)
namespace gin {

template <>
struct Converter<electron::TaskbarHost::ThumbarButton> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Handle<v8::Value> val,
                     electron::TaskbarHost::ThumbarButton* out) {
    gin::Dictionary dict(isolate);
    if (!gin::ConvertFromV8(isolate, val, &dict))
      return false;
    dict.Get("click", &(out->clicked_callback));
    dict.Get("tooltip", &(out->tooltip));
    dict.Get("flags", &out->flags);
    return dict.Get("icon", &(out->icon));
  }
};

}  // namespace gin
#endif

namespace electron::api {

namespace {

// Converts binary data to Buffer.
v8::Local<v8::Value> ToBuffer(v8::Isolate* isolate, void* val, int size) {
  auto buffer = node::Buffer::Copy(isolate, static_cast<char*>(val), size);
  if (buffer.IsEmpty())
    return v8::Null(isolate);
  else
    return buffer.ToLocalChecked();
}

}  // namespace

BaseWindow::BaseWindow(v8::Isolate* isolate,
                       const gin_helper::Dictionary& options) {
  // The parent window.
  gin::Handle<BaseWindow> parent;
  if (options.Get("parent", &parent) && !parent.IsEmpty())
    parent_window_.Reset(isolate, parent.ToV8());

  // Offscreen windows are always created frameless.
  gin_helper::Dictionary web_preferences;
  bool offscreen;
  if (options.Get(options::kWebPreferences, &web_preferences) &&
      web_preferences.Get(options::kOffscreen, &offscreen) && offscreen) {
    const_cast<gin_helper::Dictionary&>(options).Set(options::kFrame, false);
  }

  // Creates NativeWindow.
  window_.reset(NativeWindow::Create(
      options, parent.IsEmpty() ? nullptr : parent->window_.get()));
  window_->AddObserver(this);

  SetContentView(View::Create(isolate));

#if defined(TOOLKIT_VIEWS)
  v8::Local<v8::Value> icon;
  if (options.Get(options::kIcon, &icon)) {
    SetIconImpl(isolate, icon, NativeImage::OnConvertError::kWarn);
  }
#endif
}

BaseWindow::BaseWindow(gin_helper::Arguments* args,
                       const gin_helper::Dictionary& options)
    : BaseWindow(args->isolate(), options) {
  InitWithArgs(args);
  // Init window after everything has been setup.
  window()->InitFromOptions(options);
}

BaseWindow::~BaseWindow() {
  CloseImmediately();

  // Remove global reference so the JS object can be garbage collected.
  self_ref_.Reset();
}

void BaseWindow::InitWith(v8::Isolate* isolate, v8::Local<v8::Object> wrapper) {
  AttachAsUserData(window_.get());
  gin_helper::TrackableObject<BaseWindow>::InitWith(isolate, wrapper);

  // We can only append this window to parent window's child windows after this
  // window's JS wrapper gets initialized.
  if (!parent_window_.IsEmpty()) {
    gin::Handle<BaseWindow> parent;
    gin::ConvertFromV8(isolate, GetParentWindow(), &parent);
    DCHECK(!parent.IsEmpty());
    parent->child_windows_.Set(isolate, weak_map_id(), wrapper);
  }

  // Reference this object in case it got garbage collected.
  self_ref_.Reset(isolate, wrapper);
}

void BaseWindow::WillCloseWindow(bool* prevent_default) {
  if (Emit("close")) {
    *prevent_default = true;
  }
}

void BaseWindow::OnWindowClosed() {
  // Invalidate weak ptrs before the Javascript object is destroyed,
  // there might be some delayed emit events which shouldn't be
  // triggered after this.
  weak_factory_.InvalidateWeakPtrs();

  RemoveFromWeakMap();
  window_->RemoveObserver(this);

  // We can not call Destroy here because we need to call Emit first, but we
  // also do not want any method to be used, so just mark as destroyed here.
  MarkDestroyed();

  Emit("closed");

  RemoveFromParentChildWindows();

  // Destroy the native class when window is closed.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, GetDestroyClosure());
}

void BaseWindow::OnWindowEndSession() {
  Emit("session-end");
}

void BaseWindow::OnWindowBlur() {
  EmitEventSoon("blur");
}

void BaseWindow::OnWindowFocus() {
  EmitEventSoon("focus");
}

void BaseWindow::OnWindowShow() {
  Emit("show");
}

void BaseWindow::OnWindowHide() {
  Emit("hide");
}

void BaseWindow::OnWindowMaximize() {
  Emit("maximize");
}

void BaseWindow::OnWindowUnmaximize() {
  Emit("unmaximize");
}

void BaseWindow::OnWindowMinimize() {
  Emit("minimize");
}

void BaseWindow::OnWindowRestore() {
  Emit("restore");
}

void BaseWindow::OnWindowWillResize(const gfx::Rect& new_bounds,
                                    const gfx::ResizeEdge& edge,
                                    bool* prevent_default) {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  auto info = gin::Dictionary::CreateEmpty(isolate);
  info.Set("edge", edge);

  if (Emit("will-resize", new_bounds, info)) {
    *prevent_default = true;
  }
}

void BaseWindow::OnWindowResize() {
  Emit("resize");
}

void BaseWindow::OnWindowResized() {
  Emit("resized");
}

void BaseWindow::OnWindowWillMove(const gfx::Rect& new_bounds,
                                  bool* prevent_default) {
  if (Emit("will-move", new_bounds)) {
    *prevent_default = true;
  }
}

void BaseWindow::OnWindowMove() {
  Emit("move");
}

void BaseWindow::OnWindowMoved() {
  Emit("moved");
}

void BaseWindow::OnWindowEnterFullScreen() {
  Emit("enter-full-screen");
}

void BaseWindow::OnWindowLeaveFullScreen() {
  Emit("leave-full-screen");
}

void BaseWindow::OnWindowSwipe(const std::string& direction) {
  Emit("swipe", direction);
}

void BaseWindow::OnWindowRotateGesture(float rotation) {
  Emit("rotate-gesture", rotation);
}

void BaseWindow::OnWindowSheetBegin() {
  Emit("sheet-begin");
}

void BaseWindow::OnWindowSheetEnd() {
  Emit("sheet-end");
}

void BaseWindow::OnWindowEnterHtmlFullScreen() {
  Emit("enter-html-full-screen");
}

void BaseWindow::OnWindowLeaveHtmlFullScreen() {
  Emit("leave-html-full-screen");
}

void BaseWindow::OnWindowAlwaysOnTopChanged() {
  Emit("always-on-top-changed", IsAlwaysOnTop());
}

void BaseWindow::OnExecuteAppCommand(const std::string& command_name) {
  Emit("app-command", command_name);
}

void BaseWindow::OnTouchBarItemResult(const std::string& item_id,
                                      const base::Value::Dict& details) {
  Emit("-touch-bar-interaction", item_id, details);
}

void BaseWindow::OnNewWindowForTab() {
  Emit("new-window-for-tab");
}

void BaseWindow::OnSystemContextMenu(int x, int y, bool* prevent_default) {
  if (Emit("system-context-menu", gfx::Point(x, y))) {
    *prevent_default = true;
  }
}

#if BUILDFLAG(IS_WIN)
void BaseWindow::OnWindowMessage(UINT message, WPARAM w_param, LPARAM l_param) {
  if (IsWindowMessageHooked(message)) {
    v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    v8::HandleScope scope(isolate);
    messages_callback_map_[message].Run(
        ToBuffer(isolate, static_cast<void*>(&w_param), sizeof(WPARAM)),
        ToBuffer(isolate, static_cast<void*>(&l_param), sizeof(LPARAM)));
  }
}
#endif

void BaseWindow::SetContentView(gin::Handle<View> view) {
  content_view_.Reset(JavascriptEnvironment::GetIsolate(), view.ToV8());
  window_->SetContentView(view->view());
}

void BaseWindow::CloseImmediately() {
  if (!window_->IsClosed())
    window_->CloseImmediately();
}

void BaseWindow::Close() {
  window_->Close();
}

void BaseWindow::Focus() {
  window_->Focus(true);
}

void BaseWindow::Blur() {
  window_->Focus(false);
}

bool BaseWindow::IsFocused() const {
  return window_->IsFocused();
}

void BaseWindow::Show() {
  window_->Show();
}

void BaseWindow::ShowInactive() {
  // This method doesn't make sense for modal window.
  if (IsModal())
    return;
  window_->ShowInactive();
}

void BaseWindow::Hide() {
  window_->Hide();
}

bool BaseWindow::IsVisible() const {
  return window_->IsVisible();
}

bool BaseWindow::IsEnabled() const {
  return window_->IsEnabled();
}

void BaseWindow::SetEnabled(bool enable) {
  window_->SetEnabled(enable);
}

void BaseWindow::Maximize() {
  window_->Maximize();
}

void BaseWindow::Unmaximize() {
  window_->Unmaximize();
}

bool BaseWindow::IsMaximized() const {
  return window_->IsMaximized();
}

void BaseWindow::Minimize() {
  window_->Minimize();
}

void BaseWindow::Restore() {
  window_->Restore();
}

bool BaseWindow::IsMinimized() const {
  return window_->IsMinimized();
}

void BaseWindow::SetFullScreen(bool fullscreen) {
  window_->SetFullScreen(fullscreen);
}

bool BaseWindow::IsFullscreen() const {
  return window_->IsFullscreen();
}

void BaseWindow::SetBounds(const gfx::Rect& bounds,
                           gin_helper::Arguments* args) {
  bool animate = false;
  args->GetNext(&animate);
  window_->SetBounds(bounds, animate);
}

gfx::Rect BaseWindow::GetBounds() const {
  return window_->GetBounds();
}

bool BaseWindow::IsNormal() const {
  return window_->IsNormal();
}

gfx::Rect BaseWindow::GetNormalBounds() const {
  return window_->GetNormalBounds();
}

void BaseWindow::SetContentBounds(const gfx::Rect& bounds,
                                  gin_helper::Arguments* args) {
  bool animate = false;
  args->GetNext(&animate);
  window_->SetContentBounds(bounds, animate);
}

gfx::Rect BaseWindow::GetContentBounds() const {
  return window_->GetContentBounds();
}

void BaseWindow::SetSize(int width, int height, gin_helper::Arguments* args) {
  bool animate = false;
  gfx::Size size = window_->GetMinimumSize();
  size.SetToMax(gfx::Size(width, height));
  args->GetNext(&animate);
  window_->SetSize(size, animate);
}

std::vector<int> BaseWindow::GetSize() const {
  std::vector<int> result(2);
  gfx::Size size = window_->GetSize();
  result[0] = size.width();
  result[1] = size.height();
  return result;
}

void BaseWindow::SetContentSize(int width,
                                int height,
                                gin_helper::Arguments* args) {
  bool animate = false;
  args->GetNext(&animate);
  window_->SetContentSize(gfx::Size(width, height), animate);
}

std::vector<int> BaseWindow::GetContentSize() const {
  std::vector<int> result(2);
  gfx::Size size = window_->GetContentSize();
  result[0] = size.width();
  result[1] = size.height();
  return result;
}

void BaseWindow::SetMinimumSize(int width, int height) {
  window_->SetMinimumSize(gfx::Size(width, height));
}

std::vector<int> BaseWindow::GetMinimumSize() const {
  std::vector<int> result(2);
  gfx::Size size = window_->GetMinimumSize();
  result[0] = size.width();
  result[1] = size.height();
  return result;
}

void BaseWindow::SetMaximumSize(int width, int height) {
  window_->SetMaximumSize(gfx::Size(width, height));
}

std::vector<int> BaseWindow::GetMaximumSize() const {
  std::vector<int> result(2);
  gfx::Size size = window_->GetMaximumSize();
  result[0] = size.width();
  result[1] = size.height();
  return result;
}

void BaseWindow::SetSheetOffset(double offsetY, gin_helper::Arguments* args) {
  double offsetX = 0.0;
  args->GetNext(&offsetX);
  window_->SetSheetOffset(offsetX, offsetY);
}

void BaseWindow::SetResizable(bool resizable) {
  window_->SetResizable(resizable);
}

bool BaseWindow::IsResizable() const {
  return window_->IsResizable();
}

void BaseWindow::SetMovable(bool movable) {
  window_->SetMovable(movable);
}

bool BaseWindow::IsMovable() const {
  return window_->IsMovable();
}

void BaseWindow::SetMinimizable(bool minimizable) {
  window_->SetMinimizable(minimizable);
}

bool BaseWindow::IsMinimizable() const {
  return window_->IsMinimizable();
}

void BaseWindow::SetMaximizable(bool maximizable) {
  window_->SetMaximizable(maximizable);
}

bool BaseWindow::IsMaximizable() const {
  return window_->IsMaximizable();
}

void BaseWindow::SetFullScreenable(bool fullscreenable) {
  window_->SetFullScreenable(fullscreenable);
}

bool BaseWindow::IsFullScreenable() const {
  return window_->IsFullScreenable();
}

void BaseWindow::SetClosable(bool closable) {
  window_->SetClosable(closable);
}

bool BaseWindow::IsClosable() const {
  return window_->IsClosable();
}

void BaseWindow::SetAlwaysOnTop(bool top, gin_helper::Arguments* args) {
  std::string level = "floating";
  int relative_level = 0;
  args->GetNext(&level);
  args->GetNext(&relative_level);

  ui::ZOrderLevel z_order =
      top ? ui::ZOrderLevel::kFloatingWindow : ui::ZOrderLevel::kNormal;
  window_->SetAlwaysOnTop(z_order, level, relative_level);
}

bool BaseWindow::IsAlwaysOnTop() const {
  return window_->GetZOrderLevel() != ui::ZOrderLevel::kNormal;
}

void BaseWindow::Center() {
  window_->Center();
}

void BaseWindow::SetPosition(int x, int y, gin_helper::Arguments* args) {
  bool animate = false;
  args->GetNext(&animate);
  window_->SetPosition(gfx::Point(x, y), animate);
}

std::vector<int> BaseWindow::GetPosition() const {
  std::vector<int> result(2);
  gfx::Point pos = window_->GetPosition();
  result[0] = pos.x();
  result[1] = pos.y();
  return result;
}
void BaseWindow::MoveAbove(const std::string& sourceId,
                           gin_helper::Arguments* args) {
  if (!window_->MoveAbove(sourceId))
    args->ThrowError("Invalid media source id");
}

void BaseWindow::MoveTop() {
  window_->MoveTop();
}

void BaseWindow::SetTitle(const std::string& title) {
  window_->SetTitle(title);
}

std::string BaseWindow::GetTitle() const {
  return window_->GetTitle();
}

void BaseWindow::SetAccessibleTitle(const std::string& title) {
  window_->SetAccessibleTitle(title);
}

std::string BaseWindow::GetAccessibleTitle() const {
  return window_->GetAccessibleTitle();
}

void BaseWindow::FlashFrame(bool flash) {
  window_->FlashFrame(flash);
}

void BaseWindow::SetSkipTaskbar(bool skip) {
  window_->SetSkipTaskbar(skip);
}

void BaseWindow::SetExcludedFromShownWindowsMenu(bool excluded) {
  window_->SetExcludedFromShownWindowsMenu(excluded);
}

bool BaseWindow::IsExcludedFromShownWindowsMenu() const {
  return window_->IsExcludedFromShownWindowsMenu();
}

void BaseWindow::SetSimpleFullScreen(bool simple_fullscreen) {
  window_->SetSimpleFullScreen(simple_fullscreen);
}

bool BaseWindow::IsSimpleFullScreen() const {
  return window_->IsSimpleFullScreen();
}

void BaseWindow::SetKiosk(bool kiosk) {
  window_->SetKiosk(kiosk);
}

bool BaseWindow::IsKiosk() const {
  return window_->IsKiosk();
}

bool BaseWindow::IsTabletMode() const {
  return window_->IsTabletMode();
}

void BaseWindow::SetBackgroundColor(const std::string& color_name) {
  SkColor color = ParseCSSColor(color_name);
  window_->SetBackgroundColor(color);
}

std::string BaseWindow::GetBackgroundColor(gin_helper::Arguments* args) const {
  return ToRGBHex(window_->GetBackgroundColor());
}

void BaseWindow::InvalidateShadow() {
  window_->InvalidateShadow();
}

void BaseWindow::SetHasShadow(bool has_shadow) {
  window_->SetHasShadow(has_shadow);
}

bool BaseWindow::HasShadow() const {
  return window_->HasShadow();
}

void BaseWindow::SetOpacity(const double opacity) {
  window_->SetOpacity(opacity);
}

double BaseWindow::GetOpacity() const {
  return window_->GetOpacity();
}

void BaseWindow::SetShape(const std::vector<gfx::Rect>& rects) {
  window_->widget()->SetShape(std::make_unique<std::vector<gfx::Rect>>(rects));
}

void BaseWindow::SetRepresentedFilename(const std::string& filename) {
  window_->SetRepresentedFilename(filename);
}

std::string BaseWindow::GetRepresentedFilename() const {
  return window_->GetRepresentedFilename();
}

void BaseWindow::SetDocumentEdited(bool edited) {
  window_->SetDocumentEdited(edited);
}

bool BaseWindow::IsDocumentEdited() const {
  return window_->IsDocumentEdited();
}

void BaseWindow::SetIgnoreMouseEvents(bool ignore,
                                      gin_helper::Arguments* args) {
  gin_helper::Dictionary options;
  bool forward = false;
  args->GetNext(&options) && options.Get("forward", &forward);
  return window_->SetIgnoreMouseEvents(ignore, forward);
}

void BaseWindow::SetContentProtection(bool enable) {
  return window_->SetContentProtection(enable);
}

void BaseWindow::SetFocusable(bool focusable) {
  return window_->SetFocusable(focusable);
}

bool BaseWindow::IsFocusable() const {
  return window_->IsFocusable();
}

void BaseWindow::SetMenu(v8::Isolate* isolate, v8::Local<v8::Value> value) {
  auto context = isolate->GetCurrentContext();
  gin::Handle<Menu> menu;
  v8::Local<v8::Object> object;
  if (value->IsObject() && value->ToObject(context).ToLocal(&object) &&
      gin::ConvertFromV8(isolate, value, &menu) && !menu.IsEmpty()) {
    // We only want to update the menu if the menu has a non-zero item count,
    // or we risk crashes.
    if (menu->model()->GetItemCount() == 0) {
      RemoveMenu();
    } else {
      window_->SetMenu(menu->model());
    }

    menu_.Reset(isolate, menu.ToV8());
  } else if (value->IsNull()) {
    RemoveMenu();
  } else {
    isolate->ThrowException(
        v8::Exception::TypeError(gin::StringToV8(isolate, "Invalid Menu")));
  }
}

void BaseWindow::RemoveMenu() {
  menu_.Reset();
  window_->SetMenu(nullptr);
}

void BaseWindow::SetParentWindow(v8::Local<v8::Value> value,
                                 gin_helper::Arguments* args) {
  if (IsModal()) {
    args->ThrowError("Can not be called for modal window");
    return;
  }

  gin::Handle<BaseWindow> parent;
  if (value->IsNull() || value->IsUndefined()) {
    RemoveFromParentChildWindows();
    parent_window_.Reset();
    window_->SetParentWindow(nullptr);
  } else if (gin::ConvertFromV8(isolate(), value, &parent)) {
    RemoveFromParentChildWindows();
    parent_window_.Reset(isolate(), value);
    window_->SetParentWindow(parent->window_.get());
    parent->child_windows_.Set(isolate(), weak_map_id(), GetWrapper());
  } else {
    args->ThrowError("Must pass BaseWindow instance or null");
  }
}

std::string BaseWindow::GetMediaSourceId() const {
  return window_->GetDesktopMediaID().ToString();
}

v8::Local<v8::Value> BaseWindow::GetNativeWindowHandle() {
  // TODO(MarshallOfSound): Replace once
  // https://chromium-review.googlesource.com/c/chromium/src/+/1253094/ has
  // landed
  NativeWindowHandle handle = window_->GetNativeWindowHandle();
  return ToBuffer(isolate(), &handle, sizeof(handle));
}

void BaseWindow::SetProgressBar(double progress, gin_helper::Arguments* args) {
  gin_helper::Dictionary options;
  std::string mode;
  args->GetNext(&options) && options.Get("mode", &mode);

  NativeWindow::ProgressState state = NativeWindow::ProgressState::kNormal;
  if (mode == "error")
    state = NativeWindow::ProgressState::kError;
  else if (mode == "paused")
    state = NativeWindow::ProgressState::kPaused;
  else if (mode == "indeterminate")
    state = NativeWindow::ProgressState::kIndeterminate;
  else if (mode == "none")
    state = NativeWindow::ProgressState::kNone;

  window_->SetProgressBar(progress, state);
}

void BaseWindow::SetOverlayIcon(const gfx::Image& overlay,
                                const std::string& description) {
  window_->SetOverlayIcon(overlay, description);
}

void BaseWindow::SetVisibleOnAllWorkspaces(bool visible,
                                           gin_helper::Arguments* args) {
  gin_helper::Dictionary options;
  bool visibleOnFullScreen = false;
  bool skipTransformProcessType = false;
  if (args->GetNext(&options)) {
    options.Get("visibleOnFullScreen", &visibleOnFullScreen);
    options.Get("skipTransformProcessType", &skipTransformProcessType);
  }
  return window_->SetVisibleOnAllWorkspaces(visible, visibleOnFullScreen,
                                            skipTransformProcessType);
}

bool BaseWindow::IsVisibleOnAllWorkspaces() const {
  return window_->IsVisibleOnAllWorkspaces();
}

void BaseWindow::SetAutoHideCursor(bool auto_hide) {
  window_->SetAutoHideCursor(auto_hide);
}

void BaseWindow::SetVibrancy(v8::Isolate* isolate, v8::Local<v8::Value> value) {
  std::string type = gin::V8ToString(isolate, value);
  window_->SetVibrancy(type);
}

void BaseWindow::SetBackgroundMaterial(const std::string& material_type) {
  window_->SetBackgroundMaterial(material_type);
}

#if BUILDFLAG(IS_MAC)
std::string BaseWindow::GetAlwaysOnTopLevel() const {
  return window_->GetAlwaysOnTopLevel();
}

void BaseWindow::SetWindowButtonVisibility(bool visible) {
  window_->SetWindowButtonVisibility(visible);
}

bool BaseWindow::GetWindowButtonVisibility() const {
  return window_->GetWindowButtonVisibility();
}

void BaseWindow::SetWindowButtonPosition(std::optional<gfx::Point> position) {
  window_->SetWindowButtonPosition(std::move(position));
}

std::optional<gfx::Point> BaseWindow::GetWindowButtonPosition() const {
  return window_->GetWindowButtonPosition();
}
#endif

#if BUILDFLAG(IS_MAC)
bool BaseWindow::IsHiddenInMissionControl() {
  return window_->IsHiddenInMissionControl();
}

void BaseWindow::SetHiddenInMissionControl(bool hidden) {
  window_->SetHiddenInMissionControl(hidden);
}
#endif

void BaseWindow::SetTouchBar(
    std::vector<gin_helper::PersistentDictionary> items) {
  window_->SetTouchBar(std::move(items));
}

void BaseWindow::RefreshTouchBarItem(const std::string& item_id) {
  window_->RefreshTouchBarItem(item_id);
}

void BaseWindow::SetEscapeTouchBarItem(gin_helper::PersistentDictionary item) {
  window_->SetEscapeTouchBarItem(std::move(item));
}

void BaseWindow::SelectPreviousTab() {
  window_->SelectPreviousTab();
}

void BaseWindow::SelectNextTab() {
  window_->SelectNextTab();
}

void BaseWindow::ShowAllTabs() {
  window_->ShowAllTabs();
}

void BaseWindow::MergeAllWindows() {
  window_->MergeAllWindows();
}

void BaseWindow::MoveTabToNewWindow() {
  window_->MoveTabToNewWindow();
}

void BaseWindow::ToggleTabBar() {
  window_->ToggleTabBar();
}

void BaseWindow::AddTabbedWindow(NativeWindow* window,
                                 gin_helper::Arguments* args) {
  if (!window_->AddTabbedWindow(window))
    args->ThrowError("AddTabbedWindow cannot be called by a window on itself.");
}

v8::Local<v8::Value> BaseWindow::GetTabbingIdentifier() {
  auto tabbing_id = window_->GetTabbingIdentifier();
  if (!tabbing_id.has_value())
    return v8::Undefined(isolate());

  return gin::ConvertToV8(isolate(), tabbing_id.value());
}

void BaseWindow::SetAutoHideMenuBar(bool auto_hide) {
  window_->SetAutoHideMenuBar(auto_hide);
}

bool BaseWindow::IsMenuBarAutoHide() const {
  return window_->IsMenuBarAutoHide();
}

void BaseWindow::SetMenuBarVisibility(bool visible) {
  window_->SetMenuBarVisibility(visible);
}

bool BaseWindow::IsMenuBarVisible() const {
  return window_->IsMenuBarVisible();
}

void BaseWindow::SetAspectRatio(double aspect_ratio,
                                gin_helper::Arguments* args) {
  gfx::Size extra_size;
  args->GetNext(&extra_size);
  window_->SetAspectRatio(aspect_ratio, extra_size);
}

void BaseWindow::PreviewFile(const std::string& path,
                             gin_helper::Arguments* args) {
  std::string display_name;
  if (!args->GetNext(&display_name))
    display_name = path;
  window_->PreviewFile(path, display_name);
}

void BaseWindow::CloseFilePreview() {
  window_->CloseFilePreview();
}

void BaseWindow::SetGTKDarkThemeEnabled(bool use_dark_theme) {
  window_->SetGTKDarkThemeEnabled(use_dark_theme);
}

v8::Local<v8::Value> BaseWindow::GetContentView() const {
  if (content_view_.IsEmpty())
    return v8::Null(isolate());
  else
    return v8::Local<v8::Value>::New(isolate(), content_view_);
}

v8::Local<v8::Value> BaseWindow::GetParentWindow() const {
  if (parent_window_.IsEmpty())
    return v8::Null(isolate());
  else
    return v8::Local<v8::Value>::New(isolate(), parent_window_);
}

std::vector<v8::Local<v8::Object>> BaseWindow::GetChildWindows() const {
  return child_windows_.Values(isolate());
}

bool BaseWindow::IsModal() const {
  return window_->is_modal();
}

bool BaseWindow::SetThumbarButtons(gin_helper::Arguments* args) {
#if BUILDFLAG(IS_WIN)
  std::vector<TaskbarHost::ThumbarButton> buttons;
  if (!args->GetNext(&buttons)) {
    args->ThrowError();
    return false;
  }
  auto* window = static_cast<NativeWindowViews*>(window_.get());
  return window->taskbar_host().SetThumbarButtons(
      window_->GetAcceleratedWidget(), buttons);
#else
  return false;
#endif
}

#if defined(TOOLKIT_VIEWS)
void BaseWindow::SetIcon(v8::Isolate* isolate, v8::Local<v8::Value> icon) {
  SetIconImpl(isolate, icon, NativeImage::OnConvertError::kThrow);
}

void BaseWindow::SetIconImpl(v8::Isolate* isolate,
                             v8::Local<v8::Value> icon,
                             NativeImage::OnConvertError on_error) {
  NativeImage* native_image = nullptr;
  if (!NativeImage::TryConvertNativeImage(isolate, icon, &native_image,
                                          on_error))
    return;

#if BUILDFLAG(IS_WIN)
  static_cast<NativeWindowViews*>(window_.get())
      ->SetIcon(native_image->GetHICON(GetSystemMetrics(SM_CXSMICON)),
                native_image->GetHICON(GetSystemMetrics(SM_CXICON)));
#elif BUILDFLAG(IS_LINUX)
  static_cast<NativeWindowViews*>(window_.get())
      ->SetIcon(native_image->image().AsImageSkia());
#endif
}
#endif

#if BUILDFLAG(IS_WIN)
bool BaseWindow::HookWindowMessage(UINT message,
                                   const MessageCallback& callback) {
  messages_callback_map_[message] = callback;
  return true;
}

void BaseWindow::UnhookWindowMessage(UINT message) {
  messages_callback_map_.erase(message);
}

bool BaseWindow::IsWindowMessageHooked(UINT message) {
  return base::Contains(messages_callback_map_, message);
}

void BaseWindow::UnhookAllWindowMessages() {
  messages_callback_map_.clear();
}

bool BaseWindow::SetThumbnailClip(const gfx::Rect& region) {
  auto* window = static_cast<NativeWindowViews*>(window_.get());
  return window->taskbar_host().SetThumbnailClip(
      window_->GetAcceleratedWidget(), region);
}

bool BaseWindow::SetThumbnailToolTip(const std::string& tooltip) {
  auto* window = static_cast<NativeWindowViews*>(window_.get());
  return window->taskbar_host().SetThumbnailToolTip(
      window_->GetAcceleratedWidget(), tooltip);
}

void BaseWindow::SetAppDetails(const gin_helper::Dictionary& options) {
  std::wstring app_id;
  base::FilePath app_icon_path;
  int app_icon_index = 0;
  std::wstring relaunch_command;
  std::wstring relaunch_display_name;

  options.Get("appId", &app_id);
  options.Get("appIconPath", &app_icon_path);
  options.Get("appIconIndex", &app_icon_index);
  options.Get("relaunchCommand", &relaunch_command);
  options.Get("relaunchDisplayName", &relaunch_display_name);

  ui::win::SetAppDetailsForWindow(app_id, app_icon_path, app_icon_index,
                                  relaunch_command, relaunch_display_name,
                                  window_->GetAcceleratedWidget());
}

void BaseWindow::SetTitleBarOverlay(const gin_helper::Dictionary& options,
                                    gin_helper::Arguments* args) {
  // Ensure WCO is already enabled on this window
  if (!window_->titlebar_overlay_enabled()) {
    args->ThrowError("Titlebar overlay is not enabled");
    return;
  }

  auto* window = static_cast<NativeWindowViews*>(window_.get());
  bool updated = false;

  // Check and update the button color
  std::string btn_color;
  if (options.Get(options::kOverlayButtonColor, &btn_color)) {
    // Parse the string as a CSS color
    SkColor color;
    if (!content::ParseCssColorString(btn_color, &color)) {
      args->ThrowError("Could not parse color as CSS color");
      return;
    }

    // Update the view
    window->set_overlay_button_color(color);
    updated = true;
  }

  // Check and update the symbol color
  std::string symbol_color;
  if (options.Get(options::kOverlaySymbolColor, &symbol_color)) {
    // Parse the string as a CSS color
    SkColor color;
    if (!content::ParseCssColorString(symbol_color, &color)) {
      args->ThrowError("Could not parse symbol color as CSS color");
      return;
    }

    // Update the view
    window->set_overlay_symbol_color(color);
    updated = true;
  }

  // Check and update the height
  int height = 0;
  if (options.Get(options::kOverlayHeight, &height)) {
    window->set_titlebar_overlay_height(height);
    updated = true;
  }

  // If anything was updated, invalidate the layout and schedule a paint of the
  // window's frame view
  if (updated) {
    auto* frame_view = static_cast<WinFrameView*>(
        window->widget()->non_client_view()->frame_view());
    frame_view->InvalidateCaptionButtons();
  }
}
#endif

int32_t BaseWindow::GetID() const {
  return weak_map_id();
}

void BaseWindow::RemoveFromParentChildWindows() {
  if (parent_window_.IsEmpty())
    return;

  gin::Handle<BaseWindow> parent;
  if (!gin::ConvertFromV8(isolate(), GetParentWindow(), &parent) ||
      parent.IsEmpty()) {
    return;
  }

  parent->child_windows_.Remove(weak_map_id());
}

// static
gin_helper::WrappableBase* BaseWindow::New(gin_helper::Arguments* args) {
  auto options = gin_helper::Dictionary::CreateEmpty(args->isolate());
  args->GetNext(&options);

  return new BaseWindow(args, options);
}

// static
void BaseWindow::BuildPrototype(v8::Isolate* isolate,
                                v8::Local<v8::FunctionTemplate> prototype) {
  prototype->SetClassName(gin::StringToV8(isolate, "BaseWindow"));
  gin_helper::Destroyable::MakeDestroyable(isolate, prototype);
  gin_helper::ObjectTemplateBuilder(isolate, prototype->PrototypeTemplate())
      .SetMethod("setContentView", &BaseWindow::SetContentView)
      .SetMethod("close", &BaseWindow::Close)
      .SetMethod("focus", &BaseWindow::Focus)
      .SetMethod("blur", &BaseWindow::Blur)
      .SetMethod("isFocused", &BaseWindow::IsFocused)
      .SetMethod("show", &BaseWindow::Show)
      .SetMethod("showInactive", &BaseWindow::ShowInactive)
      .SetMethod("hide", &BaseWindow::Hide)
      .SetMethod("isVisible", &BaseWindow::IsVisible)
      .SetMethod("isEnabled", &BaseWindow::IsEnabled)
      .SetMethod("setEnabled", &BaseWindow::SetEnabled)
      .SetMethod("maximize", &BaseWindow::Maximize)
      .SetMethod("unmaximize", &BaseWindow::Unmaximize)
      .SetMethod("isMaximized", &BaseWindow::IsMaximized)
      .SetMethod("minimize", &BaseWindow::Minimize)
      .SetMethod("restore", &BaseWindow::Restore)
      .SetMethod("isMinimized", &BaseWindow::IsMinimized)
      .SetMethod("setFullScreen", &BaseWindow::SetFullScreen)
      .SetMethod("isFullScreen", &BaseWindow::IsFullscreen)
      .SetMethod("setBounds", &BaseWindow::SetBounds)
      .SetMethod("getBounds", &BaseWindow::GetBounds)
      .SetMethod("isNormal", &BaseWindow::IsNormal)
      .SetMethod("getNormalBounds", &BaseWindow::GetNormalBounds)
      .SetMethod("setSize", &BaseWindow::SetSize)
      .SetMethod("getSize", &BaseWindow::GetSize)
      .SetMethod("setContentBounds", &BaseWindow::SetContentBounds)
      .SetMethod("getContentBounds", &BaseWindow::GetContentBounds)
      .SetMethod("setContentSize", &BaseWindow::SetContentSize)
      .SetMethod("getContentSize", &BaseWindow::GetContentSize)
      .SetMethod("setMinimumSize", &BaseWindow::SetMinimumSize)
      .SetMethod("getMinimumSize", &BaseWindow::GetMinimumSize)
      .SetMethod("setMaximumSize", &BaseWindow::SetMaximumSize)
      .SetMethod("getMaximumSize", &BaseWindow::GetMaximumSize)
      .SetMethod("setSheetOffset", &BaseWindow::SetSheetOffset)
      .SetMethod("moveAbove", &BaseWindow::MoveAbove)
      .SetMethod("moveTop", &BaseWindow::MoveTop)
      .SetMethod("setResizable", &BaseWindow::SetResizable)
      .SetMethod("isResizable", &BaseWindow::IsResizable)
      .SetMethod("setMovable", &BaseWindow::SetMovable)
      .SetMethod("isMovable", &BaseWindow::IsMovable)
      .SetMethod("setMinimizable", &BaseWindow::SetMinimizable)
      .SetMethod("isMinimizable", &BaseWindow::IsMinimizable)
      .SetMethod("setMaximizable", &BaseWindow::SetMaximizable)
      .SetMethod("isMaximizable", &BaseWindow::IsMaximizable)
      .SetMethod("setFullScreenable", &BaseWindow::SetFullScreenable)
      .SetMethod("isFullScreenable", &BaseWindow::IsFullScreenable)
      .SetMethod("setClosable", &BaseWindow::SetClosable)
      .SetMethod("isClosable", &BaseWindow::IsClosable)
      .SetMethod("setAlwaysOnTop", &BaseWindow::SetAlwaysOnTop)
      .SetMethod("isAlwaysOnTop", &BaseWindow::IsAlwaysOnTop)
      .SetMethod("center", &BaseWindow::Center)
      .SetMethod("setPosition", &BaseWindow::SetPosition)
      .SetMethod("getPosition", &BaseWindow::GetPosition)
      .SetMethod("setTitle", &BaseWindow::SetTitle)
      .SetMethod("getTitle", &BaseWindow::GetTitle)
      .SetProperty("accessibleTitle", &BaseWindow::GetAccessibleTitle,
                   &BaseWindow::SetAccessibleTitle)
      .SetMethod("flashFrame", &BaseWindow::FlashFrame)
      .SetMethod("setSkipTaskbar", &BaseWindow::SetSkipTaskbar)
      .SetMethod("setSimpleFullScreen", &BaseWindow::SetSimpleFullScreen)
      .SetMethod("isSimpleFullScreen", &BaseWindow::IsSimpleFullScreen)
      .SetMethod("setKiosk", &BaseWindow::SetKiosk)
      .SetMethod("isKiosk", &BaseWindow::IsKiosk)
      .SetMethod("isTabletMode", &BaseWindow::IsTabletMode)
      .SetMethod("setBackgroundColor", &BaseWindow::SetBackgroundColor)
      .SetMethod("getBackgroundColor", &BaseWindow::GetBackgroundColor)
      .SetMethod("setHasShadow", &BaseWindow::SetHasShadow)
      .SetMethod("hasShadow", &BaseWindow::HasShadow)
      .SetMethod("setOpacity", &BaseWindow::SetOpacity)
      .SetMethod("getOpacity", &BaseWindow::GetOpacity)
      .SetMethod("setShape", &BaseWindow::SetShape)
      .SetMethod("setRepresentedFilename", &BaseWindow::SetRepresentedFilename)
      .SetMethod("getRepresentedFilename", &BaseWindow::GetRepresentedFilename)
      .SetMethod("setDocumentEdited", &BaseWindow::SetDocumentEdited)
      .SetMethod("isDocumentEdited", &BaseWindow::IsDocumentEdited)
      .SetMethod("setIgnoreMouseEvents", &BaseWindow::SetIgnoreMouseEvents)
      .SetMethod("setContentProtection", &BaseWindow::SetContentProtection)
      .SetMethod("setFocusable", &BaseWindow::SetFocusable)
      .SetMethod("isFocusable", &BaseWindow::IsFocusable)
      .SetMethod("setMenu", &BaseWindow::SetMenu)
      .SetMethod("removeMenu", &BaseWindow::RemoveMenu)
      .SetMethod("setParentWindow", &BaseWindow::SetParentWindow)
      .SetMethod("getMediaSourceId", &BaseWindow::GetMediaSourceId)
      .SetMethod("getNativeWindowHandle", &BaseWindow::GetNativeWindowHandle)
      .SetMethod("setProgressBar", &BaseWindow::SetProgressBar)
      .SetMethod("setOverlayIcon", &BaseWindow::SetOverlayIcon)
      .SetMethod("setVisibleOnAllWorkspaces",
                 &BaseWindow::SetVisibleOnAllWorkspaces)
      .SetMethod("isVisibleOnAllWorkspaces",
                 &BaseWindow::IsVisibleOnAllWorkspaces)
#if BUILDFLAG(IS_MAC)
      .SetMethod("invalidateShadow", &BaseWindow::InvalidateShadow)
      .SetMethod("_getAlwaysOnTopLevel", &BaseWindow::GetAlwaysOnTopLevel)
      .SetMethod("setAutoHideCursor", &BaseWindow::SetAutoHideCursor)
#endif
      .SetMethod("setVibrancy", &BaseWindow::SetVibrancy)
      .SetMethod("setBackgroundMaterial", &BaseWindow::SetBackgroundMaterial)

#if BUILDFLAG(IS_MAC)
      .SetMethod("isHiddenInMissionControl",
                 &BaseWindow::IsHiddenInMissionControl)
      .SetMethod("setHiddenInMissionControl",
                 &BaseWindow::SetHiddenInMissionControl)
#endif

      .SetMethod("_setTouchBarItems", &BaseWindow::SetTouchBar)
      .SetMethod("_refreshTouchBarItem", &BaseWindow::RefreshTouchBarItem)
      .SetMethod("_setEscapeTouchBarItem", &BaseWindow::SetEscapeTouchBarItem)
#if BUILDFLAG(IS_MAC)
      .SetMethod("selectPreviousTab", &BaseWindow::SelectPreviousTab)
      .SetMethod("selectNextTab", &BaseWindow::SelectNextTab)
      .SetMethod("showAllTabs", &BaseWindow::ShowAllTabs)
      .SetMethod("mergeAllWindows", &BaseWindow::MergeAllWindows)
      .SetMethod("moveTabToNewWindow", &BaseWindow::MoveTabToNewWindow)
      .SetMethod("toggleTabBar", &BaseWindow::ToggleTabBar)
      .SetMethod("addTabbedWindow", &BaseWindow::AddTabbedWindow)
      .SetProperty("tabbingIdentifier", &BaseWindow::GetTabbingIdentifier)
      .SetMethod("setWindowButtonVisibility",
                 &BaseWindow::SetWindowButtonVisibility)
      .SetMethod("_getWindowButtonVisibility",
                 &BaseWindow::GetWindowButtonVisibility)
      .SetMethod("setWindowButtonPosition",
                 &BaseWindow::SetWindowButtonPosition)
      .SetMethod("getWindowButtonPosition",
                 &BaseWindow::GetWindowButtonPosition)
      .SetProperty("excludedFromShownWindowsMenu",
                   &BaseWindow::IsExcludedFromShownWindowsMenu,
                   &BaseWindow::SetExcludedFromShownWindowsMenu)
#endif
      .SetMethod("setAutoHideMenuBar", &BaseWindow::SetAutoHideMenuBar)
      .SetMethod("isMenuBarAutoHide", &BaseWindow::IsMenuBarAutoHide)
      .SetMethod("setMenuBarVisibility", &BaseWindow::SetMenuBarVisibility)
      .SetMethod("isMenuBarVisible", &BaseWindow::IsMenuBarVisible)
      .SetMethod("setAspectRatio", &BaseWindow::SetAspectRatio)
      .SetMethod("previewFile", &BaseWindow::PreviewFile)
      .SetMethod("closeFilePreview", &BaseWindow::CloseFilePreview)
      .SetMethod("getContentView", &BaseWindow::GetContentView)
      .SetProperty("contentView", &BaseWindow::GetContentView,
                   &BaseWindow::SetContentView)
      .SetMethod("getParentWindow", &BaseWindow::GetParentWindow)
      .SetMethod("getChildWindows", &BaseWindow::GetChildWindows)
      .SetMethod("isModal", &BaseWindow::IsModal)
      .SetMethod("setThumbarButtons", &BaseWindow::SetThumbarButtons)
#if defined(TOOLKIT_VIEWS)
      .SetMethod("setIcon", &BaseWindow::SetIcon)
#endif
#if BUILDFLAG(IS_WIN)
      .SetMethod("hookWindowMessage", &BaseWindow::HookWindowMessage)
      .SetMethod("isWindowMessageHooked", &BaseWindow::IsWindowMessageHooked)
      .SetMethod("unhookWindowMessage", &BaseWindow::UnhookWindowMessage)
      .SetMethod("unhookAllWindowMessages",
                 &BaseWindow::UnhookAllWindowMessages)
      .SetMethod("setThumbnailClip", &BaseWindow::SetThumbnailClip)
      .SetMethod("setThumbnailToolTip", &BaseWindow::SetThumbnailToolTip)
      .SetMethod("setAppDetails", &BaseWindow::SetAppDetails)
      .SetMethod("setTitleBarOverlay", &BaseWindow::SetTitleBarOverlay)
#endif
      .SetProperty("id", &BaseWindow::GetID);
}

}  // namespace electron::api

namespace {

using electron::api::BaseWindow;

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  BaseWindow::SetConstructor(isolate, base::BindRepeating(&BaseWindow::New));

  gin_helper::Dictionary constructor(isolate,
                                     BaseWindow::GetConstructor(isolate)
                                         ->GetFunction(context)
                                         .ToLocalChecked());
  constructor.SetMethod("fromId", &BaseWindow::FromWeakMapID);
  constructor.SetMethod("getAllWindows", &BaseWindow::GetAll);

  gin_helper::Dictionary dict(isolate, exports);
  dict.Set("BaseWindow", constructor);
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_browser_base_window, Initialize)
