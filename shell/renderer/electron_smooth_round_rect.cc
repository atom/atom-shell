// Copyright (c) 2024 Salesforce, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "electron/shell/renderer/electron_smooth_round_rect.h"

#include <numbers>
#include "base/check.h"

namespace electron {

namespace {

// Applies quarter rotations (n * 90°) to a point relative to the origin.
constexpr SkPoint QuarterRotate(const SkPoint& p,
                                unsigned int quarter_rotations) {
  switch (quarter_rotations % 4) {
    case 0:
      return p;
    case 1:
      return {-p.y(), p.x()};
    case 2:
      return {-p.x(), -p.y()};
    // case 3:
    default:
      return {p.y(), -p.x()};
  }
}

// Edge length consumed for a given smoothness and corner radius.
constexpr float LengthForCornerSmoothness(float smoothness, float radius) {
  return (1.0f + smoothness) * radius;
}

// The smoothness value when consuming an edge length for a corner with a given
// radius.
//
// This function complements `LengthForCornerSmoothness`:
//   SmoothnessForCornerLength(LengthForCornerSmoothness(s, r), r) = s
constexpr float SmoothnessForCornerLength(float length, float radius) {
  return (length / radius) - 1.0f;
}

// Geometric measurements for constructing the curves of smooth round corners on
// a rectangle.
//
// Each measurement's value is relative to the rectangle's natural corner point.
// An "offset" measurement is a one-dimensional length and a "vector"
// measurement is a two-dimensional pair of lengths.
//
// Each measurement's direction is relative to the direction of an edge towards
// the corner. Offsets are in the same direction as the edge toward the corner.
// For vectors, the X direction is parallel and the Y direction is
// perpendicular.
struct CurveGeometry {
  constexpr CurveGeometry(float radius, float smoothness);

  constexpr SkVector edge_connecting_vector() const {
    return {edge_connecting_offset, 0.0f};
  }
  constexpr SkVector edge_curve_vector() const {
    return {edge_curve_offset, 0.0f};
  }
  constexpr SkVector arc_curve_vector() const {
    return {arc_curve_offset, 0.0f};
  }
  constexpr SkVector arc_connecting_vector_transposed() const {
    return {arc_connecting_vector.y(), arc_connecting_vector.x()};
  }

  // The point where the edge connects to the curve.
  float edge_connecting_offset;

  // The control point for the curvature where the edge connects to the curve.
  float edge_curve_offset;

  // The control point for the curvature where the arc connects to the curve.
  float arc_curve_offset;

  // The point where the arc connects to the curve.
  SkVector arc_connecting_vector;
};

constexpr CurveGeometry::CurveGeometry(float radius, float smoothness) {
  edge_connecting_offset = LengthForCornerSmoothness(smoothness, radius);

  float arc_angle = (std::numbers::pi / 4.0f) * smoothness;

  arc_connecting_vector =
      SkVector::Make(1.0f - std::sin(arc_angle), 1.0f - std::cos(arc_angle)) *
      radius;

  arc_curve_offset = (1.0f - std::tan(arc_angle / 2.0f)) * radius;

  constexpr float EDGE_CURVE_POINT_RATIO = 2.0f / 3.0f;
  edge_curve_offset =
      edge_connecting_offset -
      ((edge_connecting_offset - arc_curve_offset) * EDGE_CURVE_POINT_RATIO);
}

void DrawCorner(SkPath& path,
                float radius,
                const CurveGeometry& curve1,
                const CurveGeometry& curve2,
                const SkPoint& corner,
                unsigned int quarter_rotations) {
  // Move/Line to the edge connecting point
  {
    SkPoint edge_connecting_point =
        corner +
        QuarterRotate(curve1.edge_connecting_vector(), quarter_rotations + 1);

    if (quarter_rotations == 0) {
      path.moveTo(edge_connecting_point);
    } else {
      path.lineTo(edge_connecting_point);
    }
  }

  // Draw the first smoothing curve
  {
    SkPoint edge_curve_point =
        corner +
        QuarterRotate(curve1.edge_curve_vector(), quarter_rotations + 1);
    SkPoint arc_curve_point = corner + QuarterRotate(curve1.arc_curve_vector(),
                                                     quarter_rotations + 1);
    SkPoint arc_connecting_point =
        corner + QuarterRotate(curve1.arc_connecting_vector_transposed(),
                               quarter_rotations);
    path.cubicTo(edge_curve_point, arc_curve_point, arc_connecting_point);
  }

  // Draw the arc
  {
    SkPoint arc_connecting_point =
        corner + QuarterRotate(curve2.arc_connecting_vector, quarter_rotations);
    path.arcTo(SkPoint::Make(radius, radius), 0.0f, SkPath::kSmall_ArcSize,
               SkPathDirection::kCW, arc_connecting_point);
  }

  // Draw the second smoothing curve
  {
    SkPoint arc_curve_point =
        corner + QuarterRotate(curve2.arc_curve_vector(), quarter_rotations);
    SkPoint edge_curve_point =
        corner + QuarterRotate(curve2.edge_curve_vector(), quarter_rotations);
    SkPoint edge_connecting_point =
        corner +
        QuarterRotate(curve2.edge_connecting_vector(), quarter_rotations);
    path.cubicTo(arc_curve_point, edge_curve_point, edge_connecting_point);
  }
}

// Constrains the smoothness of two corners along the same edge.
//
// If the smoothness value needs to be constrained, it will try to keep the
// ratio of the smoothness values the same as the ratio of the radii
// (`s1/s2 = r1/r2`).
constexpr std::pair<float, float> ConstrainSmoothness(float size,
                                                      float smoothness,
                                                      float radius1,
                                                      float radius2) {
  float edge_consumed1 = LengthForCornerSmoothness(smoothness, radius1);
  float edge_consumed2 = LengthForCornerSmoothness(smoothness, radius2);

  // If both corners fit within the edge size then keep the smoothness
  if (edge_consumed1 + edge_consumed2 <= size) {
    return {smoothness, smoothness};
  }

  float ratio = radius1 / (radius1 + radius2);
  float length1 = size * ratio;
  float length2 = size - length1;

  float smoothness1 =
      std::max(SmoothnessForCornerLength(length1, radius1), 0.0f);
  float smoothness2 =
      std::max(SmoothnessForCornerLength(length2, radius2), 0.0f);

  return {smoothness1, smoothness2};
}

}  // namespace

// The algorithm for drawing this shape is based on the article
// "Desperately seeking squircles" by Daniel Furse. A brief summary:
//
// In a simple round rectangle, each corner of a plain rectangle is replaced
// with a quarter circle and connected to each edge of the corner.
//
//        Edge
//      ←------→       ↖
//     ----------o--__  `、 Corner (Quarter Circle)
//                    `、 `、
//                      |   ↘
//                       |
//                       o
//                       | ↑
//                       | | Edge
//                       | ↓
//
// This creates sharp changes in the curvature at the points where the edge
// transitions to the corner, suddenly curving at a constant rate. Our primary
// goal is to smooth out that curvature profile, slowly ramping up and back
// down, like turning a car with the steering wheel.
//
// To achieve this, we "expand" that point where the circular corner meets the
// straight edge in both directions. We use this extra space to construct a
// small curved path that eases the curvature from the edge to the corner
// circle.
//
//      Edge  Curve
//      ←--→ ←-----→
//     -----o----___o   ↖、 Corner (Circular Arc)
//                    `、 `↘
//                      o
//                      |  ↑
//                       | | Curve
//                       | ↓
//                       o
//                       | ↕ Edge
//
// Each curve is implemented as a cubic Bézier curve, composed of four control
// points:
//
// * The first control point connects to the straight edge.
// * The fourth (last) control point connects to the circular arc.
// * The second & third control points both lie on the infinite line extending
//   from the straight edge.
// * The third control point (only) also lies on the infinite line tangent to
//   the circular arc at the fourth control point.
//
// The first and fourth (last) control points are firmly fixed by attaching to
// the straight edge and circular arc, respectively. The third control point is
// fixed at the intersection between the edge and tangent lines. The second
// control point, however, is only constrained to the infinite edge line, but
// we may choose where.
SkPath DrawSmoothRoundRect(float x,
                           float y,
                           float width,
                           float height,
                           float smoothness,
                           float top_left_radius,
                           float top_right_radius,
                           float bottom_right_radius,
                           float bottom_left_radius) {
  DCHECK(0.0f <= smoothness && smoothness <= 1.0f);

  // Assume the radii are already constrained within the rectangle size
  DCHECK(top_left_radius + top_right_radius <= width);
  DCHECK(bottom_left_radius + bottom_right_radius <= width);
  DCHECK(top_left_radius + bottom_left_radius <= height);
  DCHECK(top_right_radius + bottom_right_radius <= height);

  if (width <= 0.0f || height <= 0.0f) {
    return SkPath();
  }

  // Constrain the smoothness for each curve on each edge
  auto [top_left_smoothness, top_right_smoothness] =
      ConstrainSmoothness(width, smoothness, top_left_radius, top_right_radius);
  auto [right_top_smoothness, right_bottom_smoothness] = ConstrainSmoothness(
      height, smoothness, top_right_radius, bottom_right_radius);
  auto [bottom_left_smoothness, bottom_right_smoothness] = ConstrainSmoothness(
      width, smoothness, bottom_left_radius, bottom_right_radius);
  auto [left_top_smoothness, left_bottom_smoothness] = ConstrainSmoothness(
      height, smoothness, top_left_radius, bottom_left_radius);

  SkPath path;

  // Top left corner
  DrawCorner(path, top_left_radius,
             CurveGeometry(top_left_radius, left_top_smoothness),
             CurveGeometry(top_left_radius, top_left_smoothness),
             SkPoint::Make(x, y), 0);

  // Top right corner
  DrawCorner(path, top_right_radius,
             CurveGeometry(top_right_radius, top_right_smoothness),
             CurveGeometry(top_right_radius, right_top_smoothness),
             SkPoint::Make(x + width, y), 1);

  // Bottom right corner
  DrawCorner(path, bottom_right_radius,
             CurveGeometry(bottom_right_radius, right_bottom_smoothness),
             CurveGeometry(bottom_right_radius, bottom_right_smoothness),
             SkPoint::Make(x + width, y + height), 2);

  // Bottom left corner
  DrawCorner(path, bottom_left_radius,
             CurveGeometry(bottom_left_radius, bottom_left_smoothness),
             CurveGeometry(bottom_left_radius, left_bottom_smoothness),
             SkPoint::Make(x, y + height), 3);

  path.close();
  return path;
}

}  // namespace electron
