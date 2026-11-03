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
