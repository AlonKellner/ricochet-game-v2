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

namespace {
// (a pass that writes a flag on one side: Stage 134 computes no pure pass-through;
// Stage 139: crossed on its segment from the side that passes -- an event that
// fires nothing, no shared-point stop (Stage 138) -- since a crossing off the
// segment is no event)
SurfaceDef pass_line(int id, double sx, double sy, double ex, double ey, bool states_left) {
	SurfaceDef d;
	d.id = id;
	d.line = true;
	d.sx = sx;
	d.sy = sy;
	d.ex = ex;
	d.ey = ey;
	d.vx = (sx + ex) / 2;
	d.vy = (sy + ey) / 2;
	d.left = SideDef{};
	d.right = SideDef{};
	(states_left ? d.left : d.right)->states = {StateChangeDef{"w", false, true}};
	return d;
}

template <Num N>
std::vector<int> first_hits(const std::vector<SurfaceDef> &defs, const N &like) {
	Scene<N> sc = Scene<N>::from_defs(defs, like);
	Solver<N> sv(sc, nullptr);
	const Walk<N> w = sv.run_direction(CN<N>{like.like(310.37), like.like(211.9)}, CN<N>{like.like(1.0), like.like(0.2117)}, {},
		std::nullopt, Mode::Physical, MAX_HITS, Flags());
	std::vector<int> ids;
	std::optional<N> t;
	for (const Event<N> &e : w.events) {
		if (e.kind == EventKind::Hit && ids.size() < 3) {
			if (t) {
				CHECK(e.t_to->cmp(*t) == 0); // one parameter: an exact tie
			}
			t = e.t_to;
			ids.push_back(e.surface_id);
		}
	}
	return ids;
}
} // namespace

TEST_CASE("K6: surfaces on one carrier share their crossings and tie by id, at every tier") {
	// the beam from (310.37, 211.9) along (1, 0.2117) crosses x = 1100 at y = 379.06,
	// inside all three spans; the states lie on the side it does not come from
	const Seg<F64> probe{0, SegKind::Line, false, CN<F64>{F64{1100}, F64{350}}, CN<F64>{F64{1100}, F64{450}}, std::nullopt, std::nullopt, std::nullopt};
	const bool from_left = geom::side_left(probe, CN<F64>{F64{1100}, F64{379.06}}, CN<F64>{F64{1}, F64{0.2117}});
	const std::vector<SurfaceDef> defs{pass_line(5, 1100, 350, 1100, 450, !from_left), pass_line(3, 1100, 300, 1100, 760, !from_left),
		pass_line(9, 1100, -50, 1100, 400, !from_left)};
	const Scene<F64> sc = Scene<F64>::from_defs(defs, F64{1});
	for (const auto &s : sc.surfaces) {
		CHECK(s->carrier_rep == 5);
	}
	CHECK(first_hits<F64>(defs, F64{1}) == std::vector<int>{3, 5, 9});
	CHECK(first_hits<Dual>(defs, Dual{1, 0}) == std::vector<int>{3, 5, 9});
	CHECK(first_hits<BigFloat>(defs, BigFloat::from_int(1, 4)) == std::vector<int>{3, 5, 9});
}

// Stage 133 (GAME_SPEC 9.7, the user; F23): a piece's end stored as the float32
// rounding of two carriers' crossing is read as the crossing, at every tier
TEST_CASE("crossings: a piece's end that rounds a crossing is the crossing (F23)") {
	const ojson d = load_json("bench/scenes/test_levels__crossings.surfaces.json");
	const std::vector<SurfaceDef> defs = defs_from_json(d["surfaces"]);
	PlanItem it;
	it.surface_id = 20;
	it.left = true;
	it.span = std::array<double, 6>{1400.0, 587.550048828125, 1400.0, 712.449951171875, 1530.0, 650.0};
	auto check = [&](auto one) {
		using N = decltype(one);
		const Scene<N> scene = Scene<N>::from_defs(defs, one);
		Solver<N> solver(scene, nullptr);
		const auto spans = solver.plan_piece_spans({it});
		REQUIRE(spans.size() == 1);
		REQUIRE(spans[0]);
		for (const CN<N> &x : {spans[0]->s, spans[0]->e}) {
			CHECK(x.fy() != 587.550048828125);
			CHECK(x.fy() != 712.449951171875);
			for (int sid : {19, 20}) {
				const Surf<N> *s = scene.find(sid);
				// on both circles, to the tier's precision
				CHECK(std::abs(x.sub(s->seg.circ->c).abs2().sub(s->seg.circ->r2).to_float()) < 1e-6);
			}
		}
	};
	check(F64{1});
	check(Dual{1, 0});
	check(BigFloat::from_int(1, 4));
}
