// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/osr/osr_web_contents_view.h"

#include "content/browser/web_contents/web_contents_impl.h"  // nogncheck
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "shell/browser/native_window.h"
#include "ui/display/screen.h"
#include "ui/display/screen_info.h"

namespace electron {

OffScreenWebContentsView::OffScreenWebContentsView(
    bool transparent,
    const OnPaintCallback& callback)
    : transparent_(transparent), callback_(callback) {
#if BUILDFLAG(IS_MAC)
  PlatformCreate();
#endif
}

OffScreenWebContentsView::~OffScreenWebContentsView() {
  if (native_window_)
    native_window_->RemoveObserver(this);

#if BUILDFLAG(IS_MAC)
  PlatformDestroy();
#endif
}

void OffScreenWebContentsView::SetWebContents(
    content::WebContents* web_contents) {
  web_contents_ = web_contents;

  if (auto* view = GetView())
    view->InstallTransparency();
}

void OffScreenWebContentsView::SetNativeWindow(NativeWindow* window) {
  if (native_window_)
    native_window_->RemoveObserver(this);

  native_window_ = window;

  if (native_window_)
    native_window_->AddObserver(this);

  OnWindowResize();
}

void OffScreenWebContentsView::OnWindowResize() {
  // In offscreen mode call RenderWidgetHostView's SetSize explicitly
  if (auto* view = GetView())
    view->SetSize(GetSize());
}

void OffScreenWebContentsView::OnWindowClosed() {
  if (native_window_) {
    native_window_->RemoveObserver(this);
    native_window_ = nullptr;
  }
}

gfx::Size OffScreenWebContentsView::GetSize() {
  return native_window_ ? native_window_->GetSize() : gfx::Size();
}

#if !BUILDFLAG(IS_MAC)
gfx::NativeView OffScreenWebContentsView::GetNativeView() const {
  if (!native_window_)
    return gfx::NativeView();
  return native_window_->GetNativeView();
}

gfx::NativeView OffScreenWebContentsView::GetContentNativeView() const {
  if (!native_window_)
    return gfx::NativeView();
  return native_window_->GetNativeView();
}

gfx::NativeWindow OffScreenWebContentsView::GetTopLevelNativeWindow() const {
  if (!native_window_)
    return gfx::NativeWindow();
  return native_window_->GetNativeWindow();
}
#endif

gfx::Rect OffScreenWebContentsView::GetContainerBounds() const {
  return GetViewBounds();
}

content::DropData* OffScreenWebContentsView::GetDropData() const {
  return nullptr;
}

gfx::Rect OffScreenWebContentsView::GetViewBounds() const {
  if (auto* view = GetView())
    return view->GetViewBounds();
  return {};
}

content::RenderWidgetHostViewBase*
OffScreenWebContentsView::CreateViewForWidget(
    content::RenderWidgetHost* render_widget_host) {
  if (auto* rwhv = render_widget_host->GetView())
    return static_cast<content::RenderWidgetHostViewBase*>(rwhv);

  return new OffScreenRenderWidgetHostView(
      transparent_, painting_, GetFrameRate(), callback_, render_widget_host,
      nullptr, GetSize());
}

content::RenderWidgetHostViewBase*
OffScreenWebContentsView::CreateViewForChildWidget(
    content::RenderWidgetHost* render_widget_host) {
  auto* web_contents_impl =
      static_cast<content::WebContentsImpl*>(web_contents_);

  auto* view = static_cast<OffScreenRenderWidgetHostView*>(
      web_contents_impl->GetOuterWebContents()
          ? web_contents_impl->GetOuterWebContents()->GetRenderWidgetHostView()
          : web_contents_impl->GetRenderWidgetHostView());

  return new OffScreenRenderWidgetHostView(transparent_, painting_,
                                           view->frame_rate(), callback_,
                                           render_widget_host, view, GetSize());
}

void OffScreenWebContentsView::RenderViewReady() {
  if (auto* view = GetView())
    view->InstallTransparency();
}

#if BUILDFLAG(IS_MAC)
bool OffScreenWebContentsView::CloseTabAfterEventTrackingIfNeeded() {
  return false;
}
#endif  // BUILDFLAG(IS_MAC)

void OffScreenWebContentsView::StartDragging(
    const content::DropData& drop_data,
    const url::Origin& source_origin,
    blink::DragOperationsMask allowed_ops,
    const gfx::ImageSkia& image,
    const gfx::Vector2d& cursor_offset,
    const gfx::Rect& drag_obj_rect,
    const blink::mojom::DragEventSourceInfo& event_info,
    content::RenderWidgetHostImpl* source_rwh) {
  if (web_contents_)
    static_cast<content::WebContentsImpl*>(web_contents_)
        ->SystemDragEnded(source_rwh);
}

void OffScreenWebContentsView::SetPainting(bool painting) {
  painting_ = painting;
  if (auto* view = GetView())
    view->SetPainting(painting);
}

bool OffScreenWebContentsView::IsPainting() const {
  if (auto* view = GetView())
    return view->is_painting();
  return painting_;
}

void OffScreenWebContentsView::SetFrameRate(int frame_rate) {
  frame_rate_ = frame_rate;
  if (auto* view = GetView())
    view->SetFrameRate(frame_rate);
}

int OffScreenWebContentsView::GetFrameRate() const {
  if (auto* view = GetView())
    return view->frame_rate();
  return frame_rate_;
}

OffScreenRenderWidgetHostView* OffScreenWebContentsView::GetView() const {
  if (web_contents_) {
    return static_cast<OffScreenRenderWidgetHostView*>(
        web_contents_->GetRenderViewHost()->GetWidget()->GetView());
  }
  return nullptr;
}

content::BackForwardTransitionAnimationManager*
OffScreenWebContentsView::GetBackForwardTransitionAnimationManager() {
  return nullptr;
}

}  // namespace electron
