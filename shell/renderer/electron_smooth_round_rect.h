// Copyright (c) 2024 Salesforce, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_RENDERER_API_ELECTRON_SMOOTH_ROUND_RECT_H_
#define ELECTRON_SHELL_RENDERER_API_ELECTRON_SMOOTH_ROUND_RECT_H_

#include "third_party/skia/include/core/SkPath.h"

namespace electron {

// Draws a rectangle that has smooth round corners for a given "smoothness"
// value between 0.0 and 1.0 (representing 0% to 100%).
//
// The smoothness value determines how much edge length can be consumed by each
// corner, scaling with respect to that corner's radius. The smoothness will
// dynamically scale back if there is not enough edge length, similar to how
// the corner radius backs off when there isn't enough edge length.
//
// Each corner's radius can be supplied independently. Corner radii are expected
// to already be balanced (Radius1 + Radius2 <= Length, for each given side).
//
// Elliptical corner radii are not currently supported.
SkPath DrawSmoothRoundRect(float x,
                           float y,
                           float width,
                           float height,
                           float smoothness,
                           float top_left_radius,
                           float top_right_radius,
                           float bottom_right_radius,
                           float bottom_left_radius);

}  // namespace electron

#endif  // ELECTRON_SHELL_RENDERER_API_ELECTRON_SMOOTH_ROUND_RECT_H_
