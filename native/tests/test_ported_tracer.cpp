// Stage 119.3: retired GUT tests (the tracer rows of the retirement ledger,
// docs/research/gut_ledger_tracer.tsv), ported to the native functions their
// GDScript classes bound: rc::Tracer, rc::Solver, rc::inv, rc::vis, rc::arc.
// One case per GUT test function, same inputs, same assertions.
#include "json_defs.hpp"
#include "rc/invariants.hpp"
#include "rc/sweep.hpp"
#include "rc/tracer.hpp"
#include "rc/visibility.hpp"
#include "rc/visibility_checks.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace rc;

namespace {

// ---------------------------------------------------------------- game surfaces (Surface + SceneDefs + surface_infos)

// A game surface as the GUT built it: its scene def and its checker info.
struct GameSurf {
	SurfaceDef def;
	inv::SurfaceInfo info;
};

SideDef side(EffectType t, bool interactive = false, std::vector<StateChangeDef> states = {}) {
	SideDef s;
	s.type = t;
	s.interactive = interactive;
	s.states = std::move(states);
	return s;
}

// Surface.new(Segment.from_coords(s, e, v), left, right) through SceneDefs.describe_surface
// and InvariantChecker.surface_infos (has_effect: a transformative side or player_solid).
GameSurf surf(int id, V2 s, V2 e, V2 v, std::optional<SideDef> l, std::optional<SideDef> r, bool player_solid, bool is_target = false) {
	const arc::SegGeom g = arc::SegGeom::make(s, e, v);
	GameSurf o;
	o.def.id = id;
	o.def.line = g.is_line();
	o.def.sx = s.x;
	o.def.sy = s.y;
	o.def.ex = e.x;
	o.def.ey = e.y;
	o.def.vx = v.x;
	o.def.vy = v.y;
	o.def.left = l;
	o.def.right = r;
	o.def.is_target = is_target;
	o.info.id = id;
	o.info.s = s;
	o.info.e = e;
	o.info.v = v;
	o.info.carrier = g.get_carrier();
	o.info.has_effect = player_solid || (l && is_transformative(l->type)) || (r && is_transformative(r->type));
	return o;
}

// RoomBuilder.create_block_surface / H.wall_between: terminal both sides, player-solid
GameSurf block(int id, V2 s, V2 e) { return surf(id, s, e, (s + e) / 2.0f, side(EffectType::Terminal), side(EffectType::Terminal), true); }

// H.mirror(x, y0, y1): reflection both sides, plannable, not player-solid
GameSurf mirror(int id, float x, float y0, float y1) {
	return surf(id, V2{x, y0}, V2{x, y1}, V2{x, (y0 + y1) / 2.0f}, side(EffectType::Reflection, true), side(EffectType::Reflection, true), false);
}

// RoomBuilder.create_room_surfaces(rect): top, right, bottom, left
std::vector<GameSurf> room(int first_id, float x, float y, float w, float h) {
	const float l = x, r = x + w, t = y, b = y + h;
	return {block(first_id, V2{l, t}, V2{r, t}), block(first_id + 1, V2{r, t}, V2{r, b}), block(first_id + 2, V2{r, b}, V2{l, b}),
		block(first_id + 3, V2{l, b}, V2{l, t})};
}

std::vector<SurfaceDef> defs_of(const std::vector<GameSurf> &ss) {
	std::vector<SurfaceDef> out;
	for (const GameSurf &s : ss) {
		out.push_back(s.def);
	}
	return out;
}

std::vector<inv::SurfaceInfo> infos_of(const std::vector<GameSurf> &ss) {
	std::vector<inv::SurfaceInfo> out;
	for (const GameSurf &s : ss) {
		out.push_back(s.info);
	}
	return out;
}

// TestTrace.fresh_tracer: the game's switches (FeatureFlags.infinity_event() is true)
Tracer fresh_tracer(const std::vector<SurfaceDef> &defs) {
	Tracer t;
	t.set_emit_infinity(true);
	t.set_defs(defs);
	return t;
}

// TestTrace.trace_pair
std::optional<TracePair> trace_pair(const std::vector<SurfaceDef> &defs, V2 player, V2 cursor, const std::vector<PlanItem> &plan = {},
		const Flags &flags = Flags()) {
	Tracer t = fresh_tracer(defs);
	return t.trace_pair(player, cursor, plan, flags);
}

PlanItem item(int id, bool left) {
	PlanItem p;
	p.surface_id = id;
	p.left = left;
	return p;
}

std::vector<std::string> kinds(const inv::Violations &vs) {
	std::vector<std::string> out;
	for (const inv::Violation &v : vs) {
		out.push_back(v.fmt.substr(0, v.fmt.find(':')));
	}
	return out;
}

bool has(const std::vector<std::string> &ks, const std::string &k) { return std::find(ks.begin(), ks.end(), k) != ks.end(); }

ojson load_json(const std::string &rel) {
	std::ifstream in(std::string(RC_REPO_ROOT) + "/" + rel);
	REQUIRE_MESSAGE(in.good(), rel);
	return ojson::parse(in);
}

// ---------------------------------------------------------------- tests/native/invariant_cases.gd

TraceStep ic_step(V2 a, V2 b, bool with_hit, int surface_id = 1) {
	TraceStep s = TraceStep::make(a, b, 0, with_hit ? std::optional<TraceHit>(TraceHit{10.0, b, surface_id, Side::Left, true}) : std::nullopt,
		std::nullopt, V2{}, false);
	if (with_hit) {
		s.surface_id = surface_id;
		s.hit_side = int(Side::Left);
		s.hit_on_segment = true;
	}
	return s;
}

TracePath ic_path(std::vector<TraceStep> steps, int cursor_index = -1) {
	TracePath p;
	p.steps = std::move(steps);
	p.cursor_index = cursor_index;
	return p;
}

std::vector<TraceStep> ic_typed(const std::vector<TraceStep> &steps, const std::vector<StepType> &types) {
	std::vector<TraceStep> out;
	for (size_t i = 0; i < steps.size(); i++) {
		out.push_back(steps[i].with_type(types[i]));
	}
	return out;
}

std::vector<TraceStep> cat(std::vector<TraceStep> a, const std::vector<TraceStep> &b) {
	a.insert(a.end(), b.begin(), b.end());
	return a;
}

struct ICase {
	std::string name;
	V2 player, cursor;
	TracePath physical; // planned is the same path in every case
	std::vector<TraceStep> typed;
	std::vector<GameSurf> surfaces;
	std::vector<std::string> expect;
};

// InvariantCases.cases(): the wall at x = 1800 has id 1, the mirror id 2
std::vector<ICase> invariant_cases() {
	const StepType A = StepType::Aligned, AP = StepType::AlignedPostPlanned, DY = StepType::DivergedPhysical, DR = StepType::DivergedPlanned,
				   DPP = StepType::DivergedPostPlanned;
	const V2 player{100, 100}, cursor{300, 100};
	const std::vector<GameSurf> wall = {block(1, V2{1800, 0}, V2{1800, 600})};
	const float nan = std::numeric_limits<float>::quiet_NaN();
	std::vector<ICase> out;
	const std::vector<TraceStep> clean_steps = {ic_step(player, V2{1800, 100}, true)};
	const TracePath clean = ic_path(clean_steps, 1);
	out.push_back({"clean", player, cursor, clean, ic_typed(clean_steps, {A}), wall, {}});
	const std::vector<TraceStep> gap_steps = {ic_step(player, V2{500, 100}, true), ic_step(V2{520, 100}, V2{1800, 100}, true)};
	out.push_back({"gap", player, cursor, ic_path(gap_steps, 2), ic_typed(gap_steps, {A, A}), wall, {"NOGAPS", "PHYSICAL-CONTINUITY"}});
	const std::vector<TraceStep> nan_steps = {ic_step(player, V2{nan, 100}, false)};
	out.push_back({"nan", player, cursor, ic_path(nan_steps), ic_typed(nan_steps, {A}), wall, {"S16", "TRACE-ENDS"}});
	out.push_back({"yellow_then_green", player, cursor, clean, ic_typed(gap_steps, {DY, A}), wall,
		{"GREEN-FROM-PLAYER", "SINGLE-DIVERGENCE", "PHYSICAL-PREVIEW-MATCH"}});
	const std::vector<TraceStep> zero_steps = {ic_step(player, player, true), ic_step(player, V2{1800, 100}, true)};
	out.push_back({"zero_length_rehit", player, cursor, ic_path(zero_steps, 2), ic_typed(zero_steps, {A, A}), wall, {"ORIGIN-NOT-REHIT", "UX7"}});
	const std::vector<TraceStep> post_steps = {ic_step(player, V2{300, 100}, false), ic_step(V2{300, 100}, V2{1800, 100}, true)};
	const TracePath post = ic_path(post_steps, 1);
	const std::vector<TraceStep> red = ic_typed({ic_step(V2{300, 100}, V2{300, 900}, false)}, {DPP});
	out.push_back({"post_cursor_divergence", player, cursor, post, cat(ic_typed(post_steps, {A, AP}), red), wall, {"NO-POST-CURSOR-DIVERGENCE"}});
	out.push_back({"divergence_at_cursor", player, cursor, post, cat(cat(ic_typed({post_steps[0]}, {A}), red), ic_typed({post_steps[1]}, {DY})), wall,
		{"NO-POST-CURSOR-DIVERGENCE"}});
	const std::vector<TraceStep> off_steps = {ic_step(V2{120, 100}, V2{1800, 100}, true)};
	out.push_back({"solid_not_from_player", player, cursor, ic_path(off_steps, 1), ic_typed(off_steps, {A}), wall, {"UX7", "SOLID-PATH-TO-CURSOR"}});
	const std::vector<TraceStep> mid_steps = {ic_step(player, V2{600, 400}, false)};
	out.push_back({"ends_mid_air", player, cursor, ic_path(mid_steps), ic_typed(mid_steps, {A}), wall, {"TRACE-ENDS"}});
	const std::vector<TraceStep> vis_steps = {ic_step(player, V2{600, 400}, true)};
	out.push_back({"visual_off_carrier", player, cursor, ic_path(vis_steps, 1), ic_typed(vis_steps, {A}), wall, {"VISUAL-ON-CARRIER"}});
	const std::vector<TraceStep> cross_steps = {ic_step(player, V2{1800, 100}, true)};
	out.push_back({"crosses_mirror", player, cursor, ic_path(cross_steps, 1), ic_typed(cross_steps, {A}), {wall[0], mirror(2, 400, 0, 600)},
		{"PHYSICS-COMPLIANCE"}});
	out.push_back({"no_line", player, cursor, ic_path({}), {}, wall, {"UX7"}});
	const std::vector<TraceStep> away_steps = {ic_step(player, V2{100, 900}, false)};
	out.push_back({"aim_away", player, cursor, ic_path(away_steps), ic_typed(away_steps, {A}), wall, {"UX7", "TRACE-ENDS"}});
	const std::vector<TraceStep> solid_gap_steps = {ic_step(player, V2{500, 100}, false), ic_step(V2{540, 100}, V2{1800, 100}, true)};
	out.push_back({"solid_gap", player, cursor, ic_path(solid_gap_steps, 2), ic_typed(solid_gap_steps, {A, DR}), wall,
		{"SOLID-PATH-TO-CURSOR", "PHYSICAL-PREVIEW-MATCH"}});
	return out;
}

// test_invariant_detects.gd _check: InvariantKernel.check_all(..., result null, surface_infos,
// defs, flags {}, plan [], has_renderer true, plan_applied false)
std::vector<std::string> ic_check(const ICase &c) {
	const std::vector<SurfaceDef> defs = defs_of(c.surfaces);
	const std::vector<inv::SurfaceInfo> infos = infos_of(c.surfaces);
	const Scene<F64> scene = Scene<F64>::from_defs(defs, F64{1.0});
	inv::Input in;
	in.player = c.player;
	in.cursor = c.cursor;
	in.has_renderer = true;
	in.physical = &c.physical;
	in.planned = &c.physical;
	in.typed = &c.typed;
	in.surfaces = &infos;
	in.defs = &defs;
	in.scene = &scene;
	in.plan_applied = false;
	return kinds(inv::check_all(in));
}

// ---------------------------------------------------------------- the beam-event scenes (Stage 83, 98)

const ojson REFL = {{"type", "reflection"}, {"interactive", true}};
const ojson INV = {{"type", "inversion"}, {"interactive", true}};
const ojson TERM = {{"type", "terminal"}, {"interactive", false}};
const ojson PASS = {{"type", "pass"}, {"interactive", false}};
// a pass that writes a flag (Stage 134: a pure pass-through is never computed)
const ojson WRITE = {{"type", "pass"}, {"interactive", false}, {"states", ojson::array({{{"key", "w"}, {"op", "set"}, {"value", true}}})}};

ojson vdef(int id, double x0, double y0, double x1, double y1, double vx, double vy, const ojson &l, const ojson &r, bool line = true) {
	return {{"id", id}, {"line", line}, {"start", {x0, y0}}, {"end", {x1, y1}}, {"via", {vx, vy}}, {"full", false}, {"left", l}, {"right", r}};
}

// test_stage83_invariants.gd _scene
std::vector<SurfaceDef> stage83_scene() {
	return defs_from_json(ojson::array({vdef(1, 300, -500, 300, 500, 300, 0, REFL, REFL), vdef(2, 700, 0, 500, 0, 600, -100, INV, INV, false),
		vdef(3, -900, -900, -900, 900, -900, 0, TERM, TERM), vdef(4, 900, -900, 900, 900, 900, 0, TERM, TERM),
		vdef(5, -900, -900, 900, -900, 0, -900, TERM, TERM), vdef(6, -900, 900, 900, 900, 0, 900, TERM, TERM)}));
}

// BeamSolver.trace_direction(f64, scene, player, dir, [], null, PHYSICAL) as the checker reads it
std::vector<inv::EvInfo> trace_direction(const Scene<F64> &sc, double px, double py, double dx, double dy) {
	Solver<F64> solver(sc, nullptr);
	const Walk<F64> w = solver.run_direction(CN<F64>{F64{px}, F64{py}}, CN<F64>{F64{dx}, F64{dy}}, {}, std::nullopt, Mode::Physical, MAX_HITS, Flags());
	std::vector<inv::EvInfo> out;
	for (const Event<F64> &e : w.events) {
		out.push_back(inv::ev_info(e));
	}
	return out;
}

// ---------------------------------------------------------------- test_stage49_mid_air_termination.gd

// _build_main_scene_surfaces: the room (160, 90, 1600, 900), then surfaces 5-9
std::vector<GameSurf> stage49_surfaces() {
	std::vector<GameSurf> s = room(1, 160, 90, 1600, 900);
	const SideDef refl = side(EffectType::Reflection, true);
	const SideDef pass = side(EffectType::Pass, false); // SideConfig.new(null, false)
	s.push_back(surf(5, V2{500, 200}, V2{500, 700}, V2{500, 450}, refl, pass, false));
	s.push_back(surf(6, V2{1400, 300}, V2{1400, 800}, V2{1400, 550}, refl, pass, false));
	s.push_back(surf(7, V2{700, 800}, V2{1200, 800}, V2{950, 800}, refl, pass, false));
	s.push_back(surf(8, V2{960, 200}, V2{960, 500}, V2{960, 350}, pass, refl, false));
	s.push_back(surf(9, V2{1100, 400}, V2{1100, 700}, V2{1230, 550}, side(EffectType::Inversion, true), pass, true));
	return s;
}

// _geometric_carrier_dist (the test's own: a line's distance, a circle's |dist - r|)
double carrier_dist(const V2 &p, const arc::GeneralizedCircle &g) {
	if (g.is_line()) {
		const double denom = std::sqrt(g.b * g.b + g.c * g.c);
		if (denom < 1e-10) {
			return INF_D;
		}
		return std::fabs(g.b * p.x + g.c * p.y + g.d) / denom;
	}
	return std::fabs(std::hypot(double(p.x) - g.center_x(), double(p.y) - g.center_y()) - g.radius());
}

// a hit whose surface has a game Segment (step.hit.segment != null)
bool hit_on_scene(const TraceStep &st, const std::vector<GameSurf> &ss) {
	if (!st.hit) {
		return false;
	}
	return std::any_of(ss.begin(), ss.end(), [&](const GameSurf &s) { return s.def.id == st.hit->surface_id; });
}

const Rect2f DEFAULT_BOUNDS = Rect2f::of(0, 0, 1920, 1080);

// _check_all_visual_hitpoints (tolerance 5 px)
std::vector<std::string> visual_hitpoint_violations(const TracePath &path, const std::vector<GameSurf> &ss) {
	std::vector<std::string> out;
	for (size_t i = 0; i < path.steps.size(); i++) {
		const TraceStep &st = path.steps[i];
		if (!hit_on_scene(st, ss) || st.end.is_inf() || inv::is_at_bounds(st.end, DEFAULT_BOUNDS)) {
			continue;
		}
		double min_dist = INF_D;
		for (const GameSurf &s : ss) {
			min_dist = std::min(min_dist, carrier_dist(st.end, s.info.carrier));
		}
		if (min_dist > 5.0) {
			out.push_back("step " + std::to_string(i) + ": visual endpoint " + std::to_string(min_dist) + " px from the nearest carrier");
		}
	}
	return out;
}

// _check_terminal_on_terminal_carrier (tolerance 5 px)
std::vector<std::string> terminal_violations(const TracePath &path, const std::vector<GameSurf> &ss) {
	if (path.steps.empty()) {
		return {};
	}
	const TraceStep &last = path.steps.back();
	if (!hit_on_scene(last, ss) || last.end.is_inf() || inv::is_at_bounds(last.end, DEFAULT_BOUNDS)) {
		return {};
	}
	for (const GameSurf &s : ss) {
		const bool terminal = (s.def.left && s.def.left->type == EffectType::Terminal) || (s.def.right && s.def.right->type == EffectType::Terminal);
		if (terminal && carrier_dist(last.end, s.info.carrier) < 5.0) {
			return {};
		}
	}
	return {"terminal endpoint not on any terminal carrier"};
}

const V2 S49_PLAYER{791.3528f, 725.1172f};
const V2 S49_AIM{847.8832f, 768.4231f};

// ---------------------------------------------------------------- test_stage110_visibility.gd

const vis::Rect VIS_BOUNDS{0, 0, 1000, 600};

// _preview_visible: the raw pair's merged steps before the cursor ALIGNED, the cursor reached
bool preview_visible(const std::vector<SurfaceDef> &defs, V2 player, V2 cursor, const std::vector<PlanItem> &plan) {
	const std::optional<TracePair> pair = trace_pair(defs, player, cursor, plan);
	if (!pair) {
		return false;
	}
	const int ci = pair->planned.cursor_index;
	if (ci < 0 || !pair->cursor_point_agrees) {
		return false;
	}
	return inv::preview_visible(pair->planned, sweep::merge(pair->planned.steps, pair->physical.steps, ci), pair->cursor_point_agrees);
}

// _assert_pinned: supported, the structure checks hold, an 11 x 7 cursor grid agrees with the preview
void assert_pinned(const std::vector<SurfaceDef> &defs, V2 player, const std::vector<PlanItem> &plan, const std::string &label) {
	Tracer t = fresh_tracer(defs);
	const vis::Pt P{player.x, player.y};
	const vis::Result region = vis::compute(t, P, plan, Flags(), VIS_BOUNDS);
	CHECK_MESSAGE(region.supported, label << ": supported");
	const inv::Violations vs = inv::check_visibility(inv::VisInput{&region, &t, P, plan, Flags(), VIS_BOUNDS});
	CHECK_MESSAGE(vs.empty(), label << ": the structure checks hold (" << (vs.empty() ? "" : vs[0].fmt) << ")");
	for (int i = 0; i < 11; i++) {
		for (int j = 0; j < 7; j++) {
			const V2 c{40.0f + float(i) * 92.0f, 30.0f + float(j) * 90.0f};
			const vis::Membership m = vis::contains(region, vis::Pt{c.x, c.y});
			if (m == vis::Membership::Unclassified) {
				continue;
			}
			CHECK_MESSAGE((m == vis::Membership::Inside) == preview_visible(defs, player, c, plan), label << ": cursor (" << c.x << ", " << c.y << ")");
		}
	}
}

} // namespace

// ================================================================ invariants

TEST_CASE("ported: invariant_cases: the corrupted traces are detected") {
	// every hand-built case trips the invariants it is named after; the clean one trips none
	for (const ICase &c : invariant_cases()) {
		const std::vector<std::string> found = ic_check(c);
		if (c.expect.empty()) {
			CHECK_MESSAGE(found.empty(), c.name << ": " << (found.empty() ? "" : found[0]));
		}
		for (const std::string &want : c.expect) {
			CHECK_MESSAGE(has(found, want), c.name << ": expected " << want);
		}
	}
}

TEST_CASE("ported: test_invariant_detects: test_each_corruption_is_detected") {
	std::set<std::string> seen;
	for (const ICase &c : invariant_cases()) {
		const std::vector<std::string> found = ic_check(c);
		for (const std::string &want : c.expect) {
			CHECK_MESSAGE(has(found, want), c.name << ": expected " << want);
			seen.insert(want);
		}
	}
	// the data-only checks all have a detecting case
	for (const char *k : {"UX7", "NOGAPS", "S16", "GREEN-FROM-PLAYER", "ORIGIN-NOT-REHIT", "SINGLE-DIVERGENCE", "NO-POST-CURSOR-DIVERGENCE",
			 "PHYSICAL-PREVIEW-MATCH", "PHYSICAL-CONTINUITY", "SOLID-PATH-TO-CURSOR", "TRACE-ENDS", "PHYSICS-COMPLIANCE", "VISUAL-ON-CARRIER"}) {
		CHECK_MESSAGE(seen.count(k) == 1, k << " has a detecting case");
	}
}

TEST_CASE("ported: test_invariant_detects: test_event_checks_detect_corrupted_events") {
	const std::vector<SurfaceDef> defs = defs_from_json(ojson::array({vdef(1, 300, -500, 300, 500, 300, 0, REFL, REFL),
		vdef(3, -900, -900, -900, 900, -900, 0, TERM, TERM), vdef(4, 900, -900, 900, 900, 900, 0, TERM, TERM)}));
	const Scene<F64> sc = Scene<F64>::from_defs(defs, F64{1.0});
	std::vector<inv::EvInfo> ev = trace_direction(sc, 0, -40, 1, 0.0625);
	REQUIRE(ev.size() >= 2);
	CHECK(inv::check_effect_law(ev, sc).empty());
	CHECK(inv::check_event_on_beam(ev, sc).empty());
	CHECK(inv::check_beam_order(ev).empty());
	// a point off its beam and off the carrier
	REQUIRE(ev[0].has_point);
	ev[0].fx += 0.5;
	ev[0].fy += 0.5;
	const std::vector<std::string> found = kinds(inv::check_event_on_beam(ev, sc));
	CHECK(has(found, "EVENT-ON-BEAM"));
	CHECK(has(found, "HITPOINT-ON-CARRIER"));
	// the plan's effects: a plan the trace did not apply
	CHECK(kinds(inv::check_plan_effects_applied_events(ev, {item(1, true)})) == std::vector<std::string>{"PLAN-EFFECTS"});
}

TEST_CASE("ported: test_stage83_invariants: test_BEAM_ORDER_detects_reordered_events") {
	// a beam with two passes before an effect: swap them
	const std::vector<SurfaceDef> defs = defs_from_json(ojson::array({vdef(1, 100, -500, 100, 500, 100, 0, WRITE, WRITE),
		vdef(2, 200, -500, 200, 500, 200, 0, WRITE, WRITE), vdef(3, 300, -500, 300, 500, 300, 0, TERM, TERM)}));
	const Scene<F64> sc = Scene<F64>::from_defs(defs, F64{1.0});
	std::vector<inv::EvInfo> ev = trace_direction(sc, 0, 0, 1, 0);
	REQUIRE(ev.size() >= 2);
	CHECK(inv::check_beam_order(ev).empty());
	std::swap(ev[0], ev[1]);
	CHECK_MESSAGE(!inv::check_beam_order(ev).empty(), "reordered events detected");
}

TEST_CASE("ported: test_stage83_invariants: test_EFFECT_LAW_detects_wrong_tangent") {
	const Scene<F64> sc = Scene<F64>::from_defs(stage83_scene(), F64{1.0});
	std::vector<inv::EvInfo> ev = trace_direction(sc, 0, -40, 1, 0.0625);
	// rotate the beam after the first applied effect: tau + 0.01 i tau, unit
	bool rotated = false;
	for (size_t i = 0; i + 1 < ev.size(); i++) {
		if (ev[i].kind == EventKind::Hit && ev[i].applied && !ev[i].terminal) {
			inv::EvInfo &n = ev[i + 1];
			const double tx = n.tx - 0.01 * n.ty, ty = n.ty + 0.01 * n.tx;
			const double len = std::hypot(tx, ty);
			n.tx = tx / len;
			n.ty = ty / len;
			rotated = true;
			break;
		}
	}
	REQUIRE(rotated);
	CHECK_MESSAGE(!inv::check_effect_law(ev, sc).empty(), "wrong outgoing tangent detected");
}

TEST_CASE("ported: test_stage83_invariants: test_EVENT_ON_BEAM_detects_offset_point") {
	const Scene<F64> sc = Scene<F64>::from_defs(stage83_scene(), F64{1.0});
	std::vector<inv::EvInfo> ev = trace_direction(sc, 0, -40, 1, 0.0625);
	REQUIRE(!ev.empty());
	REQUIRE(ev[0].has_point);
	ev[0].fx += 0.5;
	ev[0].fy += 0.5;
	CHECK_MESSAGE(!inv::check_event_on_beam(ev, sc).empty(), "offset point detected");
}

TEST_CASE("ported: test_stage83_invariants: test_PLAN_EFFECTS_APPLIED_by_surface_ids") {
	const Scene<F64> sc = Scene<F64>::from_defs(stage83_scene(), F64{1.0});
	const std::vector<inv::EvInfo> ev = trace_direction(sc, 0, -40, 1, 0.0625);
	std::vector<PlanItem> applied;
	for (const inv::EvInfo &e : ev) {
		if (e.kind == EventKind::Hit && e.applied && !e.terminal) {
			applied.push_back(item(e.surface_id, e.left));
		}
	}
	CHECK(inv::check_plan_effects_applied_events(ev, applied).empty());
	CHECK_MESSAGE(!inv::check_plan_effects_applied_events(ev, {item(99, true)}).empty(), "plan / applied mismatch detected");
}

// ================================================================ geometry

TEST_CASE("ported: test_side_hitpoints: test_at_which_endpoint_near_start") {
	// BeamGeom.seg_from_segment(f64, the line (100, 200) -> (300, 200) via (200, 200), 1)
	SegDef d;
	d.id = 1;
	d.kind = SegKind::Line;
	d.sx = 100;
	d.sy = 200;
	d.ex = 300;
	d.ey = 200;
	d.has_v = true;
	d.vx = 200;
	d.vy = 200;
	const Seg<F64> seg = geom::seg_from_def(d, F64{1.0});
	// a point near but not exactly at the start (a float32 Vector2): exact equality, no tolerance
	const CN<F64> near{F64{double(100.005f)}, F64{double(200.003f)}};
	CHECK(geom::endpoint_at(seg, near) == 0);
	CHECK(geom::endpoint_at(seg, CN<F64>{F64{100.0}, F64{200.0}}) == 1);
}

TEST_CASE("ported: test_stage51_conjugation_side: test_same_circle_exact_equality") {
	const arc::GeneralizedCircle c1{1.0, -2200.0, -1100.0, 907475.0};
	const arc::GeneralizedCircle c2{1.0, -2200.0, -1100.0, 907475.0};
	CHECK(arc::same_circle(c1, c2));
	const arc::GeneralizedCircle c3{1.0, -2200.0, -1100.0, 999999.0};
	CHECK(!arc::same_circle(c1, c3));
	CHECK(arc::same_circle(c1, c1));
}

// ================================================================ the tracer

TEST_CASE("ported: test_stage107_provenance: test_stage107_categorical_side_resolved_under_flags_before") {
	// a gate keyed on "open": the toggler before it opens it in the same shot;
	// the gate's record carries the flags it was resolved under
	auto vline = [](int id, double x, const ojson &l, const ojson &r) {
		return ojson{{"id", id}, {"line", true}, {"start", {x, -400.0}}, {"end", {x, 400.0}}, {"via", {x, 0.0}}, {"full", false}, {"circle", nullptr},
			{"left", l}, {"right", r}, {"is_target", false}};
	};
	auto sd = [](const char *type, ojson states = ojson::array()) { return ojson{{"type", type}, {"interactive", true}, {"states", states}}; };
	const ojson tog = sd("pass", ojson::array({{{"key", "open"}, {"op", "toggle"}, {"value", true}}}));
	ojson gate = vline(2, 500.0, nullptr, nullptr);
	// (open: a pass that writes a flag -- Stage 134 computes no pure pass-through)
	const ojson seen = sd("pass", ojson::array({{{"key", "seen"}, {"op", "set"}, {"value", true}}}));
	gate["resolver"] = {{"key", "open"},
		{"table", {{"true", {{"left", seen}, {"right", seen}}}, {"DEFAULT", {{"left", sd("terminal")}, {"right", sd("terminal")}}}}}};
	const std::vector<SurfaceDef> defs = defs_from_json(ojson::array({vline(1, 300.0, tog, tog), gate, vline(3, 900.0, sd("terminal"), sd("terminal"))}));
	const std::optional<TracePair> pair = trace_pair(defs, V2{100, 0}, V2{200, 0});
	REQUIRE(pair);
	const std::vector<HitRecord> &hits = pair->physical.hits;
	std::vector<int> ids;
	for (const HitRecord &h : hits) {
		ids.push_back(h.surface_id);
	}
	CHECK_MESSAGE((ids == std::vector<int>{1, 2, 3}), "the open gate passes");
	REQUIRE(hits.size() >= 2);
	Flags open;
	open.set("open", true);
	CHECK(hits[1].flags_before == open);
}

TEST_CASE("ported: test_stage110_truncation: test_a_path_that_ends_is_not_truncated") {
	const std::optional<TracePair> pair = trace_pair({block(1, V2{400, 0}, V2{400, 600}).def}, V2{100, 300}, V2{200, 300});
	REQUIRE(pair);
	CHECK_MESSAGE(!pair->physical.truncated, "a path ending at a block is not cut");
}

TEST_CASE("ported: test_stage47_escape_via: test_line_escape_endpoint_is_infinity") {
	const std::optional<TracePair> pair = trace_pair({}, V2{500, 300}, V2{800, 300});
	REQUIRE(pair);
	const TracePath &path = pair->physical;
	CHECK_MESSAGE(!path.steps.empty(), "escape steps");
	bool has_inf_endpoint = false;
	for (const TraceStep &st : path.steps) {
		if (std::isinf(st.end.x) || std::isinf(st.end.y) || std::isinf(st.start.x) || std::isinf(st.start.y)) {
			has_inf_endpoint = true;
		}
	}
	CHECK_MESSAGE(has_inf_endpoint, "the line escape's endpoint is infinity, not a finite far-away point");
}

TEST_CASE("ported: test_stage47_escape_via: test_line_escape_via_stores_direction") {
	const std::optional<TracePair> pair = trace_pair({}, V2{500, 300}, V2{800, 300});
	REQUIRE(pair);
	bool found_escape = false;
	for (const TraceStep &st : pair->physical.steps) {
		if (!st.hit && !st.is_arc_step) {
			found_escape = true;
			CHECK_MESSAGE(!(std::isinf(st.via.x) && std::isinf(st.via.y)), "the escape's via stores the direction, not INF");
		}
	}
	CHECK_MESSAGE(found_escape, "line escape steps");
}

TEST_CASE("ported: test_stage49_mid_air_termination: test_all_visual_hitpoints_on_physical_carrier") {
	const std::vector<GameSurf> ss = stage49_surfaces();
	const std::optional<TracePair> pair = trace_pair(defs_of(ss), S49_PLAYER, S49_AIM);
	REQUIRE(pair);
	const std::vector<std::string> vs = visual_hitpoint_violations(pair->physical, ss);
	CHECK_MESSAGE(vs.empty(), (vs.empty() ? std::string() : vs[0]));
}

TEST_CASE("ported: test_stage49_mid_air_termination: test_terminal_endpoint_on_terminal_carrier") {
	const std::vector<GameSurf> ss = stage49_surfaces();
	const std::optional<TracePair> pair = trace_pair(defs_of(ss), S49_PLAYER, S49_AIM);
	REQUIRE(pair);
	const std::vector<std::string> vs = terminal_violations(pair->physical, ss);
	CHECK_MESSAGE(vs.empty(), (vs.empty() ? std::string() : vs[0]));
}

TEST_CASE("ported: test_stage49_mid_air_termination: test_sweep_all_visual_hitpoints") {
	const std::vector<GameSurf> ss = stage49_surfaces();
	const std::vector<SurfaceDef> defs = defs_of(ss);
	Tracer t = fresh_tracer(defs);
	int total = 0;
	int violations = 0;
	std::string first;
	for (int px = 200; px < 1700; px += 100) {
		for (int py = 100; py < 1000; py += 100) {
			const V2 player{float(px), float(py)};
			for (int deg = 0; deg < 360; deg += 15) {
				const double angle = double(deg) * M_PI / 180.0;
				const V2 target = player + V2{float(std::cos(angle)), float(std::sin(angle))} * 100.0f;
				const std::optional<TracePair> pair = t.trace_pair(player, target, {}, Flags());
				if (!pair) {
					continue;
				}
				const std::vector<std::string> vs = visual_hitpoint_violations(pair->physical, ss);
				for (const TraceStep &st : pair->physical.steps) {
					total += hit_on_scene(st, ss) ? 1 : 0;
				}
				if (!vs.empty() && first.empty()) {
					first = "player (" + std::to_string(px) + ", " + std::to_string(py) + ") angle " + std::to_string(deg) + ": " + vs[0];
				}
				violations += int(vs.size());
			}
		}
	}
	MESSAGE("stage 49 sweep: " << violations << "/" << total << " hitpoints off their carriers");
	CHECK_MESSAGE(violations == 0, first);
}

TEST_CASE("ported: test_stage86_ghost: test_plan_refuses_non_toggler_inside_ghost_stretch") {
	const ojson dump = load_json("bench/scenes/test_levels__ghost_room.surfaces.json");
	const std::vector<SurfaceDef> defs = defs_from_json(dump["surfaces"]);
	auto at = [&](double x, double y) {
		for (const SurfaceDef &d : defs) {
			if (d.sx == x && d.sy == y) {
				return d.id;
			}
		}
		FAIL("no surface starts at (" << x << ", " << y << ")");
		return -1;
	};
	const int a = at(1000, 400), b = at(650, 400), blk = at(1100, 400), mir = at(720, 480);
	Tracer t = fresh_tracer(defs);
	// solid plan: everything interactive is accepted
	CHECK(!t.plan_refuses({}, mir, true, Flags()));
	// after A the plan is ghosted: the mirror is refused (both sides), the togglers are not
	const std::vector<PlanItem> ghosted = {item(a, true)};
	CHECK_MESSAGE(t.plan_refuses(ghosted, mir, true, Flags()), "mirror inside the stretch");
	CHECK_MESSAGE(t.plan_refuses(ghosted, mir, false, Flags()), "both sides");
	CHECK_MESSAGE(t.plan_refuses(ghosted, blk, true, Flags()), "non-interactive too");
	CHECK_MESSAGE(!t.plan_refuses(ghosted, b, false, Flags()), "the second toggler");
	CHECK_MESSAGE(!t.plan_refuses(ghosted, a, true, Flags()), "the same toggler again");
	// after A, B the plan is solid again
	CHECK(!t.plan_refuses({item(a, true), item(b, false)}, mir, true, Flags()));
	// ghost is per shot: a plan starts solid whatever the game state holds
	Flags ghost;
	ghost.set(GHOST, true);
	CHECK(!t.plan_refuses({}, mir, true, ghost));
}

TEST_CASE("ported: test_stage88_infinity: test_tracer_infinity_entry_reaches_the_cursor_through_the_wrap") {
	const ojson fixtures = load_json("bench/fixtures/stage88_infinity.json");
	ojson fx;
	for (const ojson &x : fixtures["fixtures"]) {
		if (x["name"] == "infinity_only_planned") {
			fx = x;
		}
	}
	REQUIRE(!fx.is_null());
	const V2 player{float(fx["player"][0].get<double>()), float(fx["player"][1].get<double>())};
	const V2 cursor{float(fx["cursor"][0].get<double>()), float(fx["cursor"][1].get<double>())};
	const std::vector<PlanItem> plan = plan_from_json(fx["plan"]);
	Tracer t; // BeamTracer.new(): the switch off
	t.set_defs(defs_from_json(fx["surfaces"]));
	const std::optional<TracePair> off = t.trace_pair(player, cursor, plan, Flags());
	REQUIRE(off);
	CHECK_MESSAGE(off->planned.cursor_index == -1, "switch off: the infinity entry is never consumed, the cursor is not reached");
	t.set_emit_infinity(true);
	const std::optional<TracePair> on = t.trace_pair(player, cursor, plan, Flags());
	REQUIRE(on);
	CHECK_MESSAGE(on->planned.cursor_index > 0, "switch on: the planned path reaches the cursor");
	const std::vector<TraceStep> &steps = on->planned.steps;
	REQUIRE(steps.size() >= 2);
	CHECK_MESSAGE(steps[0].surface_id == INFINITY_ID, "the outgoing half of the wrap carries INFINITY_ID");
	CHECK_MESSAGE(std::isinf(steps[0].end.x), "it leaves to infinity");
	CHECK_MESSAGE(std::isinf(steps[1].start.x), "the incoming half returns from infinity");
	CHECK(steps[1].surface_id == -1);
	const V2 dir0 = steps[0].via.normalized();
	const V2 aim = (cursor - player).normalized();
	CHECK_MESSAGE(double(dir0.dot(aim)) < 0.0, "the aim is reversed: away from the cursor");
	bool listed = false;
	for (const auto &[sid, of_surface] : physical_hits(on->physical.steps)) {
		listed = listed || sid == INFINITY_ID;
	}
	CHECK_MESSAGE(listed, "physical_hits lists the wrap under INFINITY_ID");
}

TEST_CASE("ported: test_tracer: test_target_surface_tracked") {
	// Surface.new(seg, terminal, terminal, is_target): its sides write target:<id> (TargetHit)
	const std::vector<StateChangeDef> writes = {StateChangeDef{"target:1", false, true}};
	const GameSurf target = surf(1, V2{400, 0}, V2{400, 600}, V2{400, 300}, side(EffectType::Terminal, false, writes),
		side(EffectType::Terminal, false, writes), false, true);
	const std::optional<TracePair> pair = trace_pair({target.def}, V2{200, 300}, V2{600, 300});
	REQUIRE(pair);
	const std::vector<int> &th = pair->physical.targets_hit;
	CHECK_MESSAGE(std::find(th.begin(), th.end(), 1) != th.end(), "the target is tracked");
}

// ================================================================ visibility

TEST_CASE("ported: test_stage110_visibility: test_empty_room_is_all_visible") {
	const std::vector<SurfaceDef> defs = defs_of(room(1, 0, 0, 1000, 600));
	Tracer t = fresh_tracer(defs);
	const vis::Result region = vis::compute(t, vis::Pt{200, 300}, {}, Flags(), VIS_BOUNDS);
	CHECK(region.supported);
	CHECK(!region.cells.empty());
	CHECK_MESSAGE(vis::contains(region, vis::Pt{800, 100}) == vis::Membership::Inside, "a far corner of the empty room");
	CHECK_MESSAGE(vis::contains(region, vis::Pt{200, 300}) == vis::Membership::Unclassified, "the player itself is not classified");
	CHECK_MESSAGE(vis::contains(region, vis::Pt{5000, 5000}) == vis::Membership::Unclassified, "outside the level bounds: not classified");
}

TEST_CASE("ported: test_stage110_visibility: test_a_block_casts_a_shadow") {
	std::vector<GameSurf> ss = {block(1, V2{500, 200}, V2{500, 400})};
	for (const GameSurf &r : room(2, 0, 0, 1000, 600)) {
		ss.push_back(r);
	}
	const std::vector<SurfaceDef> defs = defs_of(ss);
	Tracer t = fresh_tracer(defs);
	const vis::Result region = vis::compute(t, vis::Pt{200, 300}, {}, Flags(), VIS_BOUNDS);
	CHECK_MESSAGE(vis::contains(region, vis::Pt{400, 300}) == vis::Membership::Inside, "in front of the block");
	CHECK_MESSAGE(vis::contains(region, vis::Pt{700, 300}) == vis::Membership::Outside, "behind the block");
	CHECK_MESSAGE(vis::contains(region, vis::Pt{500, 300}) == vis::Membership::Inside, "on the block: the cursor is reached first");
	assert_pinned(defs, V2{200, 300}, {}, "block");
}

TEST_CASE("ported: test_stage110_visibility: test_a_terminal_in_the_plan_keeps_only_off_segment_aims") {
	std::vector<GameSurf> ss = {block(1, V2{500, 250}, V2{500, 350})};
	for (const GameSurf &r : room(2, 0, 0, 1000, 600)) {
		ss.push_back(r);
	}
	assert_pinned(defs_of(ss), V2{200, 300}, {item(1, true)}, "terminal entry");
}
