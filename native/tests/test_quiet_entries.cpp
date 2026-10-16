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
	CHECK(fixtures == 10);
}

TEST_CASE("quiet entries: the preview's two paths agree up to the cursor") {
	const ojson fxs = load_json("bench/fixtures/stage110_quiet_entries.json");
	int pairs = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		if (name.rfind("toggle_", 0) == 0 || name.find("_physical") == std::string::npos) {
			continue; // one shot per planned / physical pair; the toggler diverges on purpose
		}
		Tracer tracer;
		tracer.set_emit_infinity(true);
		tracer.set_defs(defs_from_json(fx["surfaces"]));
		const V2 player{float(fx["player"][0].get<double>()), float(fx["player"][1].get<double>())};
		const V2 cursor{float(fx["cursor"][0].get<double>()), float(fx["cursor"][1].get<double>())};
		const Flags flags = fx.contains("flags") && !fx["flags"].is_null() ? flags_from_json(fx["flags"]) : Flags();
		const std::optional<TracePair> pair = tracer.trace_pair(player, cursor, plan_from_json(fx["plan"]), flags);
		REQUIRE_MESSAGE(pair.has_value(), name);
		CHECK_MESSAGE(pair->physical.cursor_index >= 0, name);
		REQUIRE_MESSAGE(pair->planned.cursor_index == pair->physical.cursor_index, name);
		for (int i = 0; i <= pair->planned.cursor_index; i++) {
			const TraceStep &a = pair->planned.steps[size_t(i)];
			const TraceStep &b = pair->physical.steps[size_t(i)];
			const bool same = a.start == b.start && a.end == b.end && a.frame_id == b.frame_id;
			CHECK_MESSAGE(same, (name + " step " + std::to_string(i)));
		}
		pairs++;
	}
	CHECK(pairs == 4);
}
