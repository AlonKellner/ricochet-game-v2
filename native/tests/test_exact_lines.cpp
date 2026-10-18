// Stage 112 (K10): an exact line never meets a parallel carrier. The fixtures
// (bench/fixtures/stage112_parallel.json): the planned chain after a
// projective leaves along an exact line; a tier's rounded beam would cross the
// parallel walls at y ~ 2^61 before the wrap. The walker decides the crossing's
// existence on the exact line (Line::parallel_to), at every tier.
#include "oracle_fixtures.hpp"

#include <doctest/doctest.h>

using namespace rc;
using namespace rc::test;

TEST_CASE("Stage 112: the parallel-carrier fixtures match the oracle at f64, Dual, 4 and 64 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage112_parallel.json");
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
		const auto rb64 = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 64), true);
		CHECK_MESSAGE(diff_against_oracle(rb64, fx["expected"]) == "", (name + " big64: " + diff_against_oracle(rb64, fx["expected"])));
		fixtures++;
	}
	CHECK(fixtures == 6);
}

TEST_CASE("Stage 112: Line::parallel_to decides exactly") {
	// the vertical line x = 250 (exact) against the walls x = 400 and x = 1000, and a line a rounding away from vertical
	const auto L = exact::Line::from_exact(exact::line_from_dir(250.0, 200.0, 0.0, 1.0));
	CHECK(L->parallel_to(400.0, -500.0, 400.0, 200.0));
	CHECK(L->parallel_to(1000.0, -1000.0, 1000.0, 1000.0));
	CHECK(!L->parallel_to(0.0, 450.0, 380.0, 450.0));
	const double tilt = std::ldexp(1.0, -54);
	const auto T = exact::Line::from_exact(exact::line_from_dir(250.0, 200.0, tilt, 1.0));
	CHECK(!T->parallel_to(400.0, -500.0, 400.0, 200.0));
	// a wall one ulp off vertical is not parallel to the exact vertical either
	CHECK(!L->parallel_to(400.0, -500.0, 400.0 + std::ldexp(1.0, -44), 200.0));
}
