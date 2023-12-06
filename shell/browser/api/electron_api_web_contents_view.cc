// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_web_contents_view.h"

#include "base/no_destructor.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/browser.h"
#include "shell/browser/native_window.h"
#include "shell/browser/ui/inspectable_web_contents_view.h"
#include "shell/browser/web_contents_preferences.h"
#include "shell/common/gin_converters/gfx_converter.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/constructor.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/object_template_builder.h"
#include "shell/common/node_includes.h"
#include "shell/common/options_switches.h"
#include "third_party/skia/include/core/SkRegion.h"
#include "ui/base/hit_test.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

#if BUILDFLAG(IS_MAC)
#include "shell/browser/ui/cocoa/delayed_native_view_host.h"
#endif

namespace electron::api {

WebContentsView::WebContentsView(v8::Isolate* isolate,
                                 gin::Handle<WebContents> web_contents)
#if BUILDFLAG(IS_MAC)
    : View(new DelayedNativeViewHost(web_contents->inspectable_web_contents()
                                         ->GetView()
                                         ->GetNativeView())),
#else
    : View(web_contents->inspectable_web_contents()->GetView()->GetView()),
#endif
      web_contents_(isolate, web_contents.ToV8()),
      api_web_contents_(web_contents.get()) {
#if !BUILDFLAG(IS_MAC)
  // On macOS the View is a newly-created |DelayedNativeViewHost| and it is our
  // responsibility to delete it. On other platforms the View is created and
  // managed by InspectableWebContents.
  set_delete_view(false);
#endif
  view()->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToMinimum,
                               views::MaximumFlexSizeRule::kUnbounded));
  Observe(web_contents->web_contents());
}

WebContentsView::~WebContentsView() {
  if (api_web_contents_)  // destroy() called without closing WebContents
    api_web_contents_->Destroy();
}

gin::Handle<WebContents> WebContentsView::GetWebContents(v8::Isolate* isolate) {
  if (api_web_contents_)
    return gin::CreateHandle(isolate, api_web_contents_.get());
  else
    return gin::Handle<WebContents>();
}

void WebContentsView::SetBackgroundColor(absl::optional<WrappedSkColor> color) {
  View::SetBackgroundColor(color);
  if (api_web_contents_) {
    api_web_contents_->SetBackgroundColor(color);
    // Also update the web preferences object otherwise the view will be reset
    // on the next load URL call
    auto* web_preferences =
        WebContentsPreferences::From(api_web_contents_->web_contents());
    if (web_preferences) {
      web_preferences->SetBackgroundColor(color);
    }
  }
}

int WebContentsView::NonClientHitTest(const gfx::Point& point) {
  gfx::Point local_point(point);
  views::View::ConvertPointFromWidget(view(), &local_point);
  SkRegion* region = api_web_contents_->draggable_region();
  if (region && region->contains(local_point.x(), local_point.y()))
    return HTCAPTION;
  return HTNOWHERE;
}

void WebContentsView::WebContentsDestroyed() {
  api_web_contents_ = nullptr;
  web_contents_.Reset();
}

void WebContentsView::OnViewAddedToWidget(views::View* observed_view) {
  DCHECK_EQ(observed_view, view());
  views::Widget* widget = view()->GetWidget();
  auto* native_window = static_cast<NativeWindow*>(
      widget->GetNativeWindowProperty(electron::kElectronNativeWindowKey));
  if (!native_window)
    return;
  native_window->AddDraggableRegionProvider(this);
}

void WebContentsView::OnViewRemovedFromWidget(views::View* observed_view) {
  DCHECK_EQ(observed_view, view());
  views::Widget* widget = view()->GetWidget();
  auto* native_window = static_cast<NativeWindow*>(
      widget->GetNativeWindowProperty(kElectronNativeWindowKey));
  if (!native_window)
    return;
  native_window->RemoveDraggableRegionProvider(this);
}

// static
gin::Handle<WebContentsView> WebContentsView::Create(
    v8::Isolate* isolate,
    const gin_helper::Dictionary& web_preferences) {
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Value> arg = gin::ConvertToV8(isolate, web_preferences);
  v8::Local<v8::Object> obj;
  if (!web_preferences.Has(options::kShow))
    gin::Dictionary(isolate, arg.As<v8::Object>()).Set(options::kShow, true);
  if (GetConstructor(isolate)->NewInstance(context, 1, &arg).ToLocal(&obj)) {
    gin::Handle<WebContentsView> web_contents_view;
    if (gin::ConvertFromV8(isolate, obj, &web_contents_view))
      return web_contents_view;
  }
  return gin::Handle<WebContentsView>();
}

// static
v8::Local<v8::Function> WebContentsView::GetConstructor(v8::Isolate* isolate) {
  static base::NoDestructor<v8::Global<v8::Function>> constructor;
  if (constructor.get()->IsEmpty()) {
    constructor->Reset(
        isolate, gin_helper::CreateConstructor<WebContentsView>(
                     isolate, base::BindRepeating(&WebContentsView::New)));
  }
  return v8::Local<v8::Function>::New(isolate, *constructor.get());
}

// static
gin_helper::WrappableBase* WebContentsView::New(gin_helper::Arguments* args) {
  gin_helper::Dictionary web_preferences;
  {
    v8::Local<v8::Value> options_value;
    if (args->GetNext(&options_value)) {
      gin_helper::Dictionary options;
      if (!gin::ConvertFromV8(args->isolate(), options_value, &options)) {
        args->ThrowError("options must be an object");
        return nullptr;
      }
      v8::Local<v8::Value> web_preferences_value;
      if (options.Get("webPreferences", &web_preferences_value)) {
        if (!gin::ConvertFromV8(args->isolate(), web_preferences_value,
                                &web_preferences)) {
          args->ThrowError("options.webPreferences must be an object");
          return nullptr;
        }
      }
    }
  }
  if (web_preferences.IsEmpty())
    web_preferences = gin_helper::Dictionary::CreateEmpty(args->isolate());
  if (!web_preferences.Has(options::kShow))
    web_preferences.Set(options::kShow, false);
  auto web_contents =
      WebContents::CreateFromWebPreferences(args->isolate(), web_preferences);

  // Constructor call.
  auto* view = new WebContentsView(args->isolate(), web_contents);
  view->InitWithArgs(args);
  return view;
}

// static
void WebContentsView::BuildPrototype(
    v8::Isolate* isolate,
    v8::Local<v8::FunctionTemplate> prototype) {
  prototype->SetClassName(gin::StringToV8(isolate, "WebContentsView"));
  gin_helper::ObjectTemplateBuilder(isolate, prototype->PrototypeTemplate())
      .SetMethod("setBackgroundColor", &WebContentsView::SetBackgroundColor)
      .SetProperty("webContents", &WebContentsView::GetWebContents);
}

}  // namespace electron::api

namespace {

using electron::api::WebContentsView;

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary dict(isolate, exports);
  dict.Set("WebContentsView", WebContentsView::GetConstructor(isolate));
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_browser_web_contents_view,
                                  Initialize)
