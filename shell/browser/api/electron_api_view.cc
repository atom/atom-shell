// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_view.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "ash/style/rounded_rect_cutout_path_builder.h"
#include "gin/data_object_builder.h"
#include "gin/handle.h"
#include "gin/wrappable.h"
#include "shell/browser/javascript_environment.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/gfx_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/object_template_builder.h"
#include "shell/common/node_includes.h"
#include "ui/views/background.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/layout_manager_base.h"

#if BUILDFLAG(IS_MAC)
#include "shell/browser/animation_util.h"
#endif

namespace gin {

template <>
struct Converter<views::ChildLayout> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     views::ChildLayout* out) {
    gin_helper::Dictionary dict;
    if (!gin::ConvertFromV8(isolate, val, &dict))
      return false;
    gin::Handle<electron::api::View> view;
    if (!dict.Get("view", &view))
      return false;
    out->child_view = view->view();
    if (dict.Has("bounds"))
      dict.Get("bounds", &out->bounds);
    out->visible = true;
    if (dict.Has("visible"))
      dict.Get("visible", &out->visible);
    return true;
  }
};

template <>
struct Converter<views::ProposedLayout> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     views::ProposedLayout* out) {
    gin_helper::Dictionary dict;
    if (!gin::ConvertFromV8(isolate, val, &dict))
      return false;
    if (!dict.Get("size", &out->host_size))
      return false;
    if (!dict.Get("layouts", &out->child_layouts))
      return false;
    return true;
  }
};

template <>
struct Converter<views::LayoutOrientation> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     views::LayoutOrientation* out) {
    std::string orientation = base::ToLowerASCII(gin::V8ToString(isolate, val));
    if (orientation == "horizontal") {
      *out = views::LayoutOrientation::kHorizontal;
    } else if (orientation == "vertical") {
      *out = views::LayoutOrientation::kVertical;
    } else {
      return false;
    }
    return true;
  }
};

template <>
struct Converter<views::LayoutAlignment> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     views::LayoutAlignment* out) {
    std::string orientation = base::ToLowerASCII(gin::V8ToString(isolate, val));
    if (orientation == "start") {
      *out = views::LayoutAlignment::kStart;
    } else if (orientation == "center") {
      *out = views::LayoutAlignment::kCenter;
    } else if (orientation == "end") {
      *out = views::LayoutAlignment::kEnd;
    } else if (orientation == "stretch") {
      *out = views::LayoutAlignment::kStretch;
    } else if (orientation == "baseline") {
      *out = views::LayoutAlignment::kBaseline;
    } else {
      return false;
    }
    return true;
  }
};

template <>
struct Converter<views::FlexAllocationOrder> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     views::FlexAllocationOrder* out) {
    std::string orientation = base::ToLowerASCII(gin::V8ToString(isolate, val));
    if (orientation == "normal") {
      *out = views::FlexAllocationOrder::kNormal;
    } else if (orientation == "reverse") {
      *out = views::FlexAllocationOrder::kReverse;
    } else {
      return false;
    }
    return true;
  }
};

template <>
struct Converter<views::SizeBound> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   const views::SizeBound& in) {
    if (in.is_bounded())
      return v8::Integer::New(isolate, in.value());
    return v8::Number::New(isolate, std::numeric_limits<double>::infinity());
  }
};

template <>
struct Converter<views::SizeBounds> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   const views::SizeBounds& in) {
    return gin::DataObjectBuilder(isolate)
        .Set("width", in.width())
        .Set("height", in.height())
        .Build();
  }
};
}  // namespace gin

namespace electron::api {

using LayoutCallback = base::RepeatingCallback<views::ProposedLayout(
    const views::SizeBounds& size_bounds)>;

class JSLayoutManager : public views::LayoutManagerBase {
 public:
  explicit JSLayoutManager(LayoutCallback layout_callback)
      : layout_callback_(std::move(layout_callback)) {}
  ~JSLayoutManager() override {}

  // views::LayoutManagerBase
  views::ProposedLayout CalculateProposedLayout(
      const views::SizeBounds& size_bounds) const override {
    v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    v8::HandleScope handle_scope(isolate);
    return layout_callback_.Run(size_bounds);
  }

 private:
  LayoutCallback layout_callback_;
};

View::View(views::View* view) : view_(view) {
  view_->set_owned_by_client();
  view_->AddObserver(this);
}

View::View() : View(new views::View()) {}

View::~View() {
  if (!view_)
    return;
  view_->RemoveObserver(this);
  if (delete_view_)
    view_.ClearAndDelete();
}

void View::ReorderChildView(gin::Handle<View> child, size_t index) {
  view_->ReorderChildView(child->view(), index);

  const auto i =
      std::ranges::find_if(child_views_, [&](const ChildPair& child_view) {
        return child_view.first == child->view();
      });
  DCHECK(i != child_views_.end());

  // If |view| is already at the desired position, there's nothing to do.
  const auto pos = std::next(
      child_views_.begin(),
      static_cast<ptrdiff_t>(std::min(index, child_views_.size() - 1)));
  if (i == pos)
    return;

  if (pos < i) {
    std::rotate(pos, i, std::next(i));
  } else {
    std::rotate(i, std::next(i), std::next(pos));
  }
}

void View::AddChildViewAt(gin::Handle<View> child,
                          std::optional<size_t> maybe_index) {
  // TODO(nornagon): !view_ is only for supporting the weird case of
  // WebContentsView's view being deleted when the underlying WebContents is
  // destroyed (on non-Mac). We should fix that so that WebContentsView always
  // has a View, possibly a wrapper view around the underlying platform View.
  if (!view_)
    return;

  // This will CHECK and crash in View::AddChildViewAtImpl if not handled here.
  if (view_ == child->view()) {
    gin_helper::ErrorThrower(isolate()).ThrowError(
        "A view cannot be added as its own child");
    return;
  }

  size_t index =
      std::min(child_views_.size(), maybe_index.value_or(child_views_.size()));

  // If the child is already a child of this view, just reorder it.
  // This matches the behavior of View::AddChildViewAtImpl and
  // otherwise will CHECK if the same view is added multiple times.
  if (child->view()->parent() == view_) {
    ReorderChildView(child, index);
    return;
  }

  child_views_.emplace(child_views_.begin() + index,  // index
                       child->view(),
                       v8::Global<v8::Object>(isolate(), child->GetWrapper()));
#if BUILDFLAG(IS_MAC)
  // Disable the implicit CALayer animations that happen by default when adding
  // or removing sublayers.
  // See
  // https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/ReactingtoLayerChanges/ReactingtoLayerChanges.html
  // and https://github.com/electron/electron/pull/14911
  // TODO(nornagon): Disabling these CALayer animations (which are specific to
  // WebContentsView, I think) seems like this is something that remote_cocoa
  // or views should be taking care of, but isn't. This should be pushed
  // upstream.
  ScopedCAActionDisabler disable_animations;
#endif
  view_->AddChildViewAt(child->view(), index);
}

void View::RemoveChildView(gin::Handle<View> child) {
  if (!view_)
    return;

  const auto it =
      std::ranges::find_if(child_views_, [&](const ChildPair& child_view) {
        return child_view.first == child->view();
      });
  if (it != child_views_.end()) {
#if BUILDFLAG(IS_MAC)
    ScopedCAActionDisabler disable_animations;
#endif
    // Remove from child_views first so that OnChildViewRemoved doesn't try to
    // remove it again
    child_views_.erase(it);
    // It's possible for the child's view to be invalid here
    // if the child's webContents was closed or destroyed.
    if (child->view())
      view_->RemoveChildView(child->view());
  }
}

void View::SetBounds(const gfx::Rect& bounds) {
  if (!view_)
    return;
  view_->SetBoundsRect(bounds);
}

gfx::Rect View::GetBounds() {
  if (!view_)
    return {};
  return view_->bounds();
}

void View::SetLayout(v8::Isolate* isolate, v8::Local<v8::Object> value) {
  if (!view_)
    return;
  gin_helper::Dictionary dict(isolate, value);
  LayoutCallback calculate_proposed_layout;
  if (dict.Get("calculateProposedLayout", &calculate_proposed_layout)) {
    view_->SetLayoutManager(std::make_unique<JSLayoutManager>(
        std::move(calculate_proposed_layout)));
  } else {
    auto* layout =
        view_->SetLayoutManager(std::make_unique<views::FlexLayout>());
    views::LayoutOrientation orientation;
    if (dict.Get("orientation", &orientation))
      layout->SetOrientation(orientation);
    views::LayoutAlignment main_axis_alignment;
    if (dict.Get("mainAxisAlignment", &main_axis_alignment))
      layout->SetMainAxisAlignment(main_axis_alignment);
    views::LayoutAlignment cross_axis_alignment;
    if (dict.Get("crossAxisAlignment", &cross_axis_alignment))
      layout->SetCrossAxisAlignment(cross_axis_alignment);
    gfx::Insets interior_margin;
    if (dict.Get("interiorMargin", &interior_margin))
      layout->SetInteriorMargin(interior_margin);
    int minimum_cross_axis_size;
    if (dict.Get("minimumCrossAxisSize", &minimum_cross_axis_size))
      layout->SetMinimumCrossAxisSize(minimum_cross_axis_size);
    bool collapse_margins;
    if (dict.Has("collapseMargins") &&
        dict.Get("collapseMargins", &collapse_margins))
      layout->SetCollapseMargins(collapse_margins);
    bool include_host_insets_in_layout;
    if (dict.Has("includeHostInsetsInLayout") &&
        dict.Get("includeHostInsetsInLayout", &include_host_insets_in_layout))
      layout->SetIncludeHostInsetsInLayout(include_host_insets_in_layout);
    bool ignore_default_main_axis_margins;
    if (dict.Has("ignoreDefaultMainAxisMargins") &&
        dict.Get("ignoreDefaultMainAxisMargins",
                 &ignore_default_main_axis_margins))
      layout->SetIgnoreDefaultMainAxisMargins(ignore_default_main_axis_margins);
    views::FlexAllocationOrder flex_allocation_order;
    if (dict.Get("flexAllocationOrder", &flex_allocation_order))
      layout->SetFlexAllocationOrder(flex_allocation_order);
  }
}

std::vector<v8::Local<v8::Value>> View::GetChildren() {
  std::vector<v8::Local<v8::Value>> ret;
  ret.reserve(child_views_.size());

  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();

  for (auto& [view, global] : child_views_)
    ret.push_back(global.Get(isolate));

  return ret;
}

void View::SetBackgroundColor(std::optional<WrappedSkColor> color) {
  if (!view_)
    return;
  view_->SetBackground(color ? views::CreateSolidBackground({*color})
                             : nullptr);
}

void View::SetBorderRadius(int radius) {
  border_radius_ = radius;
  ApplyBorderRadius();
}

void View::ApplyBorderRadius() {
  if (!border_radius_.has_value() || !view_)
    return;

  auto size = view_->bounds().size();

  // Restrict border radius to the constraints set in the path builder class.
  // If the constraints are exceeded, the builder will crash.
  int radius;
  {
    float r = border_radius_.value() * 1.f;
    r = std::min(r, size.width() / 2.f);
    r = std::min(r, size.height() / 2.f);
    r = std::max(r, 0.f);
    radius = std::floor(r);
  }

  // RoundedRectCutoutPathBuilder has a minimum size of 32 x 32.
  if (radius > 0 && size.width() >= 32 && size.height() >= 32) {
    auto builder = ash::RoundedRectCutoutPathBuilder(gfx::SizeF(size));
    builder.CornerRadius(radius);
    view_->SetClipPath(builder.Build());
  } else {
    view_->SetClipPath(SkPath());
  }
}

void View::SetVisible(bool visible) {
  if (!view_)
    return;
  view_->SetVisible(visible);
}

bool View::GetVisible() const {
  return view_ ? view_->GetVisible() : false;
}

void View::OnViewBoundsChanged(views::View* observed_view) {
  ApplyBorderRadius();
  Emit("bounds-changed");
}

void View::OnViewIsDeleting(views::View* observed_view) {
  DCHECK_EQ(observed_view, view_);
  view_ = nullptr;
}

void View::OnChildViewRemoved(views::View* observed_view, views::View* child) {
  std::erase_if(child_views_, [child](const ChildPair& child_view) {
    return child_view.first == child;
  });
}

// static
gin_helper::WrappableBase* View::New(gin::Arguments* args) {
  View* view = new View();
  view->InitWithArgs(args);
  return view;
}

// static
v8::Local<v8::Function> View::GetConstructor(v8::Isolate* isolate) {
  static v8::Eternal<v8::Function> constructor;
  if (constructor.IsEmpty()) {
    constructor.Set(isolate, gin_helper::CreateConstructor<View>(
                                 isolate, base::BindRepeating(&View::New)));
  }
  return constructor.Get(isolate);
}

// static
gin::Handle<View> View::Create(v8::Isolate* isolate) {
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Object> obj;
  if (GetConstructor(isolate)->NewInstance(context, 0, nullptr).ToLocal(&obj)) {
    gin::Handle<View> view;
    if (gin::ConvertFromV8(isolate, obj, &view))
      return view;
  }
  return {};
}

// static
void View::BuildPrototype(v8::Isolate* isolate,
                          v8::Local<v8::FunctionTemplate> prototype) {
  prototype->SetClassName(gin::StringToV8(isolate, "View"));
  gin_helper::ObjectTemplateBuilder(isolate, prototype->PrototypeTemplate())
      .SetMethod("addChildView", &View::AddChildViewAt)
      .SetMethod("removeChildView", &View::RemoveChildView)
      .SetProperty("children", &View::GetChildren)
      .SetMethod("setBounds", &View::SetBounds)
      .SetMethod("getBounds", &View::GetBounds)
      .SetMethod("setBackgroundColor", &View::SetBackgroundColor)
      .SetMethod("setBorderRadius", &View::SetBorderRadius)
      .SetMethod("setLayout", &View::SetLayout)
      .SetMethod("setVisible", &View::SetVisible)
      .SetMethod("getVisible", &View::GetVisible);
}

}  // namespace electron::api

namespace {

using electron::api::View;

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();

  gin_helper::Dictionary dict(isolate, exports);
  dict.Set("View", View::GetConstructor(isolate));
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_browser_view, Initialize)
