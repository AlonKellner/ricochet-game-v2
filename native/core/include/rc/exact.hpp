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

// The unit roundoff 2^-53, exactly. (The GDScript original wrote the decimal
// 1.1102230246251565e-16, which Godot parsed two ulps low, 0x3c9ffffffffffffe;
// the native port reproduced that until B12 was fixed in Stage 102. Only the
// filters' error bounds use U, and a filter that declines falls back to exact
// arithmetic, so predicate results are unchanged.)
constexpr double U = std::bit_cast<double>(std::uint64_t{0x3ca0000000000000ULL}); // 2^-53

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

// ---- the exact pole (Stage 105)
// Every beam of an epoch is the image F(l) of a straight line l under the
// frame's maps; it passes Q = F(inf), its direction there the image of l's
// direction. The pole record keeps both exactly: Q homogeneous (w > 0), D
// a direction at any nonzero real scale (unoriented). A beam tangent to a
// carrier at Q is then a touch, decided without rounding.
struct ExactPole {
	Point3 Q;
	BigFloat dx, dy;
};
// The record after a map (nullopt: the direction degenerated, or Q went to
// infinity for pole_invert).
std::optional<ExactPole> pole_reflect(const ExactPole &p, double sx, double sy, double ex, double ey);
std::optional<ExactPole> pole_invert(const ExactPole &p, const Point3 &C, const BigFloat &rw2);
// pole_invert with Q's image given (invert_point_h(p.Q, C, rw2): it depends on Q
// alone, so a caller may memoize it -- Stage 120); the same result bit for bit.
std::optional<ExactPole> pole_invert_from(const ExactPole &p, const std::optional<Point3> &Q, const Point3 &C);
ExactPole pole_translate(const ExactPole &p, double dx, double dy);
// A straight beam on the exact line L (Q at infinity) inverted in the circle
// centered at C: Q becomes C, the direction there L's.
std::optional<ExactPole> pole_from_line(const Line3 &L, const Point3 &C);
// A pre-transform frame's start: Q0, and the initial beam's direction at Q0
// for a direction shot (the circle through Q0 tangent to (dx, dy) at P) or an
// aimed shot (the circle through P, X, Q0).
std::optional<ExactPole> pole_start_direction(double px, double py, double dx, double dy, const Point3 &Q0);
std::optional<ExactPole> pole_start_aimed(double px, double py, const Point3 &X, const Point3 &Q0);
// Exact: the carrier passes Q.
bool pole_on_line(const ExactPole &p, double sx, double sy, double ex, double ey, const Line3 *line = nullptr); // (line: the carrier's exact line, Surf::exact_line, A.2)
bool pole_on_circle(const ExactPole &p, const Point3 &C, const BigFloat &rw2);
// Exact: the carrier passes Q and is tangent there to the beam.
bool pole_tangent_line(const ExactPole &p, double sx, double sy, double ex, double ey, const Line3 *line = nullptr);
bool pole_tangent_circle(const ExactPole &p, const Point3 &C, const BigFloat &rw2);
// The largest limb count of the record (the exact-data budget).
int pole_limbs(const ExactPole &p);

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
	// (A, B: the two lines' exact coefficients when the caller has them cached, else built here)
	bool concurrent(double px, double py, double qx, double qy, double rx, double ry, double sx, double sy, const Line3 *A = nullptr,
			const Line3 *B = nullptr) const;
	bool contains(double x, double y) const;
	bool contains_h(const Point3 &X, double x, double y, double exy) const;
	bool tangent_to(const Point3 &C, const BigFloat &rw2, double cx, double cy, double rr, double ecc, double err) const;
	// Exact (Stage 112): the line is parallel to the line through s and e (no
	// crossing exists; the tier's root of a nearly parallel pair is an artefact).
	// False when the line has no exact coefficients.
	bool parallel_to(double sx, double sy, double ex, double ey) const;

	// enable_shared_from_this is not needed: derived lines capture the parent
	// pointer handed to them by the caller through the shared_ptr factories.
	std::shared_ptr<const Line> parent;
	std::vector<double> mirror; // 2 values: translation; 4 values: mirror line s -> e

private:
	mutable std::optional<Line3> exact_;
	Source source_;
	mutable bool source_done_ = false;
};

// ---- the exact pole, carried lazily (the cast-cost program, A.3)
// Every effect used to compute the pole after it eagerly (pole_after), though
// its consumers (the tangency at Q, the pinning of crossings at Q) decide by a
// float filter first and ask the exact pole only when it declines. A PoleRec
// is a start record and the maps applied to it since (like BeamRec); it keeps a
// float64 shadow with rigorous bounds -- Q homogeneous, D -- propagated through
// the maps, so the predicates' filters run on the shadow, and the exact pole
// is computed (and memoized in the record) only where a filter cannot decide.
// A record whose exact pole is absent (the start had none, a map degenerated,
// or the pole passed the budget) answers every predicate as no pole did: the
// walk keeps the candidate and pins nothing.
struct PoleShadow {
	EB qx{0.0, 0.0}, qy{0.0, 0.0}, qw{0.0, 0.0}, dx{0.0, 0.0}, dy{0.0, 0.0};
	bool ok = false; // false: a bound is not finite (every filter declines)
};
class PoleRec : public std::enable_shared_from_this<PoleRec> {
public:
	using Source = std::function<std::optional<ExactPole>()>;
	// the image of Q under the inversion (invert_point_h(Q, C, rw2)), memoized by the caller
	using ImageSource = std::function<std::optional<Point3>(const Point3 &)>;
	static constexpr int EXACT_LIMBS = 320; // (Solver::EXACT_IMAGE_LIMBS: past it the record makes no exact claim)

	// an aimed start in a pre-transform frame: Q0, D = conj(P - X) (X - Q0) (P - Q0) (pole_start_aimed; `exact` computes it, memoized)
	static std::shared_ptr<const PoleRec> start_aimed(double px, double py, const Point3 &X, const Point3 &Q0, Source exact);
	static std::shared_ptr<const PoleRec> start_direction(double px, double py, double dx, double dy, const Point3 &Q0);
	// the pole of a straight beam with exact line data, at a circle's centre (pole_from_line)
	static std::shared_ptr<const PoleRec> from_line(std::shared_ptr<const Line> line, const Point3 *C);
	std::shared_ptr<const PoleRec> reflected(double sx, double sy, double ex, double ey) const;
	std::shared_ptr<const PoleRec> translated(double dx, double dy) const;
	// C and rw2 are the circle's exact data, owned by the scene (it outlives the walk)
	std::shared_ptr<const PoleRec> inverted(const Point3 *C, const BigFloat *rw2, ImageSource image) const;

	// The exact pole: nullopt where there is none (see above). Computed once.
	const std::optional<ExactPole> &exact() const;
	const PoleShadow &shadow() const { return shadow_; }
	// The filters on the shadow: false where the carrier certainly does not pass
	// Q (is not tangent there); nullopt where the exact predicate must decide.
	std::optional<bool> on_line(double sx, double sy, double ex, double ey) const;
	std::optional<bool> on_circle(const Point3 &C, const BigFloat &rw2) const;
	std::optional<bool> tangent_line(double sx, double sy, double ex, double ey) const;
	std::optional<bool> tangent_circle(const Point3 &C, const BigFloat &rw2) const;

private:
	enum class Kind { Source, Reflect, Translate, Invert };
	Kind kind_ = Kind::Source;
	PoleShadow shadow_;
	Source source_;
	std::shared_ptr<const PoleRec> parent_;
	double m_[4] = {0.0, 0.0, 0.0, 0.0};
	const Point3 *c_ = nullptr;
	const BigFloat *rw2_ = nullptr;
	ImageSource image_;
	mutable bool done_ = false;
	mutable std::optional<ExactPole> exact_;
};

// ---- the exact beam (Perf P2b)
// A generalized circle A (x^2 + y^2) + B x + C y + D = 0, exact (any nonzero
// scale; A = 0: a line).
struct GCircle {
	BigFloat A, B, C, D;
};
GCircle gc_from_line(const Line3 &L);
// The generalized circle through three exact homogeneous points (a line when
// they are collinear).
GCircle gc_through(const Point3 &P1, const Point3 &P2, const Point3 &P3);
GCircle gc_reflect(const GCircle &g, double sx, double sy, double ex, double ey);
GCircle gc_translate(const GCircle &g, double dx, double dy);
GCircle gc_invert(const GCircle &g, const Point3 &C, const BigFloat &rw2);
int gc_limbs(const GCircle &g);
// Exact: the generalized circle touches the carrier (a line through s and e;
// a circle of center C, rw2 = (r w)^2) at one point.
bool gc_tangent_line(const GCircle &g, double sx, double sy, double ex, double ey, const Line3 *line = nullptr);
bool gc_tangent_circle(const GCircle &g, const Point3 &C, const BigFloat &rw2);
// Exact: the generalized circle passes the float64 point (x, y).
bool gc_passes(const GCircle &g, double x, double y);

// A beam's exact generalized circle, carried lazily: an exact line (the last
// straight beam with exact data) and the maps applied to it since, made exact
// only when a decision asks (BeamRec::exact). Every beam of an epoch is the
// image of a line under the frame's maps (see ExactPole); a beam whose record
// is missing makes no exact claim.
class BeamRec {
public:
	static std::shared_ptr<const BeamRec> from_line(std::shared_ptr<const Line> line);
	// a beam whose exact circle is known outright (K9: a pre-transform start)
	static std::shared_ptr<const BeamRec> from_gc(const GCircle &g);
	// ... computed by `source` when a decision asks (A.4: gc_through was 27% of a
	// tangent_infinity region, and every consumer filters in float first)
	static std::shared_ptr<const BeamRec> from_gc_lazy(std::function<GCircle()> source);
	std::shared_ptr<const BeamRec> reflected(double sx, double sy, double ex, double ey) const;
	std::shared_ptr<const BeamRec> translated(double dx, double dy) const;
	std::shared_ptr<const BeamRec> inverted(const Point3 &C, const BigFloat &rw2) const;
	// nullopt: the base line has no exact coefficients, or the circle grew past
	// the budget (EXACT_LIMBS)
	std::optional<GCircle> exact() const;
	// Stage 124: exactly whether exact() passes (x, y) (as gc_passes on it says),
	// by pulling the point back through the maps to the base line or circle: a
	// point's image stays small where the circle's coefficients grow with every
	// inversion. False where exact() would make no claim.
	bool passes(double x, double y) const;
	static constexpr int EXACT_LIMBS = 320;

private:
	bool passes_h(const Point3 &X) const;
	enum class Kind { Line, Circle, Reflect, Translate, Invert };
	Kind kind_ = Kind::Line;
	std::shared_ptr<const Line> line_;
	std::shared_ptr<const BeamRec> parent_;
	double m_[4] = {0.0, 0.0, 0.0, 0.0};
	Point3 c_;
	BigFloat rw2_;
	std::function<GCircle()> gc_source_; // (Kind::Circle, lazy)
	mutable bool done_ = false;
	mutable std::optional<GCircle> exact_;
};

} // namespace exact
} // namespace rc
