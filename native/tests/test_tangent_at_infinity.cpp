// Stage 105: a beam tangent to a carrier at the frame's infinity point is a
// touch, decided exactly from the pole record (rc/exact.hpp ExactPole,
// Walk::pole). The fixtures' replay is in test_infinity.cpp; here: the record's
// transitions against independent numerics, the record against the beams it
// rides on, and the property the old rounding broke (known finding K1): on
// scenes full of exact coincidences, every precision tier makes the same
// decisions.
#include "json_defs.hpp"
#include "parts.hpp"
#include "rc/solver.hpp"

#include <doctest/doctest.h>
#include <set>
#include <map>
#include <cmath>
#include <algorithm>

#include <cmath>
#include <random>

using namespace rc;

namespace {

double fx(const BigFloat &b) { return b.to_float(); }
double qx(const exact::ExactPole &p) { return p.Q[0].to_float() / p.Q[2].to_float(); }
double qy(const exact::ExactPole &p) { return p.Q[1].to_float() / p.Q[2].to_float(); }
// |sin| of the angle between the record's direction and (tx, ty)
double skew(const exact::ExactPole &p, double tx, double ty) {
	const double dx = fx(p.dx), dy = fx(p.dy);
	return std::fabs(dx * ty - dy * tx) / (std::hypot(dx, dy) * std::hypot(tx, ty));
}
exact::ExactPole pole_at(double x, double y, double dx, double dy) {
	return exact::ExactPole{{exact::of(x), exact::of(y), exact::of(1.0)}, exact::of(dx), exact::of(dy)};
}

SurfaceDef line_def(int id, double sx, double sy, double ex, double ey, EffectType t) {
	SurfaceDef d;
	d.id = id;
	d.line = true;
	d.sx = sx; d.sy = sy; d.ex = ex; d.ey = ey;
	d.has_via = true; d.vx = (sx + ex) / 2.0; d.vy = (sy + ey) / 2.0;
	SideDef side;
	side.type = t;
	side.interactive = t != EffectType::Pass && t != EffectType::Terminal;
	d.left = side;
	d.right = side;
	return d;
}

SurfaceDef circle_def(int id, double cx, double cy, double r, EffectType t, bool pre = false) {
	SurfaceDef d;
	d.id = id;
	d.line = false;
	d.full = true;
	d.has_circle = true;
	d.cx = cx; d.cy = cy; d.radius = r;
	d.sx = cx + r; d.sy = cy;
	d.ex = cx - r / 2.0; d.ey = cy - 0.866 * r;
	d.has_via = true; d.vx = cx - r / 2.0; d.vy = cy + 0.866 * r;
	SideDef side;
	side.type = t;
	side.interactive = !pre;
	d.left = side;
	d.right = side;
	d.pre_transform = pre;
	return d;
}

} // namespace

TEST_CASE("Stage 105: the pole transitions match independent numerics") {
	std::mt19937_64 rng(1051);
	std::uniform_real_distribution<double> u(-1.0, 1.0);
	for (int k = 0; k < 500; k++) {
		const double x = std::round(u(rng) * 800.0), y = std::round(u(rng) * 800.0);
		const double ang = u(rng) * 3.14159;
		const double dx = std::cos(ang), dy = std::sin(ang);
		const exact::ExactPole p = pole_at(x, y, dx, dy);
		// reflection in the line s -> e: the point mirrored, the direction mirrored
		const double sx = std::round(u(rng) * 500.0), sy = std::round(u(rng) * 500.0);
		const double ex = sx + std::round(u(rng) * 300.0) + 1.0, ey = sy + std::round(u(rng) * 300.0);
		const std::optional<exact::ExactPole> r = exact::pole_reflect(p, sx, sy, ex, ey);
		REQUIRE(r.has_value());
		{
			const double mx = ex - sx, my = ey - sy, mm = mx * mx + my * my;
			const double vx = x - sx, vy = y - sy;
			const double along = (vx * mx + vy * my) / mm;
			const double rx = sx + 2.0 * along * mx - vx, ry = sy + 2.0 * along * my - vy;
			CHECK(std::fabs(qx(*r) - rx) < 1e-9 * (1.0 + std::fabs(rx)));
			CHECK(std::fabs(qy(*r) - ry) < 1e-9 * (1.0 + std::fabs(ry)));
			const double da = (dx * mx + dy * my) / mm;
			CHECK(skew(*r, 2.0 * da * mx - dx, 2.0 * da * my - dy) < 1e-12);
		}
		// inversion in the circle (cx, cy, rad): the point inverted, the direction
		// the differential's image (central difference of the map along D)
		const double cx = std::round(u(rng) * 500.0), cy = std::round(u(rng) * 500.0), rad = 50.0 + std::round(std::fabs(u(rng)) * 200.0);
		if (cx == x && cy == y) {
			continue;
		}
		const exact::Point3 C{exact::of(cx), exact::of(cy), exact::of(1.0)};
		const std::optional<exact::ExactPole> v = exact::pole_invert(p, C, exact::mul(exact::of(rad), exact::of(rad)));
		REQUIRE(v.has_value());
		auto inv = [&](double px, double py, double &ox, double &oy) {
			const double ux = px - cx, uy = py - cy, uu = ux * ux + uy * uy;
			ox = cx + rad * rad * ux / uu;
			oy = cy + rad * rad * uy / uu;
		};
		double ix, iy, ax, ay, bx, by;
		inv(x, y, ix, iy);
		CHECK(std::fabs(qx(*v) - ix) < 1e-9 * (1.0 + std::fabs(ix)));
		CHECK(std::fabs(qy(*v) - iy) < 1e-9 * (1.0 + std::fabs(iy)));
		const double h = 1e-4 * (1.0 + std::hypot(x - cx, y - cy));
		inv(x + h * dx, y + h * dy, ax, ay);
		inv(x - h * dx, y - h * dy, bx, by);
		CHECK(skew(*v, ax - bx, ay - by) < 1e-6);
		// translation: the point moved, the direction kept
		const exact::ExactPole t = exact::pole_translate(p, 300.0, -40.0);
		CHECK(qx(t) == x + 300.0);
		CHECK(qy(t) == y - 40.0);
		CHECK(skew(t, dx, dy) < 1e-15);
	}
	// the center goes to infinity; a zero direction is no record
	const exact::Point3 C0{exact::of(10.0), exact::of(20.0), exact::of(1.0)};
	CHECK(!exact::pole_invert(pole_at(10, 20, 1, 0), C0, exact::of(25.0)).has_value());
	CHECK(!exact::pole_start_direction(10, 20, 1, 0, C0).has_value()); // the player at Q0
	// a straight beam on the line y = 3 inverted: Q = the center, the direction the line's
	const std::optional<exact::ExactPole> fl = exact::pole_from_line(exact::line_from_dir(0, 3, 1, 0), C0);
	REQUIRE(fl.has_value());
	CHECK(qx(*fl) == 10.0);
	CHECK(skew(*fl, 1, 0) == 0.0);
}

TEST_CASE("Stage 105: the start records are the initial beam's tangent at Q0") {
	std::mt19937_64 rng(1052);
	std::uniform_real_distribution<double> u(-1.0, 1.0);
	for (int k = 0; k < 500; k++) {
		const double q0x = std::round(u(rng) * 400.0), q0y = std::round(u(rng) * 400.0);
		const double px = std::round(u(rng) * 400.0) + 0.5, py = std::round(u(rng) * 400.0);
		const double ang = u(rng) * 3.14159;
		const exact::Point3 Q0{exact::of(q0x), exact::of(q0y), exact::of(1.0)};
		const CN<F64> P{F64{px}, F64{py}}, Q{F64{q0x}, F64{q0y}};
		// a direction shot: the circle through Q0 tangent to dir at P
		const CN<F64> dir{F64{std::cos(ang)}, F64{std::sin(ang)}};
		const Beam<F64> bd = Beam<F64>::through_tangent(P, dir, Q);
		const std::optional<exact::ExactPole> sd = exact::pole_start_direction(px, py, dir.fx(), dir.fy(), Q0);
		REQUIRE(sd.has_value());
		const CN<F64> td = bd.tangent_at(bd.param_of(Q));
		CHECK(skew(*sd, td.fx(), td.fy()) < 1e-6);
		// an aimed shot: the circle through P, X, Q0
		const double xx = std::round(u(rng) * 400.0) - 0.25, xy = std::round(u(rng) * 400.0);
		const CN<F64> X{F64{xx}, F64{xy}};
		const CN<F64> delta = P.sub(Q);
		const Beam<F64> ba = Beam<F64>::through(P, X, &delta);
		const exact::Point3 Xh{exact::of(xx), exact::of(xy), exact::of(1.0)};
		const std::optional<exact::ExactPole> sa = exact::pole_start_aimed(px, py, Xh, Q0);
		if (!sa) {
			continue;
		}
		const CN<F64> ta = ba.tangent_at(ba.param_of(Q));
		CHECK(skew(*sa, ta.fx(), ta.fy()) < 1e-6);
	}
}

namespace {

// A scene whose exact coincidences are all at the frame's infinity point:
// either a pre-transform circle (Q0 = its center from the start) or an
// inversion circle (Q = its center once the beam inverts), with mirrors and
// pass lines through that point along and across its axes, a pass circle
// tangent there, and at most one unrelated mirror; no two carriers coincide
// (coinciding carriers tie everywhere, not just at Q).
std::vector<SurfaceDef> coincidence_scene(std::mt19937_64 &rng, double &q0x, double &q0y, bool pre) {
	std::vector<SurfaceDef> defs;
	q0x = 64.0 * double(5 + rng() % 18);
	q0y = 64.0 * double(4 + rng() % 8);
	int id = 1;
	const double rad = 32.0 * double(2 + rng() % 4);
	defs.push_back(circle_def(id++, q0x, q0y, rad, EffectType::Inversion, pre));
	const int n = 2 + int(rng() % 3);
	bool used[4] = {false, false, false, false}; // one carrier of each kind: no coinciding carriers
	for (int i = 0; i < n; i++) {
		const int kind = int(rng() % 4);
		if (kind < 3 && used[kind]) {
			continue;
		}
		used[kind] = true;
		const double l = 64.0 * double(1 + rng() % 4) + 7.0; // ends off the grid: no exact endpoint hits
		if (kind == 0) {
			defs.push_back(line_def(id++, q0x, q0y - l, q0x, q0y + l, EffectType::Reflection)); // vertical through the point
		} else if (kind == 1) {
			defs.push_back(line_def(id++, q0x - l, q0y, q0x + l, q0y, i % 2 ? EffectType::Pass : EffectType::Reflection)); // horizontal
		} else if (kind == 2) {
			const double r = 32.0 * double(1 + rng() % 4);
			defs.push_back(circle_def(id++, q0x + r, q0y, r, EffectType::Pass)); // tangent to the vertical there
		} else if (i == n - 1) {
			const double ax = 64.0 * double(1 + rng() % 28) + 16.0, ay = 64.0 * double(1 + rng() % 15) + 8.0;
			defs.push_back(line_def(id++, ax, ay, ax + 96.0, ay + 40.0, EffectType::Reflection)); // unrelated
		}
	}
	return defs;
}

// The walk's events while its frame is exact (the record, or an exact line
// for a straight beam): where the frame is not exact (an inversion of a beam
// through its center leaves a straight beam without an exact line) rounding
// decides again, which is the precision ladder's business, not this rule's.
template <Num N>
std::vector<std::string> structure(const std::vector<SurfaceDef> &defs, const N &like, double px, double py, double dx, double dy, bool &cut) {
	Scene<N> sc = Scene<N>::from_defs(defs, like);
	sc.emit_infinity = true;
	Solver<N> solver(sc, nullptr);
	solver.set_record_poles(true);
	const Walk<N> w = solver.run_direction(CN<N>{like.like(px), like.like(py)}, CN<N>{like.like(dx), like.like(dy)}, {}, std::nullopt,
		Mode::Physical, MAX_HITS, Flags());
	std::set<const void *> inexact;
	std::map<const void *, std::pair<double, double>> q_of; // beam -> the frame's Q (a record)
	for (size_t i = 0; i < w.pole_trace.size(); i++) {
		const auto &[beam, pole] = w.pole_trace[i];
		if (!w.exact_trace[i]) {
			inexact.insert(beam.get());
		}
		if (pole) {
			q_of[beam.get()] = {qx(*pole), qy(*pole)};
		}
	}
	std::vector<std::string> out;
	for (const Event<N> &e : w.events) {
		if (e.beam && inexact.count(e.beam.get())) {
			cut = true;
			break;
		}
		// "@q": the event is at the frame's infinity point
		bool at_q = false;
		if (e.point && e.beam && q_of.count(e.beam.get())) {
			const auto [x, y] = q_of[e.beam.get()];
			at_q = std::hypot(e.point->fx() - x, e.point->fy() - y) < 1e-6;
		}
		out.push_back(std::string(event_kind_name(e.kind)) + std::to_string(e.surface_id) + (e.applied ? "*" : "") + (at_q ? "@q" : ""));
	}
	return out;
}

std::string joined(const std::vector<std::string> &v, size_t n) {
	std::string s;
	for (size_t i = 0; i < n && i < v.size(); i++) {
		s += v[i] + " ";
	}
	return s;
}

} // namespace

TEST_CASE("Stage 105: on coincidence-rich scenes every tier makes the same decisions (K1's property)") {
	std::mt19937_64 rng(1053);
	// RC_CASE_PART=k/n: this part walks the k-th scene of every n (the scenes are
	// drawn as in the whole run: the rng is consumed before the part is skipped)
	const rc_test::Part part = rc_test::part_of("RC_CASE_PART");
	int walks = 0;
	int differing = 0;
	int compared = 0;
	int whole = 0;
	int taken = 0;
	for (int k = 0; k < 400; k++) {
		double q0x = 0, q0y = 0;
		const std::vector<SurfaceDef> defs = coincidence_scene(rng, q0x, q0y, k % 2 == 0);
		// a player on the grid, aims along the axes and the grid's diagonals
		const double px = q0x + 64.0 * (double(rng() % 9) - 4.0) + 32.0, py = q0y + 64.0 * (double(rng() % 7) - 3.0) + 16.0;
		const double dirs[][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {2, -1}, {-3, 4}};
		const double *d = dirs[rng() % 7];
		if (!part.takes(k)) {
			continue;
		}
		taken++;
		bool cut_a = false, cut_b = false, cut_c = false, cut_e = false;
		const std::vector<std::string> a = structure<F64>(defs, F64{1}, px, py, d[0], d[1], cut_a);
		const std::vector<std::string> b = structure<Dual>(defs, Dual{1, 0}, px, py, d[0], d[1], cut_b);
		const std::vector<std::string> c = structure<BigFloat>(defs, BigFloat::from_int(1, 4), px, py, d[0], d[1], cut_c);
		const std::vector<std::string> e = structure<BigFloat>(defs, BigFloat::from_int(1, 64), px, py, d[0], d[1], cut_e);
		CHECK_MESSAGE(a == b, "walk " << k << " f64 vs dual\n  " << joined(a, a.size()) << "\n  " << joined(b, b.size()));
		// compared while every tier's frame is exact
		const size_t n = std::min({a.size(), c.size(), e.size()});
		compared += int(n);
		if (!cut_a && !cut_c && !cut_e) {
			whole++;
		}
		if (joined(a, n) != joined(e, n) || joined(c, n) != joined(e, n)) {
			// where the tiers part: never at Q (the rule's business); a tie at a
			// point that is not Q (two carriers crossing where the beam passes)
			// is the precision ladder's
			size_t i = 0;
			while (i < n && a[i] == e[i] && c[i] == e[i]) {
				i++;
			}
			const auto at_q = [](const std::string &tok) { return tok.find("@q") != std::string::npos; };
			const bool q_tie = at_q(a[i]) || at_q(c[i]) || at_q(e[i]) || (i > 0 && at_q(e[i - 1]));
			CHECK_MESSAGE(!q_tie, "walk " << k << " parts at Q (event " << i << ")");
			differing++;
			MESSAGE("walk " << k << " differs\n  f64 " << joined(a, n) << "\n  b4  " << joined(c, n) << "\n  b64 " << joined(e, n));
		}
		walks++;
	}
	MESSAGE("events compared: " << compared << ", walks exact throughout: " << whole << ", parting away from Q: " << differing << " (part " << part.k << "/"
																				<< part.n << ")");
	CHECK(compared * part.n > 4000);
	CHECK(whole * part.n > 200);
	CHECK(walks == taken);
	CHECK(differing <= 4); // ties away from Q are rare in these scenes
}

TEST_CASE("Stage 105: wherever the record exists it rides on the beam: Q on it, D its tangent there") {
	std::mt19937_64 rng(1054);
	int checked = 0;
	for (int k = 0; k < 300; k++) {
		double q0x = 0, q0y = 0;
		const std::vector<SurfaceDef> defs = coincidence_scene(rng, q0x, q0y, k % 2 == 0);
		const double px = q0x + 64.0 * (double(rng() % 9) - 4.0) + 21.0, py = q0y + 64.0 * (double(rng() % 7) - 3.0) + 13.0;
		const double ang = double(rng() % 628) / 100.0;
		for (int tier = 0; tier < 2; tier++) {
			if (tier == 0) {
				Scene<F64> sc = Scene<F64>::from_defs(defs, F64{1});
				sc.emit_infinity = true;
				Solver<F64> solver(sc, nullptr);
				solver.set_record_poles(true);
				const Walk<F64> w = solver.run_direction(CN<F64>{F64{px}, F64{py}}, CN<F64>{F64{std::cos(ang)}, F64{std::sin(ang)}}, {}, std::nullopt,
					Mode::Physical, MAX_HITS, Flags());
				for (const auto &[beam, pole] : w.pole_trace) {
					if (!pole) {
						continue;
					}
					const CN<F64> Q{F64{qx(*pole)}, F64{qy(*pole)}};
					const F64 t = beam->param_of(Q);
					const CN<F64> on = beam->at(t);
					CHECK(std::hypot(on.fx() - Q.fx(), on.fy() - Q.fy()) < 1e-6 * (1.0 + std::hypot(Q.fx(), Q.fy())));
					const CN<F64> tg = beam->tangent_at(t);
					CHECK(skew(*pole, tg.fx(), tg.fy()) < 1e-6);
					checked++;
				}
			} else {
				const BigFloat l = BigFloat::from_int(1, 8);
				Scene<BigFloat> sc = Scene<BigFloat>::from_defs(defs, l);
				sc.emit_infinity = true;
				Solver<BigFloat> solver(sc, nullptr);
				solver.set_record_poles(true);
				const Walk<BigFloat> w = solver.run_direction(CN<BigFloat>{l.like(px), l.like(py)}, CN<BigFloat>{l.like(std::cos(ang)), l.like(std::sin(ang))}, {},
					std::nullopt, Mode::Physical, MAX_HITS, Flags());
				for (const auto &[beam, pole] : w.pole_trace) {
					if (!pole) {
						continue;
					}
					// Q at the tier: the exact homogeneous coordinates divided at 8 limbs
					const CN<BigFloat> Q{pole->Q[0].with_limbs(8).div(pole->Q[2].with_limbs(8)), pole->Q[1].with_limbs(8).div(pole->Q[2].with_limbs(8))};
					const BigFloat t = beam->param_of(Q);
					const CN<BigFloat> tg = beam->tangent_at(t);
					const BigFloat ddx = pole->dx.with_limbs(8), ddy = pole->dy.with_limbs(8);
					const BigFloat cr = tg.re.mul(ddy).sub(tg.im.mul(ddx)).abs_val();
					const BigFloat nd = ddx.mul(ddx).add(ddy.mul(ddy));
					// |sin|^2 below 1e-40: cr^2 <= 1e-40 |D|^2 |tg|^2 (|tg| = 1)
					CHECK(cr.mul(cr).cmp(nd.mul(l.like(1e-40))) <= 0);
					checked++;
				}
			}
		}
	}
	MESSAGE("records checked: " << checked);
	CHECK(checked > 200);
}
