// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/gin_converters/gfx_converter.h"

#include <string>

#include "gin/data_object_builder.h"
#include "shell/common/color_util.h"
#include "shell/common/gin_helper/dictionary.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/resize_utils.h"
#include "ui/gfx/geometry/size.h"

namespace gin {

v8::Local<v8::Value> Converter<gfx::Point>::ToV8(v8::Isolate* isolate,
                                                 const gfx::Point& val) {
  auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
  dict.Set("x", val.x());
  dict.Set("y", val.y());
  return dict.GetHandle();
}

bool Converter<gfx::Point>::FromV8(v8::Isolate* isolate,
                                   v8::Local<v8::Value> val,
                                   gfx::Point* out) {
  gin::Dictionary dict(isolate);
  if (!gin::ConvertFromV8(isolate, val, &dict))
    return false;
  double x, y;
  if (!dict.Get("x", &x) || !dict.Get("y", &y))
    return false;
  *out = gfx::Point(static_cast<int>(std::round(x)),
                    static_cast<int>(std::round(y)));
  return true;
}

v8::Local<v8::Value> Converter<gfx::PointF>::ToV8(v8::Isolate* isolate,
                                                  const gfx::PointF& val) {
  auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
  dict.Set("x", val.x());
  dict.Set("y", val.y());
  return dict.GetHandle();
}

bool Converter<gfx::PointF>::FromV8(v8::Isolate* isolate,
                                    v8::Local<v8::Value> val,
                                    gfx::PointF* out) {
  gin::Dictionary dict(isolate);
  if (!gin::ConvertFromV8(isolate, val, &dict))
    return false;
  float x, y;
  if (!dict.Get("x", &x) || !dict.Get("y", &y))
    return false;
  *out = gfx::PointF(x, y);
  return true;
}

v8::Local<v8::Value> Converter<gfx::Size>::ToV8(v8::Isolate* isolate,
                                                const gfx::Size& val) {
  auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
  dict.Set("width", val.width());
  dict.Set("height", val.height());
  return dict.GetHandle();
}

bool Converter<gfx::Size>::FromV8(v8::Isolate* isolate,
                                  v8::Local<v8::Value> val,
                                  gfx::Size* out) {
  gin::Dictionary dict(isolate);
  if (!gin::ConvertFromV8(isolate, val, &dict))
    return false;
  int width, height;
  if (!dict.Get("width", &width) || !dict.Get("height", &height))
    return false;
  *out = gfx::Size(width, height);
  return true;
}

v8::Local<v8::Value> Converter<gfx::Rect>::ToV8(v8::Isolate* isolate,
                                                const gfx::Rect& val) {
  auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
  dict.Set("x", val.x());
  dict.Set("y", val.y());
  dict.Set("width", val.width());
  dict.Set("height", val.height());
  return dict.GetHandle();
}

bool Converter<gfx::Rect>::FromV8(v8::Isolate* isolate,
                                  v8::Local<v8::Value> val,
                                  gfx::Rect* out) {
  gin::Dictionary dict(isolate);
  if (!gin::ConvertFromV8(isolate, val, &dict))
    return false;
  float x, y, width, height;
  if (!dict.Get("x", &x) || !dict.Get("y", &y) || !dict.Get("width", &width) ||
      !dict.Get("height", &height))
    return false;

  *out = ToRoundedRect(gfx::RectF(x, y, width, height));
  return true;
}

v8::Local<v8::Value> Converter<gfx::Insets>::ToV8(v8::Isolate* isolate,
                                                  const gfx::Insets& val) {
  return gin::DataObjectBuilder(isolate)
      .Set("top", val.top())
      .Set("left", val.left())
      .Set("bottom", val.bottom())
      .Set("right", val.right())
      .Build();
}

bool Converter<gfx::Insets>::FromV8(v8::Isolate* isolate,
                                    v8::Local<v8::Value> val,
                                    gfx::Insets* out) {
  gin::Dictionary dict(isolate);
  if (!gin::ConvertFromV8(isolate, val, &dict))
    return false;
  double top, left, right, bottom;
  if (!dict.Get("top", &top))
    return false;
  if (!dict.Get("left", &left))
    return false;
  if (!dict.Get("bottom", &bottom))
    return false;
  if (!dict.Get("right", &right))
    return false;
  *out = gfx::Insets::TLBR(top, left, bottom, right);
  return true;
}

template <>
struct Converter<display::Display::AccelerometerSupport> {
  static v8::Local<v8::Value> ToV8(
      v8::Isolate* isolate,
      const display::Display::AccelerometerSupport& val) {
    switch (val) {
      case display::Display::AccelerometerSupport::AVAILABLE:
        return StringToV8(isolate, "available");
      case display::Display::AccelerometerSupport::UNAVAILABLE:
        return StringToV8(isolate, "unavailable");
      default:
        return StringToV8(isolate, "unknown");
    }
  }
};

template <>
struct Converter<display::Display::TouchSupport> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   const display::Display::TouchSupport& val) {
    switch (val) {
      case display::Display::TouchSupport::AVAILABLE:
        return StringToV8(isolate, "available");
      case display::Display::TouchSupport::UNAVAILABLE:
        return StringToV8(isolate, "unavailable");
      default:
        return StringToV8(isolate, "unknown");
    }
  }
};

v8::Local<v8::Value> Converter<display::Display>::ToV8(
    v8::Isolate* isolate,
    const display::Display& val) {
  auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
  dict.Set("accelerometerSupport", val.accelerometer_support());
  dict.Set("bounds", val.bounds());
  dict.Set("colorDepth", val.color_depth());
  dict.Set("colorSpace", val.GetColorSpaces().GetRasterColorSpace().ToString());
  dict.Set("depthPerComponent", val.depth_per_component());
  dict.Set("detected", val.detected());
  dict.Set("displayFrequency", val.display_frequency());
  dict.Set("id", val.id());
  dict.Set("internal", val.IsInternal());
  dict.Set("label", val.label());
  dict.Set("maximumCursorSize", val.maximum_cursor_size());
  dict.Set("monochrome", val.is_monochrome());
  dict.Set("nativeOrigin", val.native_origin());
  dict.Set("rotation", val.RotationAsDegree());
  dict.Set("scaleFactor", val.device_scale_factor());
  dict.Set("size", val.size());
  dict.Set("workArea", val.work_area());
  dict.Set("workAreaSize", val.work_area_size());
  dict.Set("touchSupport", val.touch_support());
  return dict.GetHandle();
}

v8::Local<v8::Value> Converter<gfx::ResizeEdge>::ToV8(
    v8::Isolate* isolate,
    const gfx::ResizeEdge& val) {
  switch (val) {
    case gfx::ResizeEdge::kRight:
      return StringToV8(isolate, "right");
    case gfx::ResizeEdge::kBottom:
      return StringToV8(isolate, "bottom");
    case gfx::ResizeEdge::kTop:
      return StringToV8(isolate, "top");
    case gfx::ResizeEdge::kLeft:
      return StringToV8(isolate, "left");
    case gfx::ResizeEdge::kTopLeft:
      return StringToV8(isolate, "top-left");
    case gfx::ResizeEdge::kTopRight:
      return StringToV8(isolate, "top-right");
    case gfx::ResizeEdge::kBottomLeft:
      return StringToV8(isolate, "bottom-left");
    case gfx::ResizeEdge::kBottomRight:
      return StringToV8(isolate, "bottom-right");
    default:
      return StringToV8(isolate, "unknown");
  }
}

bool Converter<WrappedSkColor>::FromV8(v8::Isolate* isolate,
                                       v8::Local<v8::Value> val,
                                       WrappedSkColor* out) {
  std::string str;
  if (!gin::ConvertFromV8(isolate, val, &str))
    return false;
  *out = electron::ParseCSSColor(str);
  return true;
}

}  // namespace gin
