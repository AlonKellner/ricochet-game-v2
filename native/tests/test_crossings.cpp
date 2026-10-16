// Stage 108: a plan entry may name a piece of its surface (GAME_SPEC 9.7): a
// span on the surface's carrier. The native solver must reproduce the
// oracle's fixtures (bench/fixtures/stage108_piece_plans.json) at every tier.
#include "oracle_fixtures.hpp"
#include "rc/tracer.hpp"

#include <doctest/doctest.h>

using namespace rc;
using namespace rc::test;

TEST_CASE("crossings: the Stage 108 piece-plan fixtures match the oracle at f64, Dual, 4 and 64 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage108_piece_plans.json");
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
	CHECK(fixtures == 9);
}

TEST_CASE("crossings: the tracer's cache tells plans apart by their pieces") {
	SurfaceDef m;
	m.id = 1;
	m.line = true;
	m.sx = 500;
	m.sy = -300;
	m.ex = 500;
	m.ey = 300;
	m.vx = 500;
	m.vy = 0;
	SideDef refl;
	refl.type = EffectType::Reflection;
	refl.interactive = true;
	m.left = refl;
	m.right = refl;
	Tracer tr;
	tr.set_defs({m});
	PlanItem upper{1, false, std::nullopt, std::array<double, 6>{500, -300, 500, 0, 500, -150}};
	PlanItem lower{1, false, std::nullopt, std::array<double, 6>{500, 0, 500, 300, 500, 150}};
	const auto a = tr.trace_pair(V2::of(100, -200), V2::of(200, -100), {upper}, Flags());
	const auto b = tr.trace_pair(V2::of(100, -200), V2::of(200, -100), {lower}, Flags());
	REQUIRE(a);
	REQUIRE(b);
	CHECK(a->planned.cursor_index >= 0);
	CHECK(a->planned.steps.front().hit_on_segment);
	CHECK(!b->planned.steps.front().hit_on_segment);   // not the cached trace of the upper piece
}
