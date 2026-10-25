// Stage 156 (GAME_SPEC 10.6, the user): arrow states. Conditional state changes
// (each read under the flags its side fired under) carry fire and charge
// between the arrow and surfaces; frost turns a side that would stop the arrow
// into a reflection (a line) or an inversion (a circle), once
// (bench/fixtures/stage156_arrow_states.json, the oracle's).
#include "goldens.hpp"
#include "oracle_fixtures.hpp"

#include <doctest/doctest.h>

using namespace rc;
using namespace rc::test;

TEST_CASE("arrow states: the Stage 156 fixtures match the oracle at f64, Dual and 4 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage156_arrow_states.json");
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
	CHECK(fixtures == 9);
}

TEST_CASE("arrow states: a side's conditional changes read the flags it fired under") {
	Flags f;
	f.set("shot:charge", true);
	const std::vector<FlagCond> give{{"shot:charge", {"true"}}, {"el:c:charge", {"DEFAULT", "false"}}};
	const std::vector<FlagCond> take{{"shot:charge", {"DEFAULT", "false"}}, {"el:c:charge", {"true"}}};
	const std::vector<StateChangeDef> swap{{"shot:charge", false, false, give}, {"el:c:charge", false, true, give},
		{"shot:charge", false, true, take}, {"el:c:charge", false, false, take}};
	const Flags a = f.applied(swap); // given, not taken back by the later rows
	CHECK_FALSE(a.is_true("shot:charge"));
	CHECK(a.is_true("el:c:charge"));
	const Flags b = a.applied(swap); // taken
	CHECK(b.is_true("shot:charge"));
	CHECK_FALSE(b.is_true("el:c:charge"));
	CHECK(b.is_true(FROST) == false);
	Flags g;
	g.set(FROST, true);
	CHECK(g.is_true(FROST));
	g.erase(FROST);
	CHECK_FALSE(g.is_true(FROST));
}
