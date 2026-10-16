// Carrier geometry of displayed steps (Stage 96/97): the port of
// GeneralizedCircle, Segment.derive_carrier and VisualConverter.is_arc on
// float32 points, computed in float64 exactly as GDScript does (a Vector2
// component read is a float64 value).
#pragma once

#include "rc/trace_types.hpp"

#include <array>
#include <cmath>

namespace rc {
namespace arc {

constexpr double MAX_ARC_RADIUS = 100000.0;
constexpr int POINTS_PER_FULL_CIRCLE = 256;

// a |z|^2 + b x + c y + d = 0
struct GeneralizedCircle {
	double a = 0, b = 0, c = 0, d = 0;
	bool is_line() const { return a == 0.0; }
	double center_x() const { return -b / (2.0 * a); }
	double center_y() const { return -c / (2.0 * a); }
	double radius() const { return std::sqrt((b * b + c * c - 4.0 * a * d) / (4.0 * a * a)); }
	double evaluate(double x, double y) const { return a * (x * x + y * y) + b * x + c * y + d; }
};

inline double det3x3(double a11, double a12, double a13, double a21, double a22, double a23, double a31, double a32, double a33) {
	return (a11 * (a22 * a33 - a23 * a32) - a12 * (a21 * a33 - a23 * a31) + a13 * (a21 * a32 - a22 * a31));
}

inline std::array<double, 4> homogeneous_row(const V2 &p) {
	if (p.is_inf()) {
		return {1.0, 0.0, 0.0, 0.0};
	}
	const double x = p.x;
	const double y = p.y;
	return {x * x + y * y, x, y, 1.0};
}

// Stage 107: the line through start and end, exactly (a = 0): derive_carrier's
// formula on the exact (float64) midpoint, so its orientation and scale are
// the three-point carrier's of a collinear via. Float32 points mapped by a
// rotation are almost never exactly collinear; a straight segment must not
// become a huge arc.
inline GeneralizedCircle straight_carrier(const V2 &p_start, const V2 &p_end) {
	const double sx = p_start.x, sy = p_start.y, ex = p_end.x, ey = p_end.y;
	const double mx = (sx + ex) / 2.0, my = (sy + ey) / 2.0;
	const std::array<double, 4> r1{sx * sx + sy * sy, sx, sy, 1.0};
	const std::array<double, 4> r2{mx * mx + my * my, mx, my, 1.0};
	const std::array<double, 4> r3{ex * ex + ey * ey, ex, ey, 1.0};
	GeneralizedCircle g;
	g.a = 0.0;
	g.b = -det3x3(r1[0], r1[2], r1[3], r2[0], r2[2], r2[3], r3[0], r3[2], r3[3]);
	g.c = det3x3(r1[0], r1[1], r1[3], r2[0], r2[1], r2[3], r3[0], r3[1], r3[3]);
	g.d = -det3x3(r1[0], r1[1], r1[2], r2[0], r2[1], r2[2], r3[0], r3[1], r3[2]);
	return g;
}

// Segment.derive_carrier(start, end, via)
inline GeneralizedCircle derive_carrier(const V2 &p_start, const V2 &p_end, const V2 &p_via) {
	const auto r1 = homogeneous_row(p_start);
	const auto r2 = homogeneous_row(p_via);
	const auto r3 = homogeneous_row(p_end);
	GeneralizedCircle g;
	g.a = det3x3(r1[1], r1[2], r1[3], r2[1], r2[2], r2[3], r3[1], r3[2], r3[3]);
	g.b = -det3x3(r1[0], r1[2], r1[3], r2[0], r2[2], r2[3], r3[0], r3[2], r3[3]);
	g.c = det3x3(r1[0], r1[1], r1[3], r2[0], r2[1], r2[3], r3[0], r3[1], r3[3]);
	g.d = -det3x3(r1[0], r1[1], r1[2], r2[0], r2[1], r2[2], r3[0], r3[1], r3[2]);
	return g;
}

// VisualConverter.is_arc: the step start -> via -> end is a drawable arc.
inline bool is_arc(const V2 &start, const V2 &via, const V2 &end_v) {
	if (end_v.is_inf()) {
		return false;
	}
	if (start.is_inf()) {
		return false;
	}
	if (start == end_v) {
		return false;
	}
	const GeneralizedCircle carrier = derive_carrier(start, end_v, via);
	if (carrier.is_line()) {
		return false;
	}
	return carrier.radius() < MAX_ARC_RADIUS;
}

} // namespace arc
} // namespace rc
