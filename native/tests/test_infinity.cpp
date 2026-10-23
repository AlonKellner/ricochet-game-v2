// Stage 88c: the `infinity` event and the infinity plan entry. The native
// solver reproduces the oracle's Stage 88 fixtures (bench/fixtures/
// stage88_infinity.json) at every tier (Stage 136: infinity is always on).
#include "goldens.hpp"
#include "oracle_fixtures.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace rc;
using namespace rc::test;

TEST_CASE("infinity: the Stage 88 fixtures match the oracle at f64, Dual and 4 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage88_infinity.json");
	int fixtures = 0;
	int infinity_events = 0;
	int consumed = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1});
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0});
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto rb = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4));
		CHECK_MESSAGE(diff_against_oracle(rb, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(rb, fx["expected"])));
		for (const Event<F64> &e : r64) {
			if (e.kind == EventKind::Infinity) {
				infinity_events++;
				CHECK(e.surface_id == INFINITY_ID);
				// the real infinity has no point and no t_to; a finite image of it has both
				CHECK(e.point.has_value() == e.t_to.has_value());
				if (e.applied) {
					consumed++;
				}
			}
			if (e.kind == EventKind::Max && e.surface_id == INFINITY_ID) {
				CHECK(!e.point);
			}
		}
		fixtures++;
	}
	CHECK(fixtures == 26);
	CHECK(infinity_events > 0);
	CHECK(consumed > 0);
}

TEST_CASE("infinity: the Stage 101 fixtures (infinity as a point, the pre-transform) match the oracle at f64, Dual and 4 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage101_infinity_point.json");
	int fixtures = 0;
	int with_point = 0;
	int pre_scenes = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		for (const SurfaceDef &d : defs) {
			pre_scenes += d.pre_transform ? 1 : 0;
		}
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1});
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0});
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto rb = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4));
		CHECK_MESSAGE(diff_against_oracle(rb, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(rb, fx["expected"])));
		for (const Event<F64> &e : r64) {
			if ((e.kind == EventKind::Infinity || e.kind == EventKind::Max) && e.surface_id == INFINITY_ID && e.point) {
				with_point++;
				CHECK(e.t_to.has_value());
			}
		}
		fixtures++;
	}
	CHECK(fixtures == int(fxs["fixtures"].size()));
	CHECK(with_point > 0);
	CHECK(pre_scenes > 0);
}

TEST_CASE("Stage 105: the tangency-at-infinity fixtures match the oracle at f64, Dual, 4 and 64 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage105_tangent_at_infinity.json");
	int fixtures = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1});
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0});
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto r4 = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4));
		CHECK_MESSAGE(diff_against_oracle(r4, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(r4, fx["expected"])));
		const auto r64l = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 64));
		CHECK_MESSAGE(diff_against_oracle(r64l, fx["expected"]) == "", (name + " big64: " + diff_against_oracle(r64l, fx["expected"])));
		fixtures++;
	}
	CHECK(fixtures == int(fxs["fixtures"].size()));
	CHECK(fixtures >= 14);
}

TEST_CASE("Stage 106: the pop-departure fixtures match the oracle at f64, Dual, 4 and 64 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage106_pop_departure.json");
	int fixtures = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1});
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0});
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto r4 = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4));
		CHECK_MESSAGE(diff_against_oracle(r4, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(r4, fx["expected"])));
		const auto r64l = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 64));
		CHECK_MESSAGE(diff_against_oracle(r64l, fx["expected"]) == "", (name + " big64: " + diff_against_oracle(r64l, fx["expected"])));
		fixtures++;
	}
	CHECK(fixtures == int(fxs["fixtures"].size()));
	CHECK(fixtures >= 11);
}

TEST_CASE("Stage 111: the realization-by-entry fixtures match the oracle at f64, Dual, 4 and 64 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage111_orientation.json");
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
	CHECK(fixtures == 32);
}

// Stage 132 (GAME_SPEC 4.2, 12.6): the ray's origin as a plan entry -- consumed by
// its passage in both walks; the realization by the parity of the infinity and
// origin entries (bench/fixtures/stage132_origin_entry.json, the oracle's)
TEST_CASE("origin entry: the Stage 132 fixtures match the oracle at f64, Dual and 4 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage132_origin_entry.json");
	int fixtures = 0;
	int consumed = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1});
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0});
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto rb = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4));
		CHECK_MESSAGE(diff_against_oracle(rb, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(rb, fx["expected"])));
		for (const Event<F64> &e : r64) {
			if (e.kind == EventKind::Origin && e.applied) {
				consumed++;
			}
		}
		fixtures++;
	}
	CHECK(fixtures == 14);
	CHECK(consumed == 8);
}
