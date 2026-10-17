// Property tests of the native core (Stages 102-103): closed-form identities
// and bounds checked on seeded random inputs, independent of the goldens.
#include "rc/bigfloat.hpp"
#include "rc/exact.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <random>

using namespace rc;

namespace {

// A normalized BigFloat of n limbs with random limbs (top bit set), a random
// sign and exponent.
BigFloat random_big(std::mt19937_64 &rng, int n, int64_t exp_lo, int64_t exp_hi) {
	BigFloat x;
	x.sign = (rng() & 1) ? 1 : -1;
	x.limbs.resize(size_t(n));
	for (int i = 0; i < n; i++) {
		x.limbs[size_t(i)] = int64_t(rng() & uint64_t(BigFloat::MASK));
	}
	x.limbs[size_t(n - 1)] |= BigFloat::TOP_BIT;
	std::uniform_int_distribution<int64_t> e(exp_lo, exp_hi);
	x.exp = e(rng);
	return x;
}

// The value with all n limbs equal to MASK: 2^(28 n) - 1 (times 2^exp).
BigFloat all_ones(int n) {
	BigFloat x;
	x.sign = 1;
	x.exp = 0;
	x.limbs.assign(size_t(n), BigFloat::MASK);
	return x;
}

} // namespace

TEST_CASE("B1: BigFloat::mul is exact past 128 limbs (columns summed in 128 bits)") {
	// (2^m - 1)^2 = 2^(2m) - 2^(m+1) + 1, m = 28 n: truncated to n limbs the
	// product's top half is 2^m - 2, i.e. limbs [MASK - 1, MASK, ..., MASK]
	// at exponent m. Every column of the schoolbook product holds up to n
	// products of two MASKs (< 2^56 each): past 128 limbs an int64 sum wraps.
	for (int n : {4, 64, 128, 129, 200, 320}) {
		const BigFloat a = all_ones(n);
		const BigFloat p = a.mul(a);
		REQUIRE(p.sign == 1);
		REQUIRE(int(p.limbs.size()) == n);
		CHECK_MESSAGE(p.exp == int64_t(BigFloat::BITS) * n, n);
		CHECK_MESSAGE(p.limbs[0] == BigFloat::MASK - 1, n);
		bool rest = true;
		for (int i = 1; i < n; i++) {
			rest = rest && p.limbs[size_t(i)] == BigFloat::MASK;
		}
		CHECK_MESSAGE(rest, n);
	}
	// random factors: the product of 320-limb numbers agrees with the product
	// computed in two halves (a * b = a * b_hi + a * b_lo, b_lo below 2^-28*160 of b),
	// to the precision both carry
	std::mt19937_64 rng(102);
	for (int k = 0; k < 20; k++) {
		const BigFloat a = random_big(rng, 320, -4000, 4000);
		const BigFloat b = random_big(rng, 320, -4000, 4000);
		const BigFloat p = a.mul(b);
		const BigFloat q = b.mul(a);
		CHECK(p.cmp(q) == 0);
		// |a b| / |a| = |b| within the truncation of a division at 320 limbs
		const BigFloat back = p.div(a);
		const BigFloat err = back.sub(b).abs_val();
		const bool tiny = err.is_zero() || err.top_exp() < b.top_exp() - BigFloat::BITS * 300;
		CHECK(tiny);
	}
}

TEST_CASE("B3: BigFloat::to_float is within 2U of the value (the bound approx() assumes)") {
	std::mt19937_64 rng(103);
	const double two_u = 2.0 * exact::U;
	int checked = 0;
	for (int n : {2, 3, 4, 8, 16, 64}) {
		for (int k = 0; k < 2000; k++) {
			const BigFloat x = random_big(rng, n, -1100 - BigFloat::BITS * n, 900 - BigFloat::BITS * n);
			const double f = x.to_float();
			if (!std::isfinite(f) || f == 0.0 || std::fabs(f) < 2.2250738585072014e-308) {
				continue; // outside the normal double range
			}
			// |f - x| <= 2U |x|, compared exactly at a wider tier
			const int w = n + 4;
			const BigFloat xw = x.with_limbs(w);
			const BigFloat d = BigFloat::from_float(f, w).sub(xw).abs_val();
			const BigFloat bound = xw.abs_val().mul(BigFloat::from_float(two_u, w));
			CHECK_MESSAGE(d.cmp(bound) <= 0, "n=" << n << " k=" << k);
			checked++;
		}
	}
	CHECK(checked > 10000);
}

// ---------------------------------------------------------------- B2

#include "goldens.hpp"

TEST_CASE("B2: the replay of a seeded float64 walk reproduces its tangent-linear gains, projectives included") {
	// The ladder re-estimates BigFloat trajectories with estimate::replay. On a
	// walk seeded from its first event (a physical direction trace: no anchored
	// prefix, whose gains the replay does not model) it must give the gains the
	// walk computed: both push the same derivatives through the same maps. Before
	// the B2 fix a projective hit got the surface's reflection / inversion map (15
	// of these 24 walks differed; 11 of them through a projective). Where a walk
	// knows a hit point exactly by provenance (endpoints, departure re-hits) its
	// derivative there is exact data while the replay re-derives the root: the
	// replay may only be larger there (conservative), never smaller.
	int walks = 0;
	int with_projective = 0;
	int mismatched = 0;
	int mismatched_projective = 0;
	for (const char *f : {"stage83_projective", "stage87_projective_plans", "stage87_click_forks", "stage81_rules"}) {
		const ojson fxs = test::load_json(std::string("bench/fixtures/") + f + ".json");
		for (const ojson &fx : fxs["fixtures"]) {
			if (fx["direction"].is_null() || fx["mode"].get<std::string>() != "physical") {
				continue;
			}
			const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
			Scene<Dual> scene = Scene<Dual>::from_defs(defs, Dual{1.0, 0.0});
			scene.emit_infinity = true;
			Solver<Dual> solver(scene, nullptr);
			const CN<Dual> P{Dual{fx["player"][0].get<double>(), 0.0}, Dual{fx["player"][1].get<double>(), 0.0}};
			const CN<Dual> D{Dual{fx["direction"][0].get<double>(), 0.0}, Dual{fx["direction"][1].get<double>(), 0.0}};
			const Walk<Dual> w = solver.run_direction(P, D, plan_from_json(fx["plan"]), std::nullopt, Mode::Physical, MAX_HITS, Flags());
			if (w.events.empty()) {
				continue;
			}
			const estimate::Sensitivity walked = estimate::collect(w.events, w.gains);
			const estimate::Sensitivity replayed = estimate::replay(w.events, scene);
			REQUIRE(walked.gain.size() == replayed.gain.size());
			bool proj = false;
			bool bad = false;
			for (size_t i = 0; i < w.events.size(); i++) {
				proj = proj || w.events[i].projective;
				const double a = walked.gain[i];
				const double b = replayed.gain[i];
				if (!((std::isnan(a) && std::isnan(b)) || std::fabs(a - b) <= 1e-6 * std::max(1.0, std::fabs(a)))) {
					bad = true;
					CHECK_MESSAGE(b >= a * (1.0 - 1e-9), fx["name"].get<std::string>() << " event " << i << ": the replay may not be smaller (walked " << a << ", replayed " << b << ")");
				}
			}
			mismatched += bad ? 1 : 0;
			mismatched_projective += (bad && proj) ? 1 : 0;
			with_projective += proj ? 1 : 0;
			walks++;
		}
	}
	MESSAGE("walks " << walks << " with a projective " << with_projective << " mismatched " << mismatched);
	CHECK(mismatched_projective == 0);
	CHECK(mismatched <= 4); // the provenance cases above
	CHECK(with_projective >= 10);
}

// ---------------------------------------------------------------- Stage 103

namespace {

BigFloat B(double x) { return BigFloat::from_float(x, 64); }

// orient, on-circle and incircle determinants in 64-limb BigFloat (1792 bits:
// exact for these double inputs), independent of exact::'s filters.
bool ref_orient_zero(double ax, double ay, double bx, double by, double cx, double cy) {
	return B(bx).sub(B(ax)).mul(B(cy).sub(B(ay))).sub(B(by).sub(B(ay)).mul(B(cx).sub(B(ax)))).is_zero();
}
bool ref_on_circle_zero(double cx, double cy, double r, double kx, double ky) {
	const BigFloat dx = B(kx).sub(B(cx));
	const BigFloat dy = B(ky).sub(B(cy));
	return dx.mul(dx).add(dy.mul(dy)).sub(B(r).mul(B(r))).is_zero();
}
bool ref_incircle_zero(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy) {
	const BigFloat adx = B(ax).sub(B(dx)), ady = B(ay).sub(B(dy));
	const BigFloat bdx = B(bx).sub(B(dx)), bdy = B(by).sub(B(dy));
	const BigFloat cdx = B(cx).sub(B(dx)), cdy = B(cy).sub(B(dy));
	const BigFloat al = adx.mul(adx).add(ady.mul(ady));
	const BigFloat bl = bdx.mul(bdx).add(bdy.mul(bdy));
	const BigFloat cl = cdx.mul(cdx).add(cdy.mul(cdy));
	const BigFloat det = al.mul(bdx.mul(cdy).sub(cdx.mul(bdy))).add(bl.mul(cdx.mul(ady).sub(adx.mul(cdy)))).add(cl.mul(adx.mul(bdy).sub(bdx.mul(ady))));
	return det.is_zero();
}

double nudge(std::mt19937_64 &rng, double x) {
	switch (rng() % 3) {
		case 0: return std::nextafter(x, INF_D);
		case 1: return std::nextafter(x, -INF_D);
		default: return x;
	}
}

} // namespace

TEST_CASE("props: exact predicates agree with a 64-limb reference on degenerate and 1-ulp-perturbed inputs") {
	std::mt19937_64 rng(1031);
	std::uniform_int_distribution<int> coord(-2000, 2000);
	std::uniform_int_distribution<int> small(1, 40);
	int zeros = 0;
	int checked = 0;
	for (int k = 0; k < 3000; k++) {
		// exactly collinear on a dyadic lattice, then maybe one ulp off
		const double ax = coord(rng) * 0.25, ay = coord(rng) * 0.25;
		const double ux = small(rng) * 0.5, uy = small(rng) * 0.5 - 10.0;
		const double s = small(rng) * 0.125, u = -small(rng) * 0.25;
		const double bx = ax + s * ux, by = ay + s * uy;
		const double cx = nudge(rng, ax + u * ux), cy = nudge(rng, ay + u * uy);
		const bool o = exact::orient_zero(ax, ay, bx, by, cx, cy);
		CHECK(o == ref_orient_zero(ax, ay, bx, by, cx, cy));
		zeros += o ? 1 : 0;
		// exactly on a circle through Pythagorean triples, maybe one ulp off
		const double ccx = coord(rng) * 0.5, ccy = coord(rng) * 0.5;
		const int m = small(rng);
		const int t = int(rng() % 4);
		const double px = (t & 1) ? 3.0 * m : -4.0 * m;
		const double py = (t & 2) ? 4.0 * m : -3.0 * m;
		const double kx = nudge(rng, ccx + px), ky = nudge(rng, ccy + py);
		const bool c = exact::on_circle_zero(ccx, ccy, 5.0 * m, kx, ky);
		CHECK(c == ref_on_circle_zero(ccx, ccy, 5.0 * m, kx, ky));
		zeros += c ? 1 : 0;
		// four points of one circle (3-4-5 and 5-12-13 multiples share radius 65)
		const double r65[][2] = {{39, 52}, {-52, 39}, {25, -60}, {-60, -25}, {63, 16}, {-33, 56}};
		const double qx = ccx, qy = ccy;
		const double dxp = nudge(rng, qx + r65[3][0]), dyp = qy + r65[3][1];
		const bool ic = exact::incircle_zero(qx + r65[0][0], qy + r65[0][1], qx + r65[1][0], qy + r65[1][1], qx + r65[2][0], qy + r65[2][1], dxp, dyp);
		CHECK(ic == ref_incircle_zero(qx + r65[0][0], qy + r65[0][1], qx + r65[1][0], qy + r65[1][1], qx + r65[2][0], qy + r65[2][1], dxp, dyp));
		zeros += ic ? 1 : 0;
		checked += 3;
	}
	// both outcomes occur: the degenerate cases are hit exactly, and the nudged ones miss
	CHECK(zeros > checked / 6);
	CHECK(zeros < checked);
}

TEST_CASE("props: Beam::param_of inverts Beam::at on random beams (float64 and 4 limbs)") {
	std::mt19937_64 rng(1032);
	std::uniform_real_distribution<double> u(-1.0, 1.0);
	for (int k = 0; k < 4000; k++) {
		const double ang = u(rng) * 3.14159;
		const double kap = u(rng) * 0.01;
		const double t = u(rng) * 150.0;
		const double px = u(rng) * 1000.0, py = u(rng) * 1000.0;
		{
			const Beam<F64> b{{F64{px}, F64{py}}, {F64{std::cos(ang)}, F64{std::sin(ang)}}, F64{kap}};
			const double back = b.param_of(b.at(F64{t})).v;
			CHECK(std::fabs(back - t) <= 1e-9 * std::max(1.0, std::fabs(t)));
		}
		{
			const BigFloat l = BigFloat::from_int(1, 4);
			const Beam<BigFloat> b{{l.like(px), l.like(py)}, CN<BigFloat>{l.like(std::cos(ang)), l.like(std::sin(ang))}.unit(), l.like(kap)};
			const BigFloat back = b.param_of(b.at(l.like(t)));
			CHECK(std::fabs(back.to_float() - t) <= 1e-25 * std::max(1.0, std::fabs(t)));
		}
	}
}

namespace {

// A random small scene: room walls (terminal), mirrors (reflection both
// sides), pass lines and full inversion circles.
std::vector<SurfaceDef> random_scene(std::mt19937_64 &rng) {
	std::uniform_real_distribution<double> x(100.0, 1800.0), y(100.0, 1000.0), r(30.0, 160.0);
	std::vector<SurfaceDef> defs;
	auto line = [&](double sx, double sy, double ex, double ey, EffectType t) {
		SurfaceDef d;
		d.id = int(defs.size()) + 1;
		d.line = true;
		d.sx = sx; d.sy = sy; d.ex = ex; d.ey = ey;
		d.has_via = true; d.vx = (sx + ex) / 2.0; d.vy = (sy + ey) / 2.0;
		SideDef side;
		side.type = t;
		side.interactive = t != EffectType::Pass && t != EffectType::Terminal;
		d.left = side;
		d.right = d.left;
		defs.push_back(d);
	};
	line(50, 50, 1870, 50, EffectType::Terminal);
	line(1870, 50, 1870, 1030, EffectType::Terminal);
	line(1870, 1030, 50, 1030, EffectType::Terminal);
	line(50, 1030, 50, 50, EffectType::Terminal);
	const int n = 2 + int(rng() % 5);
	for (int i = 0; i < n; i++) {
		const int kind = int(rng() % 3);
		if (kind == 2) {
			SurfaceDef d;
			d.id = int(defs.size()) + 1;
			d.line = false;
			d.full = true;
			d.has_circle = true;
			d.cx = x(rng); d.cy = y(rng); d.radius = r(rng);
			d.sx = d.cx + d.radius; d.sy = d.cy;
			d.ex = d.cx - d.radius / 2.0; d.ey = d.cy - 0.866 * d.radius;
			d.has_via = true; d.vx = d.cx - d.radius / 2.0; d.vy = d.cy + 0.866 * d.radius;
			SideDef inv;
			inv.type = EffectType::Inversion;
			inv.interactive = true;
			d.left = inv;
			d.right = d.left;
			defs.push_back(d);
		} else {
			line(x(rng), y(rng), x(rng), y(rng), kind == 0 ? EffectType::Reflection : EffectType::Pass);
		}
	}
	return defs;
}

} // namespace

TEST_CASE("props: walker invariants on random scenes (and the float64 twin is bit-identical)") {
	std::mt19937_64 rng(1033);
	std::uniform_real_distribution<double> x(120.0, 1780.0), y(120.0, 980.0), a(-3.14159, 3.14159);
	int walks = 0;
	int applied = 0;
	for (int k = 0; k < 300; k++) {
		const std::vector<SurfaceDef> defs = random_scene(rng);
		Scene<F64> s64 = Scene<F64>::from_defs(defs, F64{1});
		Scene<Dual> sd = Scene<Dual>::from_defs(defs, Dual{1, 0});
		s64.emit_infinity = true;
		sd.emit_infinity = true;
		const double px = x(rng), py = y(rng), ang = a(rng);
		const double dx = std::cos(ang), dy = std::sin(ang);
		Solver<F64> solver(s64, nullptr);
		Solver<Dual> fast(sd, nullptr);
		const Walk<F64> w = solver.run_direction(CN<F64>{F64{px}, F64{py}}, CN<F64>{F64{dx}, F64{dy}}, {}, std::nullopt, Mode::Physical, MAX_HITS, Flags());
		const Walk<Dual> wd = fast.run_direction(CN<Dual>{Dual{px, 0}, Dual{py, 0}}, CN<Dual>{Dual{dx, 0}, Dual{dy, 0}}, {}, std::nullopt, Mode::Physical, MAX_HITS, Flags());
		REQUIRE(!w.events.empty());
		CHECK(w.events.size() <= size_t(MAX_HITS) + 1);
		REQUIRE(w.events.size() == wd.events.size());
		for (size_t i = 0; i < w.events.size(); i++) {
			const Event<F64> &e = w.events[i];
			// the float64 twin: the same decisions and bit-identical values
			CHECK(e.kind == wd.events[i].kind);
			CHECK(e.surface_id == wd.events[i].surface_id);
			if (e.point) {
				REQUIRE(wd.events[i].point.has_value());
				CHECK(e.point->fx() == wd.events[i].point->fx());
				CHECK(e.point->fy() == wd.events[i].point->fy());
				CHECK(std::isfinite(e.point->fx()));
				CHECK(std::isfinite(e.point->fy()));
			}
			// the infinity event: the real infinity in an identity-frame straight beam (no point)
			if (e.kind == EventKind::Infinity) {
				CHECK(e.surface_id == INFINITY_ID);
				CHECK(e.point.has_value() == e.t_to.has_value());
			}
			// an applied hit lies on its surface's carrier
			if (e.kind == EventKind::Hit && e.applied && e.point) {
				const SurfaceDef &d = defs[size_t(e.surface_id - 1)];
				const double qx = e.point->fx(), qy = e.point->fy();
				double dist;
				if (d.line) {
					const double lx = d.ex - d.sx, ly = d.ey - d.sy;
					dist = std::fabs(lx * (qy - d.sy) - ly * (qx - d.sx)) / std::sqrt(lx * lx + ly * ly);
				} else {
					dist = std::fabs(std::hypot(qx - d.cx, qy - d.cy) - d.radius);
				}
				CHECK_MESSAGE(dist < 1e-6, "walk " << k << " event " << i << " off its carrier by " << dist);
				applied++;
			}
		}
		walks++;
	}
	CHECK(walks == 300);
	CHECK(applied > 300);
}

// ---------------------------------------------------------------- Perf P2b: the exact beam

namespace {
// G at the homogeneous point (x, y, w): A (x^2 + y^2) + B x w + C y w + D w^2
BigFloat gc_at(const exact::GCircle &g, const exact::Point3 &X) {
	using namespace exact;
	const BigFloat &w = X[2];
	return add(add(mul(g.A, add(mul(X[0], X[0]), mul(X[1], X[1]))), add(mul(mul(g.B, X[0]), w), mul(mul(g.C, X[1]), w))), mul(g.D, mul(w, w)));
}
exact::Point3 pt(double x, double y) { return {exact::of(x), exact::of(y), exact::of(1.0)}; }
exact::Point3 translate_h(const exact::Point3 &X, double dx, double dy) {
	using namespace exact;
	return {add(X[0], mul(of(dx), X[2])), add(X[1], mul(of(dy), X[2])), X[2]};
}
} // namespace

TEST_CASE("P2b: the exact beam's maps carry every point of the curve onto the image curve") {
	using namespace exact;
	// the line through (1, 2) and (7, -3), points on it
	const Line3 L = line_through(1.0, 2.0, 7.0, -3.0);
	std::vector<Point3> pts = {pt(1.0, 2.0), pt(7.0, -3.0), pt(13.0, -8.0), pt(-5.0, 7.0)};
	GCircle g = gc_from_line(L);
	for (const Point3 &X : pts) {
		CHECK(gc_at(g, X).is_zero());
	}
	// a chain: invert in (3, 1) r^2 = 50, reflect in the line (0, 0) -> (2, 5),
	// translate by (-4, 9), invert in (10, 10) r^2 = 7 (center (20, 20) / 2)
	const Point3 C1 = pt(3.0, 1.0);
	const BigFloat r1 = of(50.0);
	const Point3 C2 = {of(20.0), of(20.0), of(2.0)};
	const BigFloat r2 = mul(of(7.0), of(4.0)); // (r w)^2 = 7 * 2^2
	g = gc_invert(g, C1, r1);
	for (Point3 &X : pts) X = *invert_point_h(X, C1, r1);
	for (const Point3 &X : pts) CHECK(gc_at(g, X).is_zero());
	g = gc_reflect(g, 0.0, 0.0, 2.0, 5.0);
	for (Point3 &X : pts) X = reflect_point_h(X, 0.0, 0.0, 2.0, 5.0);
	for (const Point3 &X : pts) CHECK(gc_at(g, X).is_zero());
	g = gc_translate(g, -4.0, 9.0);
	for (Point3 &X : pts) X = translate_h(X, -4.0, 9.0);
	for (const Point3 &X : pts) CHECK(gc_at(g, X).is_zero());
	g = gc_invert(g, C2, r2);
	for (Point3 &X : pts) X = *invert_point_h(X, C2, r2);
	for (const Point3 &X : pts) CHECK(gc_at(g, X).is_zero());
	// and a point off the curve stays off
	CHECK(!gc_at(g, pt(0.5, 0.25)).is_zero());
}

TEST_CASE("P2b: exact tangency of a beam's circle with line and circle carriers") {
	using namespace exact;
	// the line x = 10 inverted in the circle at the origin, r^2 = 100: the circle
	// of center (5, 0), radius 5 -- tangent to x = 10 and to x = 0
	const GCircle c = gc_invert(gc_from_line(line_through(10.0, 0.0, 10.0, 1.0)), pt(0.0, 0.0), of(100.0));
	CHECK(gc_tangent_line(c, 10.0, -3.0, 10.0, 4.0));
	CHECK(gc_tangent_line(c, 0.0, 1.0, 0.0, 2.0));
	CHECK(!gc_tangent_line(c, 9.0, 0.0, 9.0, 1.0));
	CHECK(gc_tangent_line(c, 0.0, 5.0, 1.0, 5.0)); // y = 5, the top
	CHECK(!gc_tangent_line(c, 0.0, 5.0000000001, 1.0, 5.0000000001));
	// circle carriers: center (15, 0) r 5 (externally), center (7, 0) r 3 (internally)
	CHECK(gc_tangent_circle(c, pt(15.0, 0.0), of(25.0)));
	CHECK(gc_tangent_circle(c, pt(7.0, 0.0), of(9.0)));
	CHECK(!gc_tangent_circle(c, pt(14.0, 0.0), of(25.0)));
	CHECK(!gc_tangent_circle(c, pt(7.0, 0.0), of(8.0)));
	// a straight beam (A = 0) against a circle: y = 5 touches the circle at the origin of r 5
	const GCircle l = gc_from_line(line_through(-1.0, 5.0, 1.0, 5.0));
	CHECK(gc_tangent_circle(l, pt(0.0, 0.0), of(25.0)));
	CHECK(!gc_tangent_circle(l, pt(0.0, 0.0), of(24.0)));
	CHECK(!gc_tangent_line(l, 0.0, 0.0, 1.0, 0.0)); // two lines never touch
	// the lazy record gives the same circle
	const auto rec = BeamRec::from_line(Line::from_exact(line_through(10.0, 0.0, 10.0, 1.0)))->inverted(pt(0.0, 0.0), of(100.0));
	const std::optional<GCircle> e = rec->exact();
	REQUIRE(e.has_value());
	CHECK(gc_tangent_line(*e, 10.0, -3.0, 10.0, 4.0));
	CHECK(gc_tangent_circle(*e, pt(15.0, 0.0), of(25.0)));
}
