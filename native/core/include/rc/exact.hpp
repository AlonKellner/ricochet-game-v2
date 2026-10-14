// Exact geometric predicates on float64 data (Stage 91): the port of
// scripts/math/precision/exact.gd. A float64 evaluation with a rigorous
// forward error bound decides whenever the value is certainly non-zero;
// otherwise the value is evaluated exactly with BigFloat at adaptive width
// (products get the limbs of both factors, sums the limbs spanning both
// exponents). The bound only selects the cheap path, it never decides.
#pragma once

#include <bit>
#include <cstdint>

#include "rc/bigfloat.hpp"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace rc {
namespace exact {

// The unit roundoff as the shipped GDScript has it: exact.gd writes
// 1.1102230246251565e-16 (2^-53) but Godot's decimal parser is not correctly
// rounded and yields 0x3c9ffffffffffffe, two ulps below 2^-53. The filters'
// error bounds are therefore relatively 2e-16 too small; the native core
// reproduces the bit pattern (bit-identity contract), and writing pow(2, -53)
// on both sides is B-list item B12 (TDD_10).
constexpr double U = std::bit_cast<double>(std::uint64_t{0x3c9ffffffffffffeULL}); // Godot's value, see above

// A line a x + b y = c with exact coefficients (any nonzero scale), and a
// homogeneous exact point [x, y, w] (the point (x/w, y/w), w > 0).
using Line3 = std::array<BigFloat, 3>;
using Point3 = std::array<BigFloat, 3>;

// A float64 value with a rigorous error bound (running error analysis).
struct EB {
	double v;
	double e;
};
EB emul(EB x, EB y);
EB eadd(EB x, EB y);
EB esub(EB x, EB y);

// ---- exact ops
BigFloat of(double x);
BigFloat mul(const BigFloat &a, const BigFloat &b);
BigFloat add(const BigFloat &a, const BigFloat &b);
BigFloat sub(const BigFloat &a, const BigFloat &b);

// ---- predicates (sign-exact zero tests)
bool orient_zero(double ax, double ay, double bx, double by, double cx, double cy);
bool on_circle_zero(double cx, double cy, double r, double kx, double ky);
bool incircle_zero(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy);

// ---- exact lines and points
Line3 line_from_dir(double px, double py, double dx, double dy);
Line3 line_through(double px, double py, double qx, double qy);
Point3 reflect_point_h(const Point3 &X, double sx, double sy, double ex, double ey);
std::optional<Point3> invert_point_h(const Point3 &X, const Point3 &C, const BigFloat &rw2);
Line3 line_through_h(double px, double py, const Point3 &X);
Point3 circumcenter_h(double ax, double ay, double bx, double by, double cx, double cy);
bool on_line_h(const Line3 &L, const Point3 &X);
bool tangent_zero(const Line3 &L, const Point3 &C, const BigFloat &rw2);
Line3 reflect_line(const Line3 &L, double sx, double sy, double ex, double ey);

// An exact line carried lazily: float64 coefficients with rigorous bounds,
// the exact coefficients computed only when a float filter cannot decide. A
// derived line (reflected / translated) keeps its parent and mirror; a line
// from_approx computes its exact coefficients through `source` on demand
// (which may answer nullopt: the line then makes no exact claim).
class Line {
public:
	using Source = std::function<std::optional<Line3>()>;

	double fa = 0.0, fb = 0.0, fc = 0.0;
	double ea = 0.0, eb = 0.0, ec = 0.0; // |fa - a| <= ea, ...

	static std::shared_ptr<Line> from_exact(const Line3 &L);
	static std::shared_ptr<Line> from_approx(EB a, EB b, EB c, Source source);

	// The exact coefficients, or nullopt when a lazy source could not afford them.
	std::optional<Line3> exact() const;
	std::shared_ptr<Line> translated(double dx, double dy) const;
	std::shared_ptr<Line> reflected(double sx, double sy, double ex, double ey) const;
	bool concurrent(double px, double py, double qx, double qy, double rx, double ry, double sx, double sy) const;
	bool contains(double x, double y) const;
	bool contains_h(const Point3 &X, double x, double y, double exy) const;
	bool tangent_to(const Point3 &C, const BigFloat &rw2, double cx, double cy, double rr, double ecc, double err) const;

	// enable_shared_from_this is not needed: derived lines capture the parent
	// pointer handed to them by the caller through the shared_ptr factories.
	std::shared_ptr<const Line> parent;
	std::vector<double> mirror; // 2 values: translation; 4 values: mirror line s -> e

private:
	mutable std::optional<Line3> exact_;
	Source source_;
	mutable bool source_done_ = false;
};

} // namespace exact
} // namespace rc
