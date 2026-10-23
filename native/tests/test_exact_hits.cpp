// Stage 113 (K11, K12): a projective's hit point at a data point of its
// segment, and the cursor at a projective's departure point. The fixtures
// (bench/fixtures/stage113_exact_hits.json): the arc's endpoint radius (0.6,
// -0.8) is not representable, so c + R u lands off the endpoint by arithmetic
// alone; the exact predicate (Solver::exact_data_hit) says the hit point is
// the endpoint, where the physical hit is blocked on one side only and does
// not fire while the planned walk fires the entry. The cursor on a line
// projective's segment is its own hit point, reached at once on the exit beam.
#include "oracle_fixtures.hpp"

#include <doctest/doctest.h>

using namespace rc;
using namespace rc::test;

TEST_CASE("Stage 113: the exact-hit fixtures match the oracle at f64, Dual, 4 and 64 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage113_exact_hits.json");
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
		const auto rb64 = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 64));
		CHECK_MESSAGE(diff_against_oracle(rb64, fx["expected"]) == "", (name + " big64: " + diff_against_oracle(rb64, fx["expected"])));
		fixtures++;
	}
	CHECK(fixtures == 10);
}

TEST_CASE("Stage 113: the exact image runs through a projective whose hit point is a data point") {
	// the arc of the fixtures: the right half of the circle centred (700, 500),
	// r 250; the cursor on the radial through the endpoint (850, 300)
	const ojson fxs = load_json("bench/fixtures/stage113_exact_hits.json");
	const ojson &fx = fxs["fixtures"][0];
	const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
	Scene<F64> scene = Scene<F64>::from_defs(defs, F64{1});
	Solver<F64> solver(scene, nullptr);
	const std::vector<PlanItem> plan = plan_from_json(fx["plan"]);
	const std::optional<exact::Point3> X = solver.exact_image_h(CN<F64>{F64{940.0}, F64{180.0}}, plan, 0, Flags());
	REQUIRE(X.has_value());
	CHECK(exact::sub((*X)[0], exact::mul(exact::of(850.0), (*X)[2])).is_zero());
	CHECK(exact::sub((*X)[1], exact::mul(exact::of(300.0), (*X)[2])).is_zero());
	// a cursor just off the radial: the hit point is not exact data
	CHECK(!solver.exact_image_h(CN<F64>{F64{940.0}, F64{183.0}}, plan, 0, Flags()).has_value());
}
