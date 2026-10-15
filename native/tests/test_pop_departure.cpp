// Stage 106: the departure point after a pop (GAME_SPEC 11.4; K2). A pop tags
// the pop point as the restored beam's departure point, like a push does; on
// a resumed beam the departing carrier's root nearest the departure parameter
// is the departure. The fixtures themselves are checked against the oracle in
// test_infinity.cpp; here: the tag, the invariant, and tier independence on
// random scenes whose players and cursors stand exactly on circle mirrors.
#include "json_defs.hpp"
#include "rc/invariants.hpp"
#include "rc/solver.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace rc;

namespace {

std::vector<SurfaceDef> scene_dump(const char *name) {
	std::ifstream is(std::string(RC_REPO_ROOT) + "/bench/scenes/" + name + ".surfaces.json");
	return defs_from_json(ojson::parse(is)["surfaces"]);
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

SurfaceDef circle_def(int id, double cx, double cy, double r, EffectType t) {
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
	side.interactive = true;
	d.left = side;
	d.right = side;
	return d;
}

std::vector<SurfaceDef> box(int first) {
	return {line_def(first, 0, 0, 1920, 0, EffectType::Terminal), line_def(first + 1, 1920, 0, 1920, 1080, EffectType::Terminal),
		line_def(first + 2, 1920, 1080, 0, 1080, EffectType::Terminal), line_def(first + 3, 0, 1080, 0, 0, EffectType::Terminal)};
}

template <Num N>
std::vector<inv::EvInfo> infos(const std::vector<Event<N>> &evs) {
	std::vector<inv::EvInfo> out;
	for (const Event<N> &e : evs) {
		out.push_back(inv::ev_info(e));
	}
	return out;
}

template <Num N>
std::string structure(const std::vector<Event<N>> &evs, bool hits_only = false) {
	std::string out;
	for (const Event<N> &e : evs) {
		if (hits_only && e.kind != EventKind::Hit) {
			continue;
		}
		out += std::string(event_kind_name(e.kind)) + std::to_string(e.surface_id) + (e.applied ? "*" : "") + " ";
	}
	return out;
}

template <Num N>
std::optional<Walk<N>> run(const std::vector<SurfaceDef> &defs, const N &like, double px, double py, double cx, double cy,
		const std::vector<PlanItem> &plan, Mode mode, bool record = false) {
	Scene<N> sc = Scene<N>::from_defs(defs, like);
	sc.emit_infinity = true;
	Solver<N> solver(sc, nullptr);
	solver.set_record_poles(record);
	return solver.run(CN<N>{like.like(px), like.like(py)}, CN<N>{like.like(cx), like.like(cy)}, plan, mode, MAX_HITS, Flags());
}

} // namespace

TEST_CASE("Stage 106: a pop at the player's point tags it as the departure point (K2)") {
	const std::vector<SurfaceDef> defs = scene_dump("playground__tangent_infinity");
	const std::vector<PlanItem> mirror_q0 = {PlanItem{6, true, std::nullopt}};
	// the player (1160, 440) on the inversion circle 8, the aim image under the
	// mirror plan at its bottom (1160, 640): in at the image, out at the player
	const std::optional<Walk<F64>> w = run<F64>(defs, F64{1}, 1160, 440, 760, 640, mirror_q0, Mode::Physical, true);
	REQUIRE(w.has_value());
	bool tagged = false;
	for (const auto &known : w->known_trace) {
		for (const Known<F64> &k : known) {
			if (k.skip == 8 && std::hypot(k.p.fx() - 1160.0, k.p.fy() - 440.0) < 1e-9) {
				tagged = true;
			}
		}
	}
	CHECK(tagged);
	REQUIRE(w->events.size() >= 4);
	CHECK(w->events[0].surface_id == 8); // in at the image
	CHECK(w->events[2].surface_id == 8); // out at the player
	CHECK(w->events[2].applied);
	CHECK(std::hypot(w->events[2].point->fx() - 1160.0, w->events[2].point->fy() - 440.0) < 1e-9);
	CHECK(std::hypot(w->events[3].point->fx() - 1160.0, w->events[3].point->fy() - 640.0) < 1e-9); // the other crossing, not the player again
	CHECK(inv::check_reapply_in_place(infos(w->events)).empty());
	// the same walk as the shot aimed at the image with no plan (hits only: the aimed shot has a cursor event)
	const std::optional<Walk<F64>> ref = run<F64>(defs, F64{1}, 1160, 440, 1160, 640, {}, Mode::Physical);
	REQUIRE(ref.has_value());
	CHECK(structure(w->events, true) == structure(ref->events, true));
	// at 4 limbs the same decisions
	const std::optional<Walk<BigFloat>> w4 = run<BigFloat>(defs, BigFloat::from_int(1, 4), 1160, 440, 760, 640, mirror_q0, Mode::Physical);
	REQUIRE(w4.has_value());
	CHECK(structure(w4->events) == structure(w->events));
}

TEST_CASE("Stage 106: pops at known points on random scenes: every tier agrees, nothing re-applies in place") {
	std::mt19937_64 rng(1061);
	int walks = 0;
	int pops = 0;
	int differing = 0;
	for (int k = 0; k < 300; k++) {
		// a full circle mirror or inversion circle on the grid, maybe a second one, a box
		const double cx = 64.0 * double(6 + rng() % 18), cy = 64.0 * double(4 + rng() % 9);
		const double r = 50.0 * double(2 + rng() % 4);
		std::vector<SurfaceDef> defs = {circle_def(1, cx, cy, r, (k % 3 == 0) ? EffectType::Reflection : EffectType::Inversion)};
		if (k % 2 == 1) {
			defs.push_back(circle_def(2, cx + 64.0 * double(4 + rng() % 6) * (rng() % 2 ? 1.0 : -1.0), cy + 64.0 * double(rng() % 5) - 128.0,
				50.0 * double(1 + rng() % 3), EffectType::Inversion));
		}
		for (const SurfaceDef &d : box(90)) {
			defs.push_back(d);
		}
		// the player exactly on the first circle (an extreme point), the cursor on it,
		// inside it, or anywhere
		const double ext[4][2] = {{cx + r, cy}, {cx - r, cy}, {cx, cy + r}, {cx, cy - r}};
		const double *P = ext[rng() % 4];
		const double *X = ext[rng() % 4];
		double qx = X[0], qy = X[1];
		if (rng() % 3 == 0) {
			qx = cx + double(int(rng() % 9) - 4) * r / 5.0;
			qy = cy + double(int(rng() % 9) - 4) * r / 5.0;
		} else if (rng() % 3 == 0) {
			qx = 64.0 * double(2 + rng() % 26);
			qy = 64.0 * double(1 + rng() % 15);
		}
		if (qx == P[0] && qy == P[1]) {
			continue;
		}
		const std::vector<std::vector<PlanItem>> plans = {{}, {PlanItem{1, true, std::nullopt}}};
		for (const auto &plan : plans) {
			for (Mode mode : {Mode::Planned, Mode::Physical}) {
				const auto a = run<F64>(defs, F64{1}, P[0], P[1], qx, qy, plan, mode);
				const auto b = run<Dual>(defs, Dual{1, 0}, P[0], P[1], qx, qy, plan, mode);
				const auto c = run<BigFloat>(defs, BigFloat::from_int(1, 4), P[0], P[1], qx, qy, plan, mode);
				const auto e = run<BigFloat>(defs, BigFloat::from_int(1, 64), P[0], P[1], qx, qy, plan, mode);
				if (!a || !b || !c || !e) {
					continue;
				}
				walks++;
				for (const Event<F64> &ev : a->events) {
					if (ev.popped) {
						pops++;
					}
				}
				CHECK_MESSAGE(structure(a->events) == structure(b->events), "walk " << k << " f64 vs dual");
				if (structure(a->events) != structure(e->events) || structure(c->events) != structure(e->events)) {
					differing++;
					MESSAGE("walk " << k << " plan " << plan.size() << " mode " << int(mode) << "\n  f64 " << structure(a->events) << "\n  b4  "
										<< structure(c->events) << "\n  b64 " << structure(e->events));
				}
				CHECK_MESSAGE(inv::check_reapply_in_place(infos(a->events)).empty(), "walk " << k << " re-applies in place: " << structure(a->events));
				CHECK_MESSAGE(inv::check_reapply_in_place(infos(c->events)).empty(), "walk " << k << " re-applies in place at 4 limbs");
			}
		}
	}
	MESSAGE("walks: " << walks << ", pops at float64: " << pops << ", tiers differing: " << differing);
	CHECK(walks > 800);
	CHECK(pops > 200);
	CHECK(differing == 0);
}
