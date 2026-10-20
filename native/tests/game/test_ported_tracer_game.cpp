// Stage 119.3: retired GUT tests (the tracer rows of the retirement ledger,
// docs/research/gut_ledger_tracer.tsv) that need the game layer: the surface
// specs (rcg::validate_spec, rcg::build_spec), the levels (rcg::build_level)
// and the sweep inputs (rcg::sweeps, rcg::GodotRng). One case per GUT test
// function, same inputs, same assertions.
#include "rc/sweep.hpp"
#include "rcg/build.hpp"
#include "rcg/level.hpp"
#include "rcg/rng.hpp"
#include "rcg/sweeps.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;
using rc::V2;

const std::string ROOT = RC_REPO_ROOT;

// SideSpec.make({type, normal}, true, [])
rcg::SideSpec directional(const std::string &type, double nx, double ny) {
	rcg::SideSpec s;
	s.effect.type = type;
	s.effect.raw = {{"type", type}, {"normal", {nx, ny}}};
	s.effect.normal = std::array<double, 2>{nx, ny};
	s.interactive = true;
	return s;
}

// SurfaceSpec.arc(s, e, v, left, right); SurfaceSpec.line: the midpoint as the via
rcg::SurfaceSpec arc_spec(V2 s, V2 e, V2 v, const rcg::SideSpec &l, const rcg::SideSpec &r) {
	rcg::SurfaceSpec o;
	o.start = s;
	o.end = e;
	o.via = v;
	o.left = l;
	o.right = r;
	return o;
}
rcg::SurfaceSpec line_spec(V2 a, V2 b, const rcg::SideSpec &l, const rcg::SideSpec &r) {
	return arc_spec(a, b, V2::of((double(a.x) + double(b.x)) / 2.0, (double(a.y) + double(b.y)) / 2.0), l, r);
}

// test_stage84_no_legacy.gd _t: e^{i theta} z + d, float64 arithmetic, a float32 Vector2
V2 motion(V2 z, double theta, V2 d) {
	const double c = std::cos(theta), s = std::sin(theta);
	return V2::of(c * double(z.x) - s * double(z.y) + double(d.x), s * double(z.x) + c * double(z.y) + double(d.y));
}

// RigidMotionEffect.create_portal_pair(seg, theta, d) as a portal spec: both sides the portal
rcg::SurfaceSpec portal_spec(V2 s, V2 e, V2 v, double theta, V2 d) {
	rcg::SideSpec side;
	side.effect.type = "portal";
	side.effect.raw = {{"type", "portal"}, {"theta", theta}, {"d", {double(d.x), double(d.y)}}};
	side.effect.theta = theta;
	side.effect.d = std::array<double, 2>{double(d.x), double(d.y)};
	side.interactive = true;
	return arc_spec(s, e, v, side, side);
}

// ---------------------------------------------------------------- test_precision_invariant_sweep.gd

// The playground/inversion_grid scene: its defs, its surface infos (InvariantChecker.surface_infos)
// and the sweep positions of SweepRunner.configure(5, 10, 42) with its points of interest, as
// rcg::sweeps expands a data/sweeps.json group.
struct Grid {
	std::vector<rc::SurfaceDef> defs;
	std::vector<rc::inv::SurfaceInfo> infos;
	std::vector<V2> positions;
};

V2 v2_of(const json &p) { return V2{float(p[0].get<double>()), float(p[1].get<double>())}; }

Grid inversion_grid() {
	Grid g;
	g.defs = rcg::defs_of(rcg::build_level(rcg::load_level(ROOT + "/data/levels/playground/inversion_grid.json")));
	const json recipes = {{"groups", json::array({{{"name", "inversion_grid no plan"}, {"levels", {"playground/inversion_grid"}}, {"plans", "empty"},
		{"positions", {{"rule", "sweep"}, {"grid", 5}, {"fuzz", 10}, {"seed", 42}}}}})}};
	const auto ms = rcg::sweeps::manifests(recipes, ROOT + "/data/levels");
	REQUIRE(ms.size() == 1);
	const json &m = ms[0].second;
	for (const json &s : m["infos"]) {
		rc::inv::SurfaceInfo info;
		info.id = s["id"].get<int>();
		info.has_effect = s["has_effect"].get<bool>();
		const json &seg = s["segment"];
		info.s = v2_of(seg["start"]);
		info.e = v2_of(seg["end"]);
		info.v = seg["via"].is_null() ? V2{INFINITY, INFINITY} : v2_of(seg["via"]);
		info.full = seg["full"].get<bool>();
		const json &c = s["carrier"];
		info.carrier = rc::arc::GeneralizedCircle{c["a"].get<double>(), c["b"].get<double>(), c["c"].get<double>(), c["d"].get<double>()};
		g.infos.push_back(info);
	}
	for (const json &p : m["positions"]) {
		g.positions.push_back(v2_of(p));
	}
	return g;
}

// _chord_violations: a step drawn as a straight chord (an arc too large to draw)
// must be the path's own arc: via strictly inside the chord's arc <=> the
// inscribed angle at via is not acute
std::vector<std::string> chord_violations(const rc::TracePath &path) {
	std::vector<std::string> bad;
	for (const rc::TraceStep &s : path.steps) {
		if (!s.is_arc_step || std::isinf(s.start.x) || std::isinf(s.end.x) || std::isinf(s.via.x)) {
			continue;
		}
		if (rc::arc::is_arc(s.start, s.via, s.end)) {
			continue;
		}
		if (double((s.start - s.via).dot(s.end - s.via)) > 0.0) {
			bad.push_back("LONG-ARC-CHORD: chord drawn for a long-way arc");
		}
	}
	return bad;
}

std::string v2_str(const V2 &v) {
	char b[64];
	std::snprintf(b, sizeof b, "(%.9g, %.9g)", double(v.x), double(v.y));
	return b;
}

struct SweepResult {
	std::vector<std::string> failures;
	int fallbacks = 0;
	int runs = 0;
};

// _sweep: InvariantChecker.check_all on every (player, cursor != player, plan) through
// the renderer (rc::sweep::check_combo: the pair, its display paths, the merged steps),
// the combos without a beam pair counted, not asserted; then the chord check on the
// renderer's display paths
SweepResult sweep(const Grid &g, const std::vector<std::vector<int>> &plans, const std::vector<V2> &positions) {
	SweepResult r;
	rc::Tracer tracer;
	tracer.set_emit_infinity(true);
	tracer.set_defs(g.defs);
	rc::Scene<rc::F64> scene = rc::Scene<rc::F64>::from_defs(g.defs, rc::F64{1.0});
	scene.emit_infinity = true;
	const rc::Rect2f bounds = rc::Rect2f::of(0, 0, 1920, 1080);
	for (const std::vector<int> &ids : plans) {
		std::vector<rc::PlanItem> plan;
		for (int id : ids) {
			rc::PlanItem it;
			it.surface_id = id;
			it.left = true;
			plan.push_back(it);
		}
		for (const V2 &p : positions) {
			for (const V2 &c : positions) {
				if (p == c) {
					continue;
				}
				r.runs++;
				const std::optional<rc::TracePair> pair = tracer.trace_pair(p, c, plan, rc::Flags());
				if (!pair) {
					r.fallbacks++;
					continue;
				}
				const std::string at = "plan " + std::to_string(ids.size()) + " entries player " + v2_str(p) + " cursor " + v2_str(c) + ": ";
				for (const rc::inv::Violation &v : rc::sweep::check_combo(tracer, scene, g.defs, g.infos, p, c, plan, rc::Flags(), bounds)) {
					r.failures.push_back(at + v.fmt);
				}
				for (const rc::TracePath *path : {&pair->planned, &pair->physical}) {
					for (const std::string &v : chord_violations(rc::sweep::prepare_for_display(*path, bounds, g.infos))) {
						r.failures.push_back(at + v);
					}
				}
			}
		}
	}
	return r;
}

void report(const SweepResult &r, const std::string &label) {
	MESSAGE(label << ": " << r.runs << " runs, " << r.fallbacks << " without a beam pair (degenerate inputs), " << r.failures.size() << " violations");
	for (size_t i = 0; i < r.failures.size() && i < 12; i++) {
		MESSAGE(r.failures[i]);
	}
	CHECK_MESSAGE(r.failures.empty(), label);
}

} // namespace

TEST_CASE("ported: test_stage113_projective_visibility: test_b9_a_directional_normal_along_its_line_is_rejected_at_load") {
	const rcg::SideSpec along = directional("line_directional_projection", 0, 1);
	const rcg::SideSpec across = directional("line_directional_projection", 1, 0);
	CHECK_MESSAGE(!rcg::validate_spec(line_spec(V2{750, 300}, V2{750, 780}, along, along)).empty(), "the normal along the line: rejected");
	CHECK_MESSAGE(rcg::validate_spec(line_spec(V2{750, 300}, V2{750, 780}, across, across)).empty(), "across it: fine");
	const rcg::SideSpec slanted = directional("line_directional_projection", 1, 1);
	CHECK_MESSAGE(rcg::validate_spec(line_spec(V2{750, 300}, V2{750, 780}, slanted, slanted)).empty(), "slanted: fine");
	const rcg::SideSpec circ = directional("circle_directional_projection", 0, 1);
	CHECK_MESSAGE(rcg::validate_spec(arc_spec(V2{850, 390}, V2{850, 690}, V2{1000, 540}, circ, circ)).empty(), "a circle has no direction to run along");
}

TEST_CASE("ported: test_stage84_no_legacy: test_portal_pair_construction_without_frames") {
	// (the effects' carriers and Point's provenance are GDScript objects: the
	// native portal holds its motion only, the image segment its own carrier)
	const std::vector<std::array<V2, 3>> segs = {{V2{100, 100}, V2{100, 300}, V2{100, 200}}, {V2{200, 0}, V2{200, 200}, V2{300, 100}}};
	for (const auto &seg : segs) {
		for (const double theta : {0.0, M_PI / 2.0, 0.7, -2.1}) {
			const V2 d{400, -50};
			const std::vector<rcg::BuiltSurface> pp = rcg::build_spec(portal_spec(seg[0], seg[1], seg[2], theta, d), 1, std::nullopt);
			REQUIRE(pp.size() == 2);
			const rcg::BuiltSurface &a = pp[0], &b = pp[1];
			CHECK_MESSAGE(b.seg.s == motion(seg[0], theta, d), "start theta=" << theta);
			CHECK_MESSAGE(b.seg.e == motion(seg[1], theta, d), "end theta=" << theta);
			CHECK_MESSAGE(b.seg.v == motion(seg[2], theta, d), "via theta=" << theta);
			CHECK_MESSAGE(a.portal_pair == b.id, "source pairs with target");
			CHECK_MESSAGE(b.portal_pair == a.id, "target pairs with source");
			// the inverse motion: theta' = -theta, d' = -e^{-i theta} d
			REQUIRE(b.left);
			CHECK(b.left->effect.theta == -theta);
			CHECK(b.left->effect.d == -motion(d, -theta, V2{}));
		}
	}
	// beam scene pairing reads the partner (no tracked transforms)
	const std::vector<rc::SurfaceDef> defs =
		rcg::defs_of(rcg::build_spec(portal_spec(segs[0][0], segs[0][1], segs[0][2], 0.0, V2{500, 0}), 1, std::nullopt));
	REQUIRE(defs.size() == 2);
	REQUIRE(defs[0].left);
	REQUIRE(defs[1].left);
	CHECK(defs[0].left->pair == defs[1].id);
	CHECK(defs[1].left->pair == defs[0].id);
}

TEST_CASE("ported: test_precision_invariant_sweep: test_sweep_no_plan") {
	const Grid g = inversion_grid();
	report(sweep(g, {{}}, g.positions), "inversion_grid no plan");
}

TEST_CASE("ported: test_precision_invariant_sweep: test_sweep_with_plans") {
	const Grid g = inversion_grid();
	// SweepPlans.precision_positions: 10 seeded positions outside the grid's circles
	rcg::GodotRng rng(7);
	std::vector<V2> positions;
	while (positions.size() < 10) {
		const float x = rng.randf_range(40, 1880);
		const float y = rng.randf_range(40, 1040);
		const V2 p{x, y};
		bool inside = false;
		for (const float cx : {760.0f, 960.0f, 1160.0f}) {
			for (const float cy : {380.0f, 540.0f, 700.0f}) {
				inside = inside || p.distance_to(V2{cx, cy}) < 62.0f;
			}
		}
		if (!inside) {
			positions.push_back(p);
		}
	}
	// SweepPlans.precision_plan_ids, every entry LEFT
	report(sweep(g, {{7}, {4, 5}, {7, 4, 1}, {5, 5}, {1, 2, 3, 6}, {7, 4, 1, 3, 6, 6, 6}}, positions), "inversion_grid with plans");
}
