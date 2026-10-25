// Stage 110, B1: an entry the planned walk consumes without changing the frame
// (a pass side, a terminal crossed off its segment, a write of the value a flag
// already has) is consumed by the physical walk at the same event (GAME_SPEC
// 14.1). The oracle's fixtures (gen_fixtures_stage110.py) at every tier, and
// the traces the preview draws: both paths reach the cursor, step for step.
#include "oracle_fixtures.hpp"
#include "rc/sweep.hpp"
#include "rc/tracer.hpp"

#include <doctest/doctest.h>

using namespace rc;
using namespace rc::test;

TEST_CASE("quiet entries: the Stage 110 fixtures match the oracle at f64, Dual, 4 and 64 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage110_quiet_entries.json");
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
		const auto r64l = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 64));
		CHECK_MESSAGE(diff_against_oracle(r64l, fx["expected"]) == "", (name + " big64: " + diff_against_oracle(r64l, fx["expected"])));
		fixtures++;
	}
	CHECK(fixtures == 12);
}

TEST_CASE("quiet entries: the preview's two paths agree up to the cursor") {
	const ojson fxs = load_json("bench/fixtures/stage110_quiet_entries.json");
	int pairs = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		if (name.rfind("toggle_", 0) == 0 || name.rfind("terminal_", 0) == 0 || name.find("_physical") == std::string::npos) {
			continue; // one shot per planned / physical pair; the toggler diverges on purpose; a planned terminal: below
		}
		Tracer tracer;
		tracer.set_defs(defs_from_json(fx["surfaces"]));
		const V2 player{float(fx["player"][0].get<double>()), float(fx["player"][1].get<double>())};
		const V2 cursor{float(fx["cursor"][0].get<double>()), float(fx["cursor"][1].get<double>())};
		const Flags flags = fx.contains("flags") && !fx["flags"].is_null() ? flags_from_json(fx["flags"]) : Flags();
		const std::optional<TracePair> pair = tracer.trace_pair(player, cursor, plan_from_json(fx["plan"]), flags);
		REQUIRE_MESSAGE(pair.has_value(), name);
		CHECK_MESSAGE(pair->physical.cursor_index >= 0, name);
		// (Stage 134: the walks compute different candidates; they agree by their
		// significant events up to the cursor, and leave its point in one frame)
		CHECK_MESSAGE(pair->cursor_point_agrees, name);
		pairs++;
	}
	CHECK(pairs == 3);
}

TEST_CASE("quiet entries: a planned terminal stops the planned walk -- met on its segment the paths agree, crossed off it they diverge (Stage 162)") {
	const ojson fxs = load_json("bench/fixtures/stage110_quiet_entries.json");
	int seen = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		if (name.rfind("terminal_", 0) != 0 || name.find("_physical") == std::string::npos) {
			continue;
		}
		Tracer tracer;
		tracer.set_defs(defs_from_json(fx["surfaces"]));
		const V2 player{float(fx["player"][0].get<double>()), float(fx["player"][1].get<double>())};
		const V2 cursor{float(fx["cursor"][0].get<double>()), float(fx["cursor"][1].get<double>())};
		const std::optional<TracePair> pair = tracer.trace_pair(player, cursor, plan_from_json(fx["plan"]), Flags());
		REQUIRE_MESSAGE(pair.has_value(), name);
		const rc::TracePath &pl = pair->planned, &ph = pair->physical;
		REQUIRE(!pl.steps.empty());
		REQUIRE(!ph.steps.empty());
		if (name.find("on_segment") != std::string::npos) {
			CHECK(pl.steps.back().end == ph.steps.back().end); // both stop at the terminal
			CHECK(pl.steps.size() == ph.steps.size());
		} else {
			CHECK(pl.cursor_index < 0);  // the planned walk stops beside the segment
			CHECK(ph.cursor_index >= 0); // the physical one passes and reaches the cursor
		}
		seen++;
	}
	CHECK(seen == 2);
}

// Stage 110, B5: surfaces on one carrier are crossed at one point (K6); an entry
// there is matched by the frame the physical walk leaves the point in, whatever
// the order of the surfaces (gen_fixtures_stage110_frames.py).
TEST_CASE("frame entries: the Stage 110 B5 fixtures match the oracle at f64, Dual, 4 and 64 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage110_frame_entries.json");
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
		const auto r64l = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 64));
		CHECK_MESSAGE(diff_against_oracle(r64l, fx["expected"]) == "", (name + " big64: " + diff_against_oracle(r64l, fx["expected"])));
		fixtures++;
	}
	CHECK(fixtures == 22);
}

TEST_CASE("frame entries: the preview's two paths agree up to the cursor") {
	const ojson fxs = load_json("bench/fixtures/stage110_frame_entries.json");
	int pairs = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		if (name.rfind("different_", 0) == 0 || name.rfind("transform_", 0) == 0 || name.rfind("two_writes_one_entry", 0) == 0 ||
				name.find("_physical") == std::string::npos) {
			continue; // one shot per pair; the controls diverge on purpose (Stage 138: the shared point stops the physical walk)
		}
		Tracer tracer;
		tracer.set_defs(defs_from_json(fx["surfaces"]));
		const V2 player{float(fx["player"][0].get<double>()), float(fx["player"][1].get<double>())};
		const V2 cursor{float(fx["cursor"][0].get<double>()), float(fx["cursor"][1].get<double>())};
		const Flags flags = fx.contains("flags") && !fx["flags"].is_null() ? flags_from_json(fx["flags"]) : Flags();
		const std::optional<TracePair> pair = tracer.trace_pair(player, cursor, plan_from_json(fx["plan"]), flags);
		REQUIRE_MESSAGE(pair.has_value(), name);
		CHECK_MESSAGE(pair->physical.cursor_index >= 0, name);
		CHECK_MESSAGE(pair->cursor_point_agrees, name); // (Stage 134: by significant events)
		pairs++;
	}
	CHECK(pairs == 7);
}
