// Stage 139 (GAME_SPEC 12.1, 12.6, the user): a crossing off its segment on a
// surface no remaining plan entry names is not an event -- not counted, not
// drawn; the walker skips computing it where the beam's exact line proves it
// off by its rigorously bounded float64 shadow (exact::Line::misses_segment).
// (bench/fixtures/stage139_off_segment.json, the oracle's.)
#include "goldens.hpp"
#include "oracle_fixtures.hpp"
#include "rc/exact.hpp"

#include <doctest/doctest.h>

#include <random>

using namespace rc;
using namespace rc::test;

TEST_CASE("off segment: the Stage 139 fixtures match the oracle at f64, Dual and 4 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage139_off_segment.json");
	int fixtures = 0;
	int offseg_events = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1});
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0});
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto rb = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4));
		CHECK_MESSAGE(diff_against_oracle(rb, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(rb, fx["expected"])));
		for (const auto &e : r64) {
			// an off-segment hit is an event only on a plan surface
			if (e.kind == EventKind::Hit && !e.on_segment) {
				bool planned = false;
				for (const ojson &p : fx["plan"]) {
					planned = planned || p["surface_id"].get<int>() == e.surface_id;
				}
				CHECK_MESSAGE(planned, name);
				offseg_events++;
			}
		}
		fixtures++;
	}
	CHECK(fixtures == 8);
	CHECK(offseg_events == 8); // the planned mirror (its entry unmatched: one per lap, 6; matched: 1) and the planned wall (1): plan surfaces only
}

namespace {

// a x + b y - c at (x, y), exactly, from the line's exact coefficients
int exact_sign(const exact::Line3 &L, double x, double y) {
	const BigFloat v = exact::sub(exact::add(exact::mul(L[0], exact::of(x)), exact::mul(L[1], exact::of(y))), L[2]);
	return v.is_zero() ? 0 : v.is_negative() ? -1 : 1;
}

} // namespace

TEST_CASE("off segment: Line::misses_segment never drops a segment the exact line meets (random lines, reflected too)") {
	std::mt19937_64 rng(1039);
	std::uniform_int_distribution<int> coord(-4000, 4000);
	std::uniform_real_distribution<double> u(-1000.0, 1000.0);
	int dropped = 0, checked = 0;
	for (int k = 0; k < 4000; k++) {
		// an exact line through two lattice points, and a reflected copy (bounded shadow)
		const double px = coord(rng) * 0.25, py = coord(rng) * 0.25;
		double qx = coord(rng) * 0.25, qy = coord(rng) * 0.25;
		if (qx == px && qy == py) {
			qx += 1.0;
		}
		const std::shared_ptr<exact::Line> base = exact::Line::from_exact(exact::line_through(px, py, qx, qy));
		const double mx = coord(rng) * 0.5, my = coord(rng) * 0.5;
		const std::shared_ptr<exact::Line> refl = base->reflected(mx, my, mx + coord(rng) * 0.125 + 0.5, my + coord(rng) * 0.125 + 0.25);
		for (const std::shared_ptr<exact::Line> &L : {base, refl}) {
			const std::optional<exact::Line3> E = L->exact();
			REQUIRE(E.has_value());
			// segments: random, and ones with an endpoint on or a hair off the line
			for (int v = 0; v < 3; v++) {
				double sx = u(rng), sy = u(rng), ex = u(rng), ey = u(rng);
				if (v == 1) {
					// the start exactly on the base line (a lattice point of it) when the line is the base
					sx = px;
					sy = py;
				} else if (v == 2) {
					sx = std::nextafter(px, 1e9);
					sy = py;
				}
				const bool miss = L->misses_segment(sx, sy, ex, ey);
				checked++;
				if (miss) {
					dropped++;
					const int a = exact_sign(*E, sx, sy);
					const int b = exact_sign(*E, ex, ey);
					CHECK(a != 0);
					CHECK(b != 0);
					CHECK(a == b);
				}
			}
		}
	}
	CHECK(dropped > checked / 4); // the filter decides most random segments
	CHECK(dropped < checked);
}

TEST_CASE("off segment: Line::misses_circle only when the exact distance exceeds the radius") {
	std::mt19937_64 rng(1040);
	std::uniform_int_distribution<int> coord(-4000, 4000);
	std::uniform_real_distribution<double> u(-1000.0, 1000.0);
	std::uniform_real_distribution<double> rad(1.0, 400.0);
	int missed = 0, checked = 0;
	for (int k = 0; k < 3000; k++) {
		const double px = coord(rng) * 0.25, py = coord(rng) * 0.25;
		double qx = coord(rng) * 0.25, qy = coord(rng) * 0.25;
		if (qx == px && qy == py) {
			qy += 1.0;
		}
		const std::shared_ptr<exact::Line> L = exact::Line::from_exact(exact::line_through(px, py, qx, qy));
		const std::optional<exact::Line3> E = L->exact();
		REQUIRE(E.has_value());
		const double cx = u(rng), cy = u(rng), r = rad(rng);
		checked++;
		if (L->misses_circle(cx, cy, r)) {
			missed++;
			// (a cx + b cy - c)^2 > r^2 (a^2 + b^2), exactly
			const BigFloat d = exact::sub(exact::add(exact::mul((*E)[0], exact::of(cx)), exact::mul((*E)[1], exact::of(cy))), (*E)[2]);
			const BigFloat n2 = exact::add(exact::mul((*E)[0], (*E)[0]), exact::mul((*E)[1], (*E)[1]));
			const BigFloat lhs = exact::mul(d, d);
			const BigFloat rhs = exact::mul(exact::mul(exact::of(r), exact::of(r)), n2);
			CHECK(!exact::sub(lhs, rhs).is_negative());
			CHECK(!exact::sub(lhs, rhs).is_zero());
		}
	}
	CHECK(missed > checked / 4);
	CHECK(missed < checked);
}
