// Stage 107: a side's list of state changes and surface presence (GAME_SPEC
// 9.6 / 10.6). The native solver must reproduce the oracle's Stage 107
// fixtures (trace2.py, bench/fixtures/stage107_hit_effects.json) at every
// tier; the unit cases pin the scene-level rules directly.
#include "oracle_fixtures.hpp"
#include "rc/tracer.hpp"

#include <doctest/doctest.h>

using namespace rc;
using namespace rc::test;

namespace {

StateChangeDef set_(const std::string &key, FlagValue v) { return StateChangeDef{key, false, std::move(v)}; }
StateChangeDef tog(const std::string &key) { return StateChangeDef{key, true, true}; }

SurfaceDef vline(int id, double x, std::optional<SideDef> left, std::optional<SideDef> right) {
	SurfaceDef d;
	d.id = id;
	d.line = true;
	d.sx = x;
	d.sy = -400;
	d.ex = x;
	d.ey = 400;
	d.has_via = true;
	d.vx = x;
	d.vy = 0;
	d.left = std::move(left);
	d.right = std::move(right);
	return d;
}

SideDef side(EffectType t, std::vector<StateChangeDef> states = {}) {
	SideDef s;
	s.type = t;
	s.interactive = true;
	s.states = std::move(states);
	return s;
}

} // namespace

TEST_CASE("hit effects: the Stage 107 fixtures match the oracle at f64, Dual, 4 and 64 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage107_hit_effects.json");
	int fixtures = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1}, true);
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0}, true);
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto rb = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4), true);
		CHECK_MESSAGE(diff_against_oracle(rb, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(rb, fx["expected"])));
		const auto r64l = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 64), true);
		CHECK_MESSAGE(diff_against_oracle(r64l, fx["expected"]) == "", (name + " big64: " + diff_against_oracle(r64l, fx["expected"])));
		fixtures++;
	}
	CHECK(fixtures == 11);
}

TEST_CASE("hit effects: a side's state changes resolve in order, ghost by any write") {
	SurfaceDef d = vline(1, 300, side(EffectType::Pass, {set_("k", std::string("x")), tog("k")}), side(EffectType::Pass, {tog("ghost")}));
	const Scene<F64> sc = Scene<F64>::from_defs({d}, F64{1});
	const Surf<F64> &s = *sc.surfaces.front();
	CHECK(s.state_changes(true).size() == 2);
	CHECK(Flags().applied(s.state_changes(true)).canonical_key() == Flags().applied(set_("k", false)).canonical_key());
	Flags ghost;
	ghost.set(GHOST, true);
	CHECK(s.ghosted(true, ghost));        // writes k only
	CHECK(!s.ghosted(false, ghost));      // writes ghost: a toggler
	CHECK(s.state_changes(true, ghost).empty());
	CHECK(s.state_changes(false, ghost).size() == 1);
}

TEST_CASE("hit effects: presence is an AND of conditions, DEFAULT a missing key") {
	SurfaceDef d = vline(1, 300, side(EffectType::Reflection), side(EffectType::Reflection));
	d.presence = {PresenceCond{"chan:c", {"DEFAULT", "armed"}}, PresenceCond{"b", {"true"}}};
	const Scene<F64> sc = Scene<F64>::from_defs({d}, F64{1});
	const Surf<F64> &s = *sc.surfaces.front();
	Flags f;
	CHECK(!s.present(f));
	f.set("b", true);
	CHECK(s.present(f));
	f.set("chan:c", std::string("armed"));
	CHECK(s.present(f));
	f.set("chan:c", std::string("pending"));
	CHECK(!s.present(f));
}

TEST_CASE("hit effects: planning skips an absent entry and refuses to add one") {
	SurfaceDef host = vline(1, 300, side(EffectType::Pass, {set_("chan:c", std::string("pending"))}),
		side(EffectType::Pass, {set_("chan:c", std::string("pending"))}));
	host.presence = {PresenceCond{"chan:c", {"DEFAULT", "armed"}}};
	SurfaceDef group = vline(2, 500, side(EffectType::Reflection, {set_("seen", true)}), side(EffectType::Reflection));
	group.presence = {PresenceCond{"chan:c", {"g1"}}};
	const std::vector<SurfaceDef> defs{host, group};
	const Scene<F64> sc = Scene<F64>::from_defs(defs, F64{1});
	const Solver<F64> solver(sc, nullptr);
	// the group is absent at the start: no effect, no write
	const std::vector<PlanItem> plan{PlanItem{2, true, std::nullopt, std::nullopt}};
	CHECK(solver.plan_effects(plan, Flags()) == std::vector<EffectType>{EffectType::Pass});
	CHECK(solver.plan_flags(plan, Flags()).canonical_key() == Flags().canonical_key());
	// the host consumes itself: an entry on it after itself is absent
	const std::vector<PlanItem> twice{PlanItem{1, true, std::nullopt, std::nullopt}, PlanItem{1, true, std::nullopt, std::nullopt}};
	Flags after;
	after.set("chan:c", std::string("pending"));
	CHECK(solver.plan_flags(twice, Flags()).canonical_key() == after.canonical_key());
	// the tracer refuses an absent surface at the click
	Tracer tr;
	tr.set_defs(defs);
	CHECK(tr.plan_refuses({}, 2, true, Flags()));
	CHECK(!tr.plan_refuses({}, 1, true, Flags()));
	CHECK(tr.plan_refuses({PlanItem{1, true, std::nullopt, std::nullopt}}, 1, true, Flags()));
}

TEST_CASE("hit effects: the trace records every hit event's provenance") {
	// a host (pass, consumes itself) at x = 300, a mirror present only while
	// armed at x = 500 (vanishes), a wall at x = 900; the player at (100, 0)
	// shooting right, the cursor beyond the wall's reach on the beam
	SurfaceDef host = vline(1, 300, side(EffectType::Pass, {set_("chan:c", std::string("pending"))}),
		side(EffectType::Pass, {set_("chan:c", std::string("pending"))}));
	host.presence = {PresenceCond{"chan:c", {"DEFAULT", "armed"}}};
	SurfaceDef mirror = vline(2, 500, side(EffectType::Reflection), side(EffectType::Reflection));
	mirror.presence = {PresenceCond{"chan:c", {"DEFAULT", "armed"}}};
	SurfaceDef wall = vline(3, 900, side(EffectType::Terminal), side(EffectType::Terminal));
	Tracer tr;
	tr.set_defs({host, mirror, wall});
	const std::optional<TracePair> pair = tr.trace_pair(V2::of(100, 0), V2::of(200, 0), {}, Flags());
	REQUIRE(pair);
	const std::vector<HitRecord> &hs = pair->physical.hits;
	REQUIRE(hs.size() == 2);
	// the host fires and writes; the mirror is gone; the wall stops the shot
	CHECK(hs[0].surface_id == 1);
	CHECK(hs[0].fired);
	CHECK(hs[0].state);
	CHECK(hs[0].on_segment);
	CHECK(hs[0].point == V2::of(300, 0));
	CHECK(hs[0].tangent == V2::of(1, 0));
	CHECK(hs[0].flags_before.canonical_key() == Flags().canonical_key());
	CHECK(hs[1].surface_id == 3);
	CHECK(hs[1].fired);
	CHECK(!hs[1].state);
	CHECK(hs[1].flags_before.get("chan:c") != nullptr);
	CHECK(hs[0].step_index >= 0);
	CHECK(hs[1].step_index > hs[0].step_index);
	CHECK(pair->final_flags.canonical_key() == hs[1].flags_before.canonical_key());
}
