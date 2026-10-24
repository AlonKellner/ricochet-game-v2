// Stage 138 (GAME_SPEC 11.1, the user): a hit exactly at a point two or more
// present surfaces hold -- a corner, a T-junction, a proper crossing -- stops
// the physical walk when two or more surfaces there have a consequence, nothing
// firing (the planned walk follows its plan: the walks diverge); a lone endpoint
// grazes
// (bench/fixtures/stage138_shared_points.json, the oracle's).
#include "goldens.hpp"
#include "oracle_fixtures.hpp"

#include <doctest/doctest.h>

using namespace rc;
using namespace rc::test;

TEST_CASE("shared points: the Stage 138 fixtures match the oracle at f64, Dual and 4 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage138_shared_points.json");
	int fixtures = 0;
	int stops = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1});
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0});
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto rb = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4));
		CHECK_MESSAGE(diff_against_oracle(rb, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(rb, fx["expected"])));
		if (r64.size() == 1 && r64[0].kind == EventKind::Hit && r64[0].terminal) {
			stops++;
		}
		fixtures++;
	}
	CHECK(fixtures == 14);
	CHECK(stops == 5); // the corner, the T-junction, the crossings of a mirror and a wall, a mirror and a toggler, two togglers: the physical walks
}
