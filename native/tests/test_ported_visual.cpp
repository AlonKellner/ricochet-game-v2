// Stage 119.3: the retired GUT tests of the visual kernels, ported one TEST_CASE
// per GUT function ("ported: <gut file stem>: <function>"): the carriers and
// segments (GeneralizedCircle, Segment), VisualConverter (arcs, display ends,
// prepare_for_display), ArrowAdvance, StepTreeMerge, the physical hits and a
// few tracer checks behind them. Same inputs and assertions as the GDScript;
// the bindings were thin wrappers of these functions (native/gd/gd_visual.cpp).
// Vector2 math is float32 (V2) as in Godot; GDScript scalars are doubles.
#include "rc/invariants.hpp"
#include "rc/sweep.hpp"
#include "rc/tracer.hpp"
#include "rc/visual.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <vector>

using namespace rc;
using namespace rc::arc;

namespace {

constexpr double PI_D = 3.141592653589793;
const Rect2f DEFAULT_BOUNDS = Rect2f::of(0, 0, 1920, 1080); // VisualConverter.DEFAULT_BOUNDS()

// assert_almost_eq on two Vector2s: per component
bool near(const V2 &a, const V2 &b, double tol) {
	return std::fabs(double(a.x) - double(b.x)) <= tol && std::fabs(double(a.y) - double(b.y)) <= tol;
}

// TraceStep.make(s, e) as ArrowAdvance reads it: a line step, the via its midpoint
StepGeom line_step(V2 s, V2 e) {
	StepGeom g;
	g.start = s;
	g.end = e;
	g.via = (s + e) / 2.0f;
	return g;
}

// TraceStep.make(s, e, ..., via, true): an arc step
StepGeom arc_step(V2 s, V2 e, V2 v) {
	StepGeom g;
	g.start = s;
	g.end = e;
	g.via = v;
	g.is_arc_step = true;
	return g;
}

// TraceStep.make(s, e, frame_id)
TraceStep tstep(V2 s, V2 e, int frame_id = 0) {
	return TraceStep::make(s, e, frame_id, std::nullopt, std::nullopt, V2{}, false);
}

SideDef side(EffectType type, bool interactive) {
	SideDef d;
	d.type = type;
	d.interactive = interactive;
	return d;
}

// SceneDefs.describe_surface of a Segment.from_coords surface
SurfaceDef seg_def(int id, V2 s, V2 e, V2 v, std::optional<SideDef> left, std::optional<SideDef> right) {
	SurfaceDef d;
	d.id = id;
	d.line = derive_carrier(s, e, v).is_line();
	d.sx = s.x;
	d.sy = s.y;
	d.ex = e.x;
	d.ey = e.y;
	d.vx = v.x;
	d.vy = v.y;
	d.left = left;
	d.right = right;
	return d;
}

// RoomBuilder.create_block_surface: terminal both sides
SurfaceDef block_def(int id, V2 s, V2 e, V2 v) {
	return seg_def(id, s, e, v, side(EffectType::Terminal, false), side(EffectType::Terminal, false));
}

// test_helpers.mirror(x): a two-sided interactive mirror x, y 0..600
SurfaceDef mirror_def(int id, double x) {
	const SideDef refl = side(EffectType::Reflection, true);
	return seg_def(id, V2::of(x, 0), V2::of(x, 600), V2::of(x, 300), refl, refl);
}

// Segment.full_from_carrier(GeneralizedCircle.from_circle(center, r)), reflective both sides
SurfaceDef full_circle_mirror_def(int id, V2 center, double r) {
	const GeneralizedCircle g = from_circle(center, r);
	const SegGeom seg = SegGeom::full_from_carrier(g);
	SurfaceDef d = seg_def(id, seg.s, seg.e, seg.v, side(EffectType::Reflection, true), side(EffectType::Reflection, true));
	d.line = false;
	d.full = true;
	d.has_circle = true;
	d.cx = -g.b / (2.0 * g.a);
	d.cy = -g.c / (2.0 * g.a);
	d.radius = g.radius();
	return d;
}

// TestTrace.trace_pair: a fresh tracer with the game's switches (infinity event on)
std::optional<TracePair> trace_pair(const std::vector<SurfaceDef> &defs, V2 player, V2 cursor) {
	Tracer t;
	t.set_defs(defs);
	return t.trace_pair(player, cursor, {}, Flags());
}

// TestTrace.trace (PHYSICAL): the physical path, or an empty one
TracePath trace_physical(const std::vector<SurfaceDef> &defs, V2 player, V2 cursor) {
	const std::optional<TracePair> pair = trace_pair(defs, player, cursor);
	return pair ? pair->physical : TracePath{};
}

// test_nogaps_fix's surfaces: the full reflective circle (960,540) r 200 and the four screen edges (pass)
std::vector<SurfaceDef> circle_no_walls_defs() {
	std::vector<SurfaceDef> defs{full_circle_mirror_def(1, V2{960.0f, 540.0f}, 200.0)};
	const V2 corners[4][2] = {{{0, 0}, {1920, 0}}, {{1920, 0}, {1920, 1080}}, {{1920, 1080}, {0, 1080}}, {{0, 1080}, {0, 0}}};
	int id = 2;
	for (const auto &c : corners) {
		defs.push_back(seg_def(id++, c[0], c[1], (c[0] + c[1]) / 2.0f, side(EffectType::Pass, false), side(EffectType::Pass, false)));
	}
	return defs;
}

// test_nogaps_fix._count_nogaps_violations
int count_nogaps_violations(const TracePath &path) {
	int count = 0;
	for (size_t i = 1; i < path.steps.size(); i++) {
		const TraceStep &prev = path.steps[i - 1];
		const TraceStep &curr = path.steps[i];
		if (!prev.hit || !curr.hit) {
			continue;
		}
		const double gap = prev.end.distance_to(curr.start);
		const double tol = 0.05 + 0.001 * double(i);
		if (gap > tol) {
			if (inv::is_infinity_gap(prev.end, curr.start)) {
				continue;
			}
			count++;
		}
	}
	return count;
}

// test_nogaps_fix._count_continuity_violations
int count_continuity_violations(const TracePath &path) {
	int count = 0;
	for (size_t i = 1; i < path.steps.size(); i++) {
		const TraceStep &prev = path.steps[i - 1];
		const TraceStep &curr = path.steps[i];
		if (!prev.hit || curr.after_portal) {
			continue;
		}
		const double gap = prev.end.distance_to(curr.start);
		const double tol = 0.05 + 0.001 * double(i);
		if (gap > tol) {
			if (inv::is_infinity_gap(prev.end, curr.start)) {
				continue;
			}
			count++;
		}
	}
	return count;
}

// test_nogaps_fix._has_zero_length_hit
bool has_zero_length_hit(const TracePath &path) {
	for (const TraceStep &s : path.steps) {
		if (s.start == s.end && s.hit) {
			return true;
		}
	}
	return false;
}

const std::vector<std::pair<V2, V2>> CIRCLE_NO_WALLS_CASES = {
	{{0, 0}, {480, 270}},
	{{0, 0}, {960, 540}},
	{{0, 0}, {1440, 810}},
	{{0, 0}, {1920, 1080}},
	{{0, 0}, {160, 90}},
	{{960, 0}, {960, 270}},
	{{960, 0}, {960, 540}},
};

// test_stage48_full_segment._count_frame_transitions (the count)
int frame_transitions(const TracePath &path) {
	int n = 0;
	bool prev_conj = false;
	for (const TraceStep &s : path.steps) {
		const bool conj = s.frame_id != IDENTITY_FRAME;
		if (conj != prev_conj) {
			n++;
			prev_conj = conj;
		}
	}
	return n;
}

bool has_key(const std::vector<std::pair<int, std::vector<PhysicalHit>>> &hits, int id) {
	for (const auto &e : hits) {
		if (e.first == id) {
			return true;
		}
	}
	return false;
}

// test_stage80_trace_types._meta_step: a hit step the long way round through infinity
TraceStep meta_step() {
	TraceHit hit;
	hit.t = -1.0;
	hit.point = V2{1500.0f, 500.0f};
	hit.side = Side::Left;
	hit.on_segment = true;
	TraceStep step = TraceStep::make(V2{100.0f, 500.0f}, V2{1500.0f, 500.0f}, 0, hit, std::nullopt, V2::inf(), false);
	step.surface_id = 5;
	step.hit_side = int(Side::Left);
	step.hit_on_segment = true;
	step.after_portal = true;
	step.est_error = 0.25;
	step.precision_bits = 112;
	return step;
}

// the four-step fast-forward list of test_bug_fixes
std::vector<StepGeom> fast_forward_steps() {
	return {line_step({100, 100}, {500, 100}), line_step({500, 100}, {2500, 100}), line_step({2500, 100}, {3000, 100}),
		line_step({100, 500}, {400, 500})};
}

} // namespace

// ---------------------------------------------------------------- test_bug_fixes (ArrowAdvance, StepTreeMerge)

TEST_CASE("ported: test_bug_fixes: test_advance_boundary_floating_point") {
	const std::vector<StepGeom> steps{line_step(V2::of(-806.0, 840.0), V2::of(-0.000061, 642.46)),
		line_step(V2::of(-0.000061, 642.46), V2::of(560.0, 505.0))};
	const double step0_len = V2::of(-806.0, 840.0).distance_to(V2::of(-0.000061, 642.46));
	const AdvanceResult r = advance(steps, 0, 0.0, V2::of(-806.0, 840.0), step0_len + 100.0, DEFAULT_BOUNDS);
	CHECK(r.step_index == 1); // not skipped past it
	CHECK(!r.finished);
	CHECK(r.position.x > 0.0f);
	CHECK(r.position.x < 560.0f);
}

TEST_CASE("ported: test_bug_fixes: test_advance_offbounds_fast_forward") {
	const AdvanceResult r = advance(fast_forward_steps(), 0, 0.0, V2{100.0f, 100.0f}, 99999.0, DEFAULT_BOUNDS);
	CHECK(std::fabs(r.position.x - 400.0f) <= 1.0f);
	CHECK(std::fabs(r.position.y - 500.0f) <= 1.0f);
	CHECK(r.finished);
}

TEST_CASE("ported: test_bug_fixes: test_advance_partial_step_goes_offbounds") {
	const std::vector<StepGeom> steps{line_step({1800, 500}, {2100, 500})};
	const AdvanceResult r = advance(steps, 0, 0.0, V2{1800.0f, 500.0f}, 200.0, DEFAULT_BOUNDS);
	CHECK(r.finished);
}

TEST_CASE("ported: test_bug_fixes: test_advance_preserves_distance_after_fast_forward") {
	// 400 (step 0) + 2000 (step 1) consumed; 200 into step 3
	const AdvanceResult r = advance(fast_forward_steps(), 0, 0.0, V2{100.0f, 100.0f}, 2600.0, DEFAULT_BOUNDS);
	CHECK(std::fabs(r.position.x - 300.0f) <= 1.0f);
	CHECK(std::fabs(r.position.y - 500.0f) <= 1.0f);
	CHECK(!r.finished);
}

TEST_CASE("ported: test_bug_fixes: test_advance_wrap_through_infinity") {
	const std::vector<StepGeom> steps{line_step({500, 200}, {1910, 100}), line_step({1910, 100}, {2500, -50}),
		line_step({-500, 1100}, {-100, 900}), line_step({-100, 900}, {50, 700}), line_step({50, 700}, {400, 600}),
		line_step({400, 600}, {700, 500})};
	const AdvanceResult r = advance(steps, 0, 0.0, V2{500.0f, 200.0f}, 99999.0, DEFAULT_BOUNDS);
	CHECK(std::fabs(r.position.x - 700.0f) <= 1.0f);
	CHECK(std::fabs(r.position.y - 500.0f) <= 1.0f);
	CHECK(r.finished);
}

TEST_CASE("ported: test_bug_fixes: test_physical_preview_matches") {
	const SideDef refl = side(EffectType::Reflection, true);
	const SideDef pass = side(EffectType::Pass, false);
	const std::vector<SurfaceDef> defs{
		block_def(1, {560, 240}, {1360, 240}, {960, 240}),
		block_def(2, {1360, 840}, {560, 840}, {960, 840}),
		block_def(3, {560, 840}, {560, 240}, {560, 540}),
		seg_def(4, {800, 300}, {800, 780}, {800, 540}, refl, pass),
		seg_def(5, {1200, 300}, {1200, 780}, {1200, 540}, refl, pass),
	};
	const V2 player = V2::of(960.0, 827.9623);
	const V2 cursor = V2::of(689.7184, 640.1856);
	const std::optional<TracePair> pair = trace_pair(defs, player, cursor);
	REQUIRE(pair.has_value()); // (not vacuous)
	const TracePath physical = pair ? pair->physical : TracePath{};
	const TracePath planned = pair ? pair->planned : TracePath{};
	int ci = planned.cursor_index;
	if (ci < 0) {
		ci = int(planned.steps.size());
	}
	const std::vector<TraceStep> merged = sweep::merge(planned.steps, physical.steps, ci);
	size_t non_red = 0;
	for (const TraceStep &ms : merged) {
		if (ms.type == StepType::Aligned || ms.type == StepType::AlignedPostPlanned || ms.type == StepType::DivergedPhysical) {
			non_red++;
		}
	}
	CHECK(non_red == physical.steps.size());
}

// ---------------------------------------------------------------- test_direction_only (geometry extraction)

TEST_CASE("ported: test_direction_only: test_extract_geometry_empty_path") {
	const TracePath path;
	CHECK(inv::extract_trace_geometry(&path).empty());
}

TEST_CASE("ported: test_direction_only: test_extract_geometry_null_path") {
	CHECK(inv::extract_trace_geometry(nullptr).empty());
}

TEST_CASE("ported: test_direction_only: test_extract_geometry_single_step") {
	TracePath path;
	path.steps.push_back(tstep({0, 0}, {100, 0}, 0));
	const std::vector<inv::GeomSegment> geo = inv::extract_trace_geometry(&path);
	REQUIRE(geo.size() == 1);
	CHECK(geo[0].start == V2{0.0f, 0.0f});
	CHECK(geo[0].end == V2{100.0f, 0.0f});
}

// ---------------------------------------------------------------- test_epsilon_removal

TEST_CASE("ported: test_epsilon_removal: test_degenerate_arc_exact_equality") {
	const V2 p{100.0f, 200.0f};
	CHECK(!is_arc(p, p, p));
}

// ---------------------------------------------------------------- test_merge (StepTreeMerge.merge)

TEST_CASE("ported: test_merge: test_all_post_cursor") {
	const std::vector<TraceStep> steps{tstep({0, 0}, {100, 0}), tstep({100, 0}, {200, 0})};
	const std::vector<TraceStep> merged = sweep::merge(steps, steps, 0);
	for (const TraceStep &ms : merged) {
		CHECK(ms.type == StepType::AlignedPostPlanned);
	}
}

TEST_CASE("ported: test_merge: test_cursor_boundary") {
	const std::vector<TraceStep> steps{tstep({0, 0}, {100, 0}), tstep({100, 0}, {200, 0}), tstep({200, 0}, {300, 0})};
	const std::vector<TraceStep> merged = sweep::merge(steps, steps, 2);
	REQUIRE(merged.size() >= 3);
	CHECK(merged[0].type == StepType::Aligned);
	CHECK(merged[1].type == StepType::Aligned);
	CHECK(merged[2].type == StepType::AlignedPostPlanned);
}

TEST_CASE("ported: test_merge: test_diverged_post_cursor") {
	const std::vector<TraceStep> planned{tstep({0, 0}, {100, 0}, 0), tstep({100, 0}, {200, 50}, 5)};
	const std::vector<TraceStep> physical{tstep({0, 0}, {100, 0}, 0), tstep({100, 0}, {200, -50}, 7)};
	const std::vector<TraceStep> merged = sweep::merge(planned, physical, 1);
	REQUIRE(!merged.empty());
	CHECK(merged[0].type == StepType::Aligned);
	bool has_post_planned = false;
	for (const TraceStep &ms : merged) {
		has_post_planned = has_post_planned || ms.type == StepType::DivergedPostPlanned;
	}
	CHECK(has_post_planned);
}

TEST_CASE("ported: test_merge: test_frame_divergence") {
	const std::vector<TraceStep> planned{tstep({0, 0}, {100, 0}, 0), tstep({100, 0}, {200, 0}, 5)};
	const std::vector<TraceStep> physical{tstep({0, 0}, {100, 0}, 0), tstep({100, 0}, {200, 0}, 7)};
	const std::vector<TraceStep> merged = sweep::merge(planned, physical, 2);
	REQUIRE(!merged.empty());
	CHECK(merged[0].type == StepType::Aligned);
	bool has_div_planned = false;
	bool has_div_physical = false;
	for (size_t i = 1; i < merged.size(); i++) {
		has_div_planned = has_div_planned || merged[i].type == StepType::DivergedPlanned;
		has_div_physical = has_div_physical || merged[i].type == StepType::DivergedPhysical;
	}
	CHECK(has_div_planned);
	CHECK(has_div_physical);
}

TEST_CASE("ported: test_merge: test_monotonic_divergence") {
	const std::vector<TraceStep> planned{tstep({0, 0}, {100, 0}, 0), tstep({100, 0}, {200, 0}, 5), tstep({200, 0}, {300, 0}, 5)};
	const std::vector<TraceStep> physical{tstep({0, 0}, {100, 0}, 0), tstep({100, 0}, {200, 0}, 7), tstep({200, 0}, {300, 0}, 7)};
	const std::vector<TraceStep> merged = sweep::merge(planned, physical, 3);
	bool diverged = false;
	for (size_t i = 0; i < merged.size(); i++) {
		const StepType t = merged[i].type;
		if (t == StepType::DivergedPlanned || t == StepType::DivergedPhysical) {
			diverged = true;
		} else {
			CHECK_MESSAGE(!(diverged && t == StepType::Aligned), "ALIGNED after divergence at step " << i);
		}
	}
}

TEST_CASE("ported: test_merge: test_physical_shorter_than_planned") {
	// physical stops at a terminal wall before the cursor; planned continues
	const std::vector<TraceStep> planned{tstep({0, 0}, {100, 0}, 0), tstep({100, 0}, {200, 0}, 0), tstep({200, 0}, {300, 0}, 0)};
	const std::vector<TraceStep> physical{tstep({0, 0}, {100, 0}, 0)};
	const std::vector<TraceStep> merged = sweep::merge(planned, physical, 3);
	REQUIRE(!merged.empty());
	CHECK(merged[0].type == StepType::Aligned);
	int div_planned = 0;
	for (const TraceStep &ms : merged) {
		div_planned += ms.type == StepType::DivergedPlanned ? 1 : 0;
	}
	CHECK(div_planned == 2);
}

TEST_CASE("ported: test_merge: test_planned_shorter_than_physical") {
	const std::vector<TraceStep> planned{tstep({0, 0}, {100, 0}, 0)};
	const std::vector<TraceStep> physical{tstep({0, 0}, {100, 0}, 0), tstep({100, 0}, {200, 0}, 0)};
	const std::vector<TraceStep> merged = sweep::merge(planned, physical, 1);
	int div_physical = 0;
	for (const TraceStep &ms : merged) {
		div_physical += ms.type == StepType::DivergedPhysical ? 1 : 0;
	}
	CHECK(div_physical == 1);
}

// ---------------------------------------------------------------- test_nogaps_fix

TEST_CASE("ported: test_nogaps_fix: test_asymmetric_violation_pattern") {
	// prev.end with a huge y, curr.start on screen
	CHECK(inv::is_infinity_gap(V2::of(996.0467, -1171484800.0), V2::of(1026.471, 1154.711)));
}

TEST_CASE("ported: test_nogaps_fix: test_circle_no_walls_no_continuity_violations") {
	const std::vector<SurfaceDef> defs = circle_no_walls_defs();
	for (const auto &[player, cursor] : CIRCLE_NO_WALLS_CASES) {
		const TracePath raw = trace_physical(defs, player, cursor);
		CHECK(!raw.steps.empty()); // (not vacuous)
		const TracePath display = sweep::prepare_for_display(raw, DEFAULT_BOUNDS, {});
		CHECK_MESSAGE(count_continuity_violations(display) == 0, "(" << player.x << "," << player.y << ")->(" << cursor.x << "," << cursor.y << ")");
	}
}

TEST_CASE("ported: test_nogaps_fix: test_circle_no_walls_no_nogaps_violations") {
	const std::vector<SurfaceDef> defs = circle_no_walls_defs();
	for (const auto &[player, cursor] : CIRCLE_NO_WALLS_CASES) {
		const TracePath raw = trace_physical(defs, player, cursor);
		CHECK(!raw.steps.empty()); // (not vacuous)
		const TracePath display = sweep::prepare_for_display(raw, DEFAULT_BOUNDS, {});
		CHECK_MESSAGE(count_nogaps_violations(display) == 0, "(" << player.x << "," << player.y << ")->(" << cursor.x << "," << cursor.y << ")");
	}
}

TEST_CASE("ported: test_nogaps_fix: test_circle_no_walls_no_origin_not_rehit_violations") {
	const std::vector<SurfaceDef> defs = circle_no_walls_defs();
	const std::vector<std::pair<V2, V2>> cases{{{1920, 0}, {1440, 270}}, {{1920, 0}, {960, 540}}, {{1920, 0}, {0, 1080}}};
	for (const auto &[player, cursor] : cases) {
		const TracePath raw = trace_physical(defs, player, cursor);
		CHECK(!raw.steps.empty()); // (not vacuous)
		const TracePath display = sweep::prepare_for_display(raw, DEFAULT_BOUNDS, {});
		CHECK_MESSAGE(!has_zero_length_hit(display), "(" << player.x << "," << player.y << ")->(" << cursor.x << "," << cursor.y << ")");
	}
}

TEST_CASE("ported: test_nogaps_fix: test_infinity_gap_detected_as_infinity") {
	// both endpoints of the violation data huge
	CHECK(inv::is_infinity_gap(V2::of(-275643616.0, -68910664.0), V2::of(585744512.0, 122.6381)));
}

namespace {
// test_nogaps_fix: one hit step through infinity, the aim ray (1920,0) -> (1440,270)
TracePath one_inf_step(V2 start, V2 end) {
	const V2 player{1920.0f, 0.0f};
	const V2 cursor{1440.0f, 270.0f};
	TraceHit hit;
	hit.t = 0.43;
	hit.point = V2{50.0f, 0.0f};
	hit.side = Side::Left;
	hit.on_segment = true;
	TracePath path;
	path.steps.push_back(TraceStep::make(start, end, 0, hit, Ray{player, player, cursor}, V2::inf(), false));
	return path;
}
} // namespace

TEST_CASE("ported: test_nogaps_fix: test_no_zero_length_after_prepare_end_inf_start_on_bounds") {
	const TracePath display = sweep::prepare_for_display(one_inf_step(V2{0.0f, 0.0f}, V2::inf()), DEFAULT_BOUNDS, {});
	CHECK(!has_zero_length_hit(display));
}

TEST_CASE("ported: test_nogaps_fix: test_no_zero_length_after_prepare_start_inf_end_on_bounds") {
	const TracePath display = sweep::prepare_for_display(one_inf_step(V2::inf(), V2::of(0.002144, 1080.0)), DEFAULT_BOUNDS, {});
	CHECK(!has_zero_length_hit(display));
}

// ---------------------------------------------------------------- test_stage31_arrow_flight_plan

TEST_CASE("ported: test_stage31_arrow_flight_plan: test_stage31_camera_bounds_arc_flight") {
	const StepGeom step = arc_step({2, 0}, {-2, 0}, {0, 2});
	const double arc_len = 2.0 * PI_D;
	const AdvanceResult r = advance({step}, 0, 0.0, step.start, arc_len * 0.5, DEFAULT_BOUNDS);
	CHECK(near(r.position, V2{0.0f, 2.0f}, 0.01)); // follows the curve, not the chord
}

// ---------------------------------------------------------------- test_stage41_arc_rendering (VisualConverter arcs)

TEST_CASE("ported: test_stage41_arc_rendering: test_stage41_S16_arc_no_nan") {
	const V2 cases[3][3] = {{{200, 100}, {300, 200}, {200, 300}}, {{0, 100}, {100, 0}, {0, -100}}, {{500, 300}, {600, 400}, {500, 500}}};
	for (const auto &tc : cases) {
		const V2 s = tc[0], v = tc[1], e = tc[2];
		if (is_arc(s, v, e)) {
			const ArcParams p = arc_params(s, v, e);
			CHECK(!(std::isnan(p.center.x) || std::isnan(p.center.y)));
			CHECK(!std::isnan(p.radius));
			CHECK(!(std::isnan(p.start_angle) || std::isnan(p.end_angle)));
		}
	}
}

TEST_CASE("ported: test_stage41_arc_rendering: test_stage41_arc_angles_from_three_points") {
	const V2 s{200, 100}, e{200, 300}, v{300, 200};
	const ArcParams p = arc_params(s, v, e);
	const double sa = (s - V2{200, 200}).angle();
	const double ea = (e - V2{200, 200}).angle();
	CHECK(std::fabs(p.start_angle - sa) <= 0.01);
	CHECK(std::fabs(p.end_angle - ea) <= 0.01);
}

TEST_CASE("ported: test_stage41_arc_rendering: test_stage41_clockwise_flag_cw_winding") {
	const V2 s{200, 300}, e{200, 100}, v{300, 200};
	const ArcParams p = arc_params(s, v, e);
	const double cross = (s - p.center).cross(v - p.center);
	if (cross < 0.0) {
		CHECK(p.clockwise);
	} else {
		CHECK(!p.clockwise);
	}
}

TEST_CASE("ported: test_stage41_arc_rendering: test_stage41_draw_arc_clockwise_swap") {
	// via on the left of start -> end: CW
	const V2 s{200, 100}, e{200, 300}, v{100, 200};
	const ArcParams p = arc_params(s, v, e);
	CHECK(p.clockwise);
	const double sa = (s - p.center).angle();
	CHECK(std::fabs(p.start_angle - sa) <= 0.01);
	CHECK(std::fabs(p.end_angle - (sa - p.span)) <= 0.01);
}

TEST_CASE("ported: test_stage41_arc_rendering: test_stage41_escape_step_guard") {
	// is_arc(start, via, end): the end at infinity is no arc
	CHECK(!is_arc(V2{200, 200}, V2{300, 300}, V2::inf()));
}

TEST_CASE("ported: test_stage41_arc_rendering: test_stage41_visual_converter_circle_carrier") {
	const V2 s{200, 100}, e{200, 300}, v{300, 200};
	CHECK(is_arc(s, v, e));
	const ArcParams p = arc_params(s, v, e);
	CHECK(near(p.center, V2{200, 200}, 0.1));
	CHECK(std::fabs(p.radius - 100.0) <= 0.1);
}

// ---------------------------------------------------------------- test_stage42_arrow_arc_flight (ArrowAdvance on arcs)
// The CCW semicircle start (2,0) end (-2,0) via (0,2): centre (0,0), radius 2, arc length 2 pi.

TEST_CASE("ported: test_stage42_arrow_arc_flight: test_arc_constant_visual_speed") {
	const StepGeom step = arc_step({2, 0}, {-2, 0}, {0, 2});
	const double chord_dist = step.start.distance_to(step.end);
	CHECK(std::fabs(chord_dist - 4.0) <= 0.01);
	const double arc_len = 2.0 * PI_D;
	CHECK(arc_len > chord_dist);
	const AdvanceResult r = advance({step}, 0, 0.0, step.start, arc_len * 0.5, DEFAULT_BOUNDS);
	CHECK(near(r.position, V2{0, 2}, 0.01)); // the arc's midpoint, not the chord's
}

TEST_CASE("ported: test_stage42_arrow_arc_flight: test_arc_out_of_screen_resumes_at_reentry_edge") {
	// centre (950,-197.9), r 302.1: the long way out through the top edge at x~721.7, back in at x~1178.3
	const StepGeom step = arc_step({900, 100}, {1000, 100}, {950, -500});
	const std::vector<StepGeom> steps{step};
	int idx = 0;
	double prog = 0.0;
	V2 pos = step.start;
	bool seen_reentry = false;
	int frames = 0;
	while (idx < 1 && frames < 2000) {
		const AdvanceResult r = advance(steps, idx, prog, pos, 20.0, DEFAULT_BOUNDS);
		idx = r.step_index;
		prog = r.progress;
		pos = r.position;
		frames++;
		if (std::fabs(double(pos.y)) <= 2.0 && std::fabs(double(pos.x) - 1178.3) <= 3.0) {
			seen_reentry = true;
		}
		if (idx < 1) {
			CHECK_MESSAGE(DEFAULT_BOUNDS.grow(2.0f).has_point(pos), "arrow off screen at (" << pos.x << "," << pos.y << ")");
		}
	}
	CHECK(seen_reentry);
	CHECK(pos == step.end);
}

TEST_CASE("ported: test_stage42_arrow_arc_flight: test_arc_position_at_midpoint") {
	const StepGeom step = arc_step({2, 0}, {-2, 0}, {0, 2});
	const AdvanceResult r = advance({step}, 0, 0.0, step.start, 2.0 * PI_D * 0.5, DEFAULT_BOUNDS);
	CHECK(near(r.position, V2{0, 2}, 0.01));
}

TEST_CASE("ported: test_stage42_arrow_arc_flight: test_arc_position_at_quarter") {
	const StepGeom step = arc_step({2, 0}, {-2, 0}, {0, 2});
	const AdvanceResult r = advance({step}, 0, 0.0, step.start, 2.0 * PI_D * 0.25, DEFAULT_BOUNDS);
	CHECK(near(r.position, V2::of(std::sqrt(2.0), std::sqrt(2.0)), 0.01));
}

TEST_CASE("ported: test_stage42_arrow_arc_flight: test_arc_reaches_end") {
	const StepGeom step = arc_step({2, 0}, {-2, 0}, {0, 2});
	const AdvanceResult r = advance({step}, 0, 0.0, step.start, 2.0 * PI_D + 1.0, DEFAULT_BOUNDS);
	CHECK(near(r.position, V2{-2, 0}, 0.01));
	CHECK(r.finished);
}

TEST_CASE("ported: test_stage42_arrow_arc_flight: test_arc_tangent_direction_at_midpoint") {
	const StepGeom step = arc_step({2, 0}, {-2, 0}, {0, 2});
	const AdvanceResult r = advance({step}, 0, 0.0, step.start, 2.0 * PI_D * 0.5, DEFAULT_BOUNDS);
	CHECK(near(r.direction, V2{-1, 0}, 0.01));
}

TEST_CASE("ported: test_stage42_arrow_arc_flight: test_arc_tangent_direction_at_start") {
	const StepGeom step = arc_step({2, 0}, {-2, 0}, {0, 2});
	const AdvanceResult r = advance({step}, 0, 0.0, step.start, 0.001, DEFAULT_BOUNDS);
	CHECK(near(r.direction, V2{0, 1}, 0.02));
}

TEST_CASE("ported: test_stage42_arrow_arc_flight: test_clockwise_arc") {
	const StepGeom step = arc_step({0, 2}, {0, -2}, {2, 0});
	const AdvanceResult r = advance({step}, 0, 0.0, step.start, 2.0 * PI_D * 0.5, DEFAULT_BOUNDS);
	CHECK(near(r.position, V2{2, 0}, 0.01));
	CHECK(near(r.direction, V2{0, -1}, 0.01));
}

TEST_CASE("ported: test_stage42_arrow_arc_flight: test_multi_step_line_then_arc") {
	const std::vector<StepGeom> steps{line_step({0, 0}, {2, 0}), arc_step({2, 0}, {-2, 0}, {0, 2})};
	const AdvanceResult r = advance(steps, 0, 0.0, steps[0].start, 2.0 + 2.0 * PI_D * 0.5, DEFAULT_BOUNDS);
	CHECK(r.step_index == 1);
	CHECK(near(r.position, V2{0, 2}, 0.01));
}

// ---------------------------------------------------------------- test_stage48_full_segment (the full circle mirror)
// The reflected frame of a lone circle mirror is exactly "frame_id is not identity"; a reflection
// shows as at least two transitions.

TEST_CASE("ported: test_stage48_full_segment: test_full_circle_no_slip_through_sweep") {
	const V2 center{960, 540};
	const std::vector<SurfaceDef> defs{full_circle_mirror_def(1, center, 200.0)};
	int slip_count = 0;
	for (int angle_deg = 0; angle_deg < 360; angle_deg += 5) {
		const double angle = double(angle_deg) * PI_D / 180.0;
		const V2 target = center + V2::of(std::cos(angle), std::sin(angle)) * 200.0f;
		const V2 player = target + (target - center).normalized() * 100.0f;
		if (frame_transitions(trace_physical(defs, player, target)) < 2) {
			slip_count++;
			MESSAGE("slips through at " << angle_deg << " deg");
		}
	}
	CHECK(slip_count == 0);
}

TEST_CASE("ported: test_stage48_full_segment: test_full_circle_reflects_at_old_gap") {
	const V2 center{960, 540};
	const std::vector<SurfaceDef> defs{full_circle_mirror_def(1, center, 200.0)};
	const V2 gap_point{center.x, center.y - 200.0f};
	const V2 player = gap_point + V2{0, -100};
	CHECK(frame_transitions(trace_physical(defs, player, gap_point)) >= 2);
}

TEST_CASE("ported: test_stage48_full_segment: test_full_circle_reflects_horizontal") {
	const V2 center{960, 540};
	const std::vector<SurfaceDef> defs{full_circle_mirror_def(1, center, 200.0)};
	const V2 player{center.x - 200.0f - 100.0f, center.y};
	CHECK(frame_transitions(trace_physical(defs, player, center)) >= 2);
}

// ---------------------------------------------------------------- test_stage53_plan_highlight (step enrichment, physical hits)

TEST_CASE("ported: test_stage53_plan_highlight: test_escape_step_has_no_surface_id") {
	const TracePath path = trace_physical({}, V2{200, 300}, V2{600, 300});
	CHECK(!path.steps.empty()); // (not vacuous)
	for (const TraceStep &step : path.steps) {
		// the wrap's outgoing half carries INFINITY_ID (Stage 88); other escapes none
		CHECK((step.surface_id == -1 || step.surface_id == INFINITY_ID));
		CHECK(!step.hit);
	}
}

TEST_CASE("ported: test_stage53_plan_highlight: test_physical_hits_contains_hit_surface") {
	TraceStep step = TraceStep::make(V2{}, V2{}, 0, std::nullopt, std::nullopt, V2{}, false);
	step.surface_id = 5;
	step.hit_side = int(Side::Left);
	step.hit_on_segment = true;
	CHECK(has_key(physical_hits({step}), 5));
}

TEST_CASE("ported: test_stage53_plan_highlight: test_physical_hits_empty_for_no_hits") {
	const TraceStep step = TraceStep::make(V2{}, V2{}, 0, std::nullopt, std::nullopt, V2{}, false);
	CHECK(physical_hits({step}).empty());
}

TEST_CASE("ported: test_stage53_plan_highlight: test_physical_hits_records_side_and_on_segment") {
	TraceStep step = TraceStep::make(V2{}, V2{}, 0, std::nullopt, std::nullopt, V2{}, false);
	step.surface_id = 5;
	step.hit_side = int(Side::Left);
	step.hit_on_segment = true;
	const auto hits = physical_hits({step});
	REQUIRE(hits.size() == 1);
	REQUIRE(hits[0].first == 5);
	const std::vector<PhysicalHit> &entries = hits[0].second;
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].side == int(Side::Left));
	CHECK(entries[0].on_segment);
}

TEST_CASE("ported: test_stage53_plan_highlight: test_physical_hits_skips_negative_surface_id") {
	const TraceStep s1 = TraceStep::make(V2{}, V2{}, 0, std::nullopt, std::nullopt, V2{}, false);
	TraceStep s2 = s1;
	s2.surface_id = 3;
	s2.hit_side = int(Side::Right);
	s2.hit_on_segment = true;
	const auto hits = physical_hits({s1, s2});
	CHECK(!has_key(hits, -1));
	CHECK(has_key(hits, 3));
}

namespace {
// the first step of the trace (200,300) -> (600,300) at the mirror x=400 (id 1) whose surface_id is the mirror's
std::optional<TraceStep> mirror_step(bool need_hit) {
	const TracePath path = trace_physical({mirror_def(1, 400.0)}, V2{200, 300}, V2{600, 300});
	for (const TraceStep &s : path.steps) {
		if (need_hit ? (s.hit && s.surface_id >= 0) : s.surface_id == 1) {
			return s;
		}
	}
	return std::nullopt;
}
} // namespace

TEST_CASE("ported: test_stage53_plan_highlight: test_physical_trace_step_has_hit_side") {
	const std::optional<TraceStep> s = mirror_step(false);
	REQUIRE(s.has_value());
	CHECK((s->hit_side == int(Side::Left) || s->hit_side == int(Side::Right)));
}

TEST_CASE("ported: test_stage53_plan_highlight: test_physical_trace_step_has_on_segment") {
	const std::optional<TraceStep> s = mirror_step(false);
	REQUIRE(s.has_value());
	CHECK(s->hit_on_segment);
}

TEST_CASE("ported: test_stage53_plan_highlight: test_physical_trace_step_has_surface_id") {
	const std::optional<TraceStep> s = mirror_step(true);
	REQUIRE(s.has_value());
	CHECK(s->surface_id == 1);
}

TEST_CASE("ported: test_stage53_plan_highlight: test_with_type_preserves_surface_fields") {
	TraceStep step = TraceStep::make(V2{}, V2{}, 0, std::nullopt, std::nullopt, V2{}, false);
	step.surface_id = 42;
	step.hit_side = int(Side::Right);
	step.hit_on_segment = true;
	const TraceStep copy = step.with_type(StepType::DivergedPhysical);
	CHECK(copy.surface_id == 42);
	CHECK(copy.hit_side == int(Side::Right));
	CHECK(copy.hit_on_segment);
}

// ---------------------------------------------------------------- test_stage6_generalized_circle

TEST_CASE("ported: test_stage6_generalized_circle: test_stage6_center_from_coefficients") {
	const GeneralizedCircle gc{1, -200, -400, 40000};
	CHECK(std::fabs(center_of(gc).x - 100.0) <= 0.001);
	CHECK(std::fabs(center_of(gc).y - 200.0) <= 0.001);
	CHECK(std::fabs(gc.radius() - 100.0) <= 0.001);
}

TEST_CASE("ported: test_stage6_generalized_circle: test_stage6_circle_construction") {
	const GeneralizedCircle gc{1, -4, -6, 12};
	CHECK(!gc.is_line());
	CHECK(std::fabs(center_of(gc).x - 2.0) <= 0.001);
	CHECK(std::fabs(center_of(gc).y - 3.0) <= 0.001);
	CHECK(std::fabs(gc.radius() - 1.0) <= 0.001);
}

TEST_CASE("ported: test_stage6_generalized_circle: test_stage6_evaluate_on_circle") {
	const GeneralizedCircle gc = from_circle(V2{200, 200}, 100);
	CHECK(std::fabs(gc.evaluate(300, 200)) <= 0.1);
	CHECK(gc.evaluate(200, 200) < 0.0); // inside, a > 0
	CHECK(gc.evaluate(400, 200) > 0.0);
}

TEST_CASE("ported: test_stage6_generalized_circle: test_stage6_evaluate_on_line") {
	const GeneralizedCircle gc{0, 1, 0, -5};
	CHECK(std::fabs(gc.evaluate(5, 3)) <= 0.001);
	CHECK(std::fabs(gc.evaluate(3, 3) - -2.0) <= 0.001);
}

// ---------------------------------------------------------------- test_stage76_manifold_projection

TEST_CASE("ported: test_stage76_manifold_projection: test_point_already_on_circle_carrier") {
	const V2 point{100, 0};
	CHECK(near(project_onto_carrier(point, from_circle(V2{0, 0}, 100.0)), point, 0.001));
}

TEST_CASE("ported: test_stage76_manifold_projection: test_point_already_on_line_carrier") {
	const V2 point{500, 300};
	CHECK(near(project_onto_carrier(point, from_line(1.0, 0.0, -500.0)), point, 0.001));
}

TEST_CASE("ported: test_stage76_manifold_projection: test_project_onto_circle_inside") {
	const V2 projected = project_onto_carrier(V2{330, 250}, from_circle(V2{350, 250}, 50.0));
	CHECK(std::fabs(projected.x - 300.0) <= 0.01);
	CHECK(std::fabs(projected.y - 250.0) <= 0.01);
}

// ---------------------------------------------------------------- test_stage77_visual_snap (VisualConverter.compute_display_end)

namespace {
// a hit step on the surface id 1 whose segment is `seg` (the surfaces list [surface])
StepGeom hit_step(V2 s, V2 e) {
	StepGeom g = line_step(s, e);
	g.has_hit = true;
	g.surface_id = 1;
	return g;
}
std::function<std::optional<GeneralizedCircle>(int)> carrier_of_one(const GeneralizedCircle &carrier) {
	return [carrier](int id) -> std::optional<GeneralizedCircle> {
		if (id == 1) {
			return carrier;
		}
		return std::nullopt;
	};
}
// no surfaces (the default [])
std::optional<GeneralizedCircle> no_carrier(int) { return std::nullopt; }
} // namespace

TEST_CASE("ported: test_stage77_visual_snap: test_display_end_identity_frame_near_noop") {
	const GeneralizedCircle x100 = derive_carrier(V2{100, 0}, V2{100, 600}, V2{100, 300});
	const V2 de = display_end(hit_step(V2{50, 300}, V2::of(100.001, 300)), carrier_of_one(x100));
	CHECK(std::fabs(de.x - 100.0) <= 0.01);
	CHECK(std::fabs(de.y - 300.0) <= 0.01);
}

TEST_CASE("ported: test_stage77_visual_snap: test_display_end_no_hit_returns_end") {
	const StepGeom step = line_step(V2{50, 300}, V2{200, 300});
	CHECK(display_end(step, no_carrier) == step.end);
}

TEST_CASE("ported: test_stage77_visual_snap: test_display_end_null_segment_returns_end") {
	// a hit, no surface id (-1), no surfaces
	StepGeom step = line_step(V2{50, 300}, V2{200, 300});
	step.has_hit = true;
	CHECK(display_end(step, no_carrier) == step.end);
}

TEST_CASE("ported: test_stage77_visual_snap: test_display_end_on_circle_carrier") {
	const GeneralizedCircle carrier = derive_carrier(V2{300, 200}, V2{300, 400}, V2{400, 300});
	const StepGeom step = hit_step(V2{200, 300}, V2::of(401.0, 300.5));
	const V2 de = display_end(step, carrier_of_one(carrier));
	const double dist_before = std::fabs(carrier.evaluate(step.end.x, step.end.y));
	const double dist_after = std::fabs(carrier.evaluate(de.x, de.y));
	CHECK(dist_after < dist_before);
}

TEST_CASE("ported: test_stage77_visual_snap: test_display_end_preserves_point_already_on_carrier") {
	const GeneralizedCircle x100 = derive_carrier(V2{100, 0}, V2{100, 600}, V2{100, 300});
	const StepGeom step = hit_step(V2{50, 300}, V2{100, 300});
	CHECK(near(display_end(step, carrier_of_one(x100)), step.end, 0.001));
}

// ---------------------------------------------------------------- test_stage7_segment (Segment carriers and sides)

TEST_CASE("ported: test_stage7_segment: test_stage7_S11_three_points_on_carrier") {
	const SegGeom line = SegGeom::make(V2{0, 0}, V2{10, 0}, V2{5, 0});
	const GeneralizedCircle &lc = line.get_carrier();
	CHECK(std::fabs(lc.evaluate(line.s.x, line.s.y)) <= 1e-6);
	CHECK(std::fabs(lc.evaluate(line.e.x, line.e.y)) <= 1e-6);
	CHECK(std::fabs(lc.evaluate(line.v.x, line.v.y)) <= 1e-6);
	const SegGeom arcseg = SegGeom::make(V2{200, 100}, V2{200, 300}, V2{300, 200});
	const GeneralizedCircle &ac = arcseg.get_carrier();
	CHECK(std::fabs(ac.evaluate(arcseg.s.x, arcseg.s.y)) <= 0.01);
	CHECK(std::fabs(ac.evaluate(arcseg.e.x, arcseg.e.y)) <= 0.01);
	CHECK(std::fabs(ac.evaluate(arcseg.v.x, arcseg.v.y)) <= 0.01);
}

TEST_CASE("ported: test_stage7_segment: test_stage7_S12_side_consistent") {
	const SegGeom seg = SegGeom::make(V2{100, 400}, V2{100, 200}, V2{100, 300});
	const Side a = seg.determine_side(V2{50, 100});
	const Side b = seg.determine_side(V2{50, 300});
	const Side c = seg.determine_side(V2{50, 500});
	CHECK(a == b);
	CHECK(b == c);
	const Side d = seg.determine_side(V2{150, 100});
	const Side e = seg.determine_side(V2{150, 300});
	CHECK(d == e);
	CHECK(a != d);
}

TEST_CASE("ported: test_stage7_segment: test_stage7_side_determination_circle") {
	const SegGeom seg = SegGeom::make(V2{200, 100}, V2{200, 300}, V2{300, 200});
	CHECK(seg.determine_side(V2{400, 200}) != seg.determine_side(V2{200, 200}));
}

TEST_CASE("ported: test_stage7_segment: test_stage7_side_determination_line") {
	const SegGeom seg = SegGeom::make(V2{100, 400}, V2{100, 200}, V2{100, 300});
	CHECK(seg.determine_side(V2{50, 300}) == Side::Left);
	CHECK(seg.determine_side(V2{150, 300}) == Side::Right);
}

TEST_CASE("ported: test_stage7_segment: test_stage7_via_inf_produces_line") {
	CHECK(SegGeom::make(V2{0, 0}, V2{10, 0}, V2::inf()).is_line());
}

TEST_CASE("ported: test_stage7_segment: test_stage7_winding_ccw") {
	const SegGeom seg = SegGeom::make(V2{0, 0}, V2{0, 10}, V2{5, 5});
	const double winding = (seg.v - seg.s).cross(seg.e - seg.s);
	CHECK(winding > 0.0);
	const V2 test_point{10, 5};
	if (seg.get_carrier().evaluate(test_point.x, test_point.y) > 0.0) {
		CHECK(seg.determine_side(test_point) == Side::Left);
	}
}

TEST_CASE("ported: test_stage7_segment: test_stage7_winding_cw") {
	const SegGeom seg = SegGeom::make(V2{0, 10}, V2{0, 0}, V2{5, 5});
	const double winding = (seg.v - seg.s).cross(seg.e - seg.s);
	CHECK(winding < 0.0);
	const V2 test_point{10, 5};
	if (seg.get_carrier().evaluate(test_point.x, test_point.y) > 0.0) {
		CHECK(seg.determine_side(test_point) == Side::Right);
	}
}

// ---------------------------------------------------------------- test_stage80_trace_types (metadata through prepare_for_display)

TEST_CASE("ported: test_stage80_trace_types: test_converter_keeps_meta_on_split") {
	TracePath path;
	path.steps.push_back(meta_step());
	const TracePath out = sweep::prepare_for_display(path, DEFAULT_BOUNDS, {});
	std::vector<TraceStep> with_hit;
	for (const TraceStep &s : out.steps) {
		if (s.hit) {
			with_hit.push_back(s);
		}
	}
	REQUIRE(with_hit.size() == 1); // exactly one display piece carries the hit
	const TraceStep &s = with_hit[0];
	CHECK(s.surface_id == 5);
	CHECK(s.hit_side == int(Side::Left));
	CHECK(s.hit_on_segment);
	CHECK(s.est_error == 0.25);
	CHECK(s.precision_bits == 112);
	// after_portal belongs to the piece that starts the step
	CHECK(out.steps[0].after_portal);
}

TEST_CASE("ported: test_stage80_trace_types: test_physical_hits_survive_clipping") {
	TracePath path;
	path.steps.push_back(meta_step());
	const TracePath out = sweep::prepare_for_display(path, DEFAULT_BOUNDS, {});
	CHECK(has_key(physical_hits(out.steps), 5));
}
