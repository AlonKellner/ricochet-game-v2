// Stage 119.3: retired GUT tests (effects) that need the game layer, ported:
// the portal pair as the level builder makes it (rcg::build_spec,
// RigidMotionEffect.create_portal_pair) and the inversion grid level's merged
// planned / physical view (PathRenderer: trace_pair, prepare_for_display,
// StepTreeMerge.merge). Each TEST_CASE is "ported: <gut file stem>: <test function>".
#include "json_defs.hpp"
#include "rc/sweep.hpp"
#include "rc/tracer.hpp"
#include "rcg/build.hpp"
#include "rcg/level.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using namespace rc;

namespace {

constexpr double PI_D = 3.14159265358979323846;
constexpr double TOL = 0.01;

bool near2(const V2 &a, double x, double y) { return std::fabs(double(a.x) - x) <= TOL && std::fabs(double(a.y) - y) <= TOL; }

// T(z) = e^{i theta} z + d, the closed form in float64.
std::pair<double, double> T(double theta, double dx, double dy, double x, double y) {
	return {std::cos(theta) * x - std::sin(theta) * y + dx, std::sin(theta) * x + std::cos(theta) * y + dy};
}

// RigidMotionEffect.create_portal_pair(seg, theta, d) through the level builder:
// [source, target] (the target carries the inverse motion).
std::vector<rcg::BuiltSurface> portal_pair(const V2 &s, const V2 &e, const V2 &v, double theta, double dx, double dy) {
	rcg::SurfaceSpec spec;
	spec.start = s;
	spec.end = e;
	spec.via = v;
	rcg::SideSpec side;
	side.effect.type = "portal";
	side.effect.theta = theta;
	side.effect.d = std::array<double, 2>{dx, dy};
	side.interactive = true;
	spec.left = side;
	spec.right = side;
	std::vector<rcg::BuiltSurface> out = rcg::build_spec(spec, 1, std::nullopt);
	REQUIRE(out.size() == 2);
	return out;
}

// The map a portal side applies, from its own (theta, d).
V2 apply_side(const rcg::BuiltSurface &s, const V2 &z) { return rcg::apply_motion(z, s.left->effect.theta, s.left->effect.d); }

const V2 LINE_S = V2::of(100, 200), LINE_E = V2::of(200, 200), LINE_V = V2::of(150, 200);
const V2 ARC_S = V2::of(300, 200), ARC_E = V2::of(200, 300), ARC_V = V2::of(270.71, 270.71);

} // namespace

// ---------------------------------------------------------------- test_stage71_rigid_motion

TEST_CASE("ported: test_stage71_rigid_motion: test_stage71_portal_pair_rotated") {
	const auto pair = portal_pair(LINE_S, LINE_E, LINE_V, PI_D / 2.0, 100, 0);
	const arc::SegGeom &target = pair[1].seg;
	// T((100,200)) = (-200,100) + (100,0); T((200,200)) = (-200,200) + (100,0)
	CHECK(near2(target.s, -100, 100));
	CHECK(near2(target.e, -100, 200));
}

TEST_CASE("ported: test_stage71_rigid_motion: test_stage71_portal_pair_inverse_composition") {
	const double theta = PI_D / 3.0;
	const auto pair = portal_pair(LINE_S, LINE_E, LINE_V, theta, 50, -75);
	for (const V2 &p : {V2::of(0, 0), V2::of(100, 200), V2::of(-50, 300), V2::of(999, 1)}) {
		const auto [tx, ty] = T(theta, 50, -75, p.x, p.y);
		CHECK_MESSAGE(near2(apply_side(pair[0], p), tx, ty), "source effect is T(z) = e^{i theta} z + d");
		CHECK_MESSAGE(near2(apply_side(pair[1], apply_side(pair[0], p)), p.x, p.y), "T_BA(T_AB(p)) == p at (" << p.x << ", " << p.y << ")");
	}
}

TEST_CASE("ported: test_stage71_rigid_motion: test_stage71_portal_pair_arc_rotated") {
	const double theta = PI_D / 4.0;
	const auto pair = portal_pair(ARC_S, ARC_E, ARC_V, theta, 0, 0);
	const arc::SegGeom &source = pair[0].seg;
	const arc::SegGeom &target = pair[1].seg;
	const auto [sx, sy] = T(theta, 0, 0, source.s.x, source.s.y);
	const auto [ex, ey] = T(theta, 0, 0, source.e.x, source.e.y);
	const auto [vx, vy] = T(theta, 0, 0, source.v.x, source.v.y);
	CHECK_MESSAGE(near2(target.s, sx, sy), "start_B == T(start_A)");
	CHECK_MESSAGE(near2(target.e, ex, ey), "end_B == T(end_A)");
	CHECK_MESSAGE(near2(target.v, vx, vy), "via_B == T(via_A)");
	CHECK(std::fabs(target.get_carrier().radius() - source.get_carrier().radius()) <= TOL);
}

TEST_CASE("ported: test_stage71_rigid_motion: test_stage71_portal_pair_arc_inverse_composition") {
	const auto pair = portal_pair(ARC_S, ARC_E, ARC_V, PI_D / 6.0, 100, -50);
	for (const V2 &p : {V2::of(300, 200), V2::of(250, 250), V2::of(0, 0)}) {
		CHECK_MESSAGE(near2(apply_side(pair[1], apply_side(pair[0], p)), p.x, p.y), "arc portal round trip at (" << p.x << ", " << p.y << ")");
	}
}

// ---------------------------------------------------------------- test_precision_integration

TEST_CASE("ported: test_precision_integration: test_merged_view_aligned_until_first_divergence") {
	// the inversion grid level driven as the PathRenderer does, with the user's F12 dump (plan 7,4,1,3,6,6,6)
	const rcg::Level level = rcg::load_level(std::string(RC_REPO_ROOT) + "/data/levels/playground/inversion_grid.json");
	const std::vector<rcg::BuiltSurface> surfaces = rcg::build_level(level);
	std::vector<inv::SurfaceInfo> infos;
	for (const rcg::BuiltSurface &s : surfaces) {
		inv::SurfaceInfo i;
		i.id = s.id;
		i.s = s.seg.s;
		i.e = s.seg.e;
		i.v = s.seg.v;
		i.full = s.seg.full;
		i.carrier = s.seg.get_carrier();
		infos.push_back(i);
	}
	std::ifstream in(std::string(RC_REPO_ROOT) + "/bench/scenarios/dump_7_4_1_3_6_6_6_f32.json");
	REQUIRE(in.good());
	const ojson scn = ojson::parse(in);
	std::vector<PlanItem> plan;
	for (const ojson &e : scn["plan"]) {
		plan.push_back(PlanItem{e["surface_id"].get<int>(), e["side"].get<std::string>() == "LEFT", std::nullopt, std::nullopt});
	}
	const V2 player = V2::of(scn["player"][0].get<double>(), scn["player"][1].get<double>());
	const V2 cursor = V2::of(scn["cursor"][0].get<double>(), scn["cursor"][1].get<double>());
	Tracer tracer;
	tracer.set_emit_infinity(true);
	tracer.set_defs(rcg::defs_of(surfaces));
	const std::optional<TracePair> pair = tracer.trace_pair(player, cursor, plan, Flags());
	REQUIRE(pair);
	const Rect2f bounds = Rect2f::of(level.bounds.x, level.bounds.y, level.bounds.w, level.bounds.h);
	const TracePath physical = sweep::prepare_for_display(pair->physical, bounds, infos);
	const TracePath planned = sweep::prepare_for_display(pair->planned, bounds, infos);
	int ci = planned.cursor_index;
	if (ci < 0) {
		ci = int(planned.steps.size());
	}
	const std::vector<TraceStep> typed = sweep::merge(planned.steps, physical.steps, ci);
	// the first five plan hits (7,4,1,3,6) are physical too: aligned (solid)
	int aligned = 0;
	for (const TraceStep &s : typed) {
		if (s.type != StepType::Aligned) {
			break;
		}
		aligned++;
	}
	CHECK_MESSAGE(aligned >= 5, "first planned steps aligned with physical (" << aligned << ")");
}
