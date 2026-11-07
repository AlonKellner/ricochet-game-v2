// Stage 199 (GAME_SPEC 12.6, the user): the level's infinity_loop. True, the walks
// as without it; false, a passage of infinity uses up the significant events.
// (bench/fixtures/stage199_infinity_loop.json, the oracle's.)
#include "goldens.hpp"
#include "oracle_fixtures.hpp"
#include "rc/tracer.hpp"

#include <doctest/doctest.h>

using namespace rc;
using namespace rc::test;

TEST_CASE("infinity loop: the Stage 199 fixtures match the oracle at f64, Dual and 4 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage199_infinity_loop.json");
	int fixtures = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1});
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0});
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto rb = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4));
		CHECK_MESSAGE(diff_against_oracle(rb, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(rb, fx["expected"])));
		fixtures++;
	}
	CHECK(fixtures == 14);
}

TEST_CASE("terminal cursor: the Stage 201 fixtures match the oracle at f64, Dual and 4 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage201_terminal_cursor.json");
	int fixtures = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1});
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0});
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto rb = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4));
		CHECK_MESSAGE(diff_against_oracle(rb, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(rb, fx["expected"])));
		fixtures++;
	}
	CHECK(fixtures == 13);
}

// Stage 206 (GAME_SPEC 12.1, the user): counterpart surfaces -- where the planned walk's beam cannot reach its next
// entry's surface before the frame's infinity, it meets the surface's counterpart (the tangent-length circle about the
// ray's origin, a line's mirror image through it, mapped by the frame): a significant "counterpart" event that
// consumes nothing; a planned terminal's ends the walk. (bench/fixtures/stage206_counterpart.json, the oracle's.)
TEST_CASE("counterpart: the Stage 206 fixtures match the oracle at f64, Dual and 4 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage206_counterpart.json");
	int fixtures = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1});
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0});
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto rb = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4));
		CHECK_MESSAGE(diff_against_oracle(rb, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(rb, fx["expected"])));
		fixtures++;
	}
	CHECK(fixtures == 20);
}

TEST_CASE("counterpart: a wide budget (the realization's walks) and the region's solves compute none (Stage 206)") {
	const ojson fxs = load_json("bench/fixtures/stage206_counterpart.json");
	for (const ojson &fx : fxs["fixtures"]) {
		if (fx["name"].get<std::string>() != "circle_mirror_outside_cone_off") {
			continue;
		}
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		Scene<F64> scene = Scene<F64>::from_defs(defs, F64{1}, false);
		SolveCache<F64> cache;
		Solver<F64> solver(scene, &cache);
		const std::vector<PlanItem> plan = plan_from_json(fx["plan"]);
		const CN<F64> player{F64{0}, F64{0}};
		const CN<F64> east{F64{1}, F64{0}};
		const auto kinds = [](const std::vector<Event<F64>> &ev) {
			std::string out;
			for (const Event<F64> &e : ev) {
				out += std::string(event_kind_name(e.kind)) + " ";
			}
			return out;
		};
		const Walk<F64> game = solver.run_direction(player, east, plan, std::nullopt, Mode::Planned, MAX_HITS, Flags());
		CHECK(kinds(game.events) == "counterpart max ");
		// the realization's wide budget: geometry, no counterpart (as no cut at infinity)
		const Walk<F64> wide = solver.run_direction(player, east, plan, std::nullopt, Mode::Planned, 8 * MAX_HITS, Flags());
		CHECK(kinds(wide.events).find("counterpart") == std::string::npos);
		// the switch off: none either
		solver.counterparts = false;
		const Walk<F64> off = solver.run_direction(player, east, plan, std::nullopt, Mode::Planned, MAX_HITS, Flags());
		CHECK(kinds(off.events) == "max ");
	}
	// the region's solve (Tracer::solve with counterparts false) holds no counterpart where the preview's does: a
	// planned terminal the aim misses, the cursor before it (the fixture terminal_missed_cursor_before_planned)
	for (const ojson &fx : fxs["fixtures"]) {
		if (fx["name"].get<std::string>() != "terminal_missed_cursor_before_planned") {
			continue;
		}
		Tracer t;
		t.set_defs(defs_from_json(fx["surfaces"]), true);
		const std::vector<PlanItem> plan = plan_from_json(fx["plan"]);
		const auto preview = t.trace_pair(V2::of(0, 0), V2::of(100, 0), plan, Flags());
		REQUIRE(preview);
		bool seen = false;
		for (const TraceStep &st : preview->planned.steps) {
			seen = seen || st.counterpart;
		}
		CHECK(seen);
		CHECK(preview->planned.steps.back().counterpart);
		const auto region = t.solve(0, 0, 100, 0, plan, Flags(), std::nullopt, -1, std::nullopt, true, false);
		REQUIRE(region);
		bool region_cp = false;
		if (region->planned.limbs == 0) {
			for (const auto &e : region->planned.f64_events) {
				region_cp = region_cp || e.kind == EventKind::Counterpart;
			}
		} else {
			for (const auto &e : region->planned.big_events) {
				region_cp = region_cp || e.kind == EventKind::Counterpart;
			}
		}
		CHECK_FALSE(region_cp);
	}
}

TEST_CASE("infinity loop: the tracer's walks read it, and a change of it rebuilds the solve") {
	const std::vector<SurfaceDef> none;
	const auto events = [](Tracer &t) { return t.trace_pair(V2::of(0, 0), V2::of(50, 0), {}, Flags())->physical.steps.size(); };
	Tracer t;
	t.set_defs(none);
	const size_t round = events(t);
	CHECK(t.infinity_loop());
	t.set_defs(none, false);
	CHECK_FALSE(t.infinity_loop());
	const size_t once = events(t);
	CHECK(once < round);
	t.set_defs(none, true);
	CHECK(events(t) == round);
}
