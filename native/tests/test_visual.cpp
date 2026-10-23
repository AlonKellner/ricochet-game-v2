// Stage 97: the visual / testing kernels against closed forms. Bit-identity
// with the GDScript originals is the GUT differential test's job
// (tests/native/test_diff_visual.gd); here the semantics are pinned.
#include "rc/visual.hpp"

#include <doctest/doctest.h>

using namespace rc;
using namespace rc::arc;

TEST_CASE("visual: carriers from circles and three points") {
	const GeneralizedCircle g = from_circle(V2{100.0f, 50.0f}, 20.0);
	CHECK(!g.is_line());
	CHECK(center_of(g) == V2{100.0f, 50.0f});
	CHECK(g.radius() == doctest::Approx(20.0));
	const GeneralizedCircle d = derive_carrier(V2{10.0f, 0.0f}, V2{-10.0f, 0.0f}, V2{0.0f, 10.0f});
	CHECK(!d.is_line());
	CHECK(center_of(d) == V2{0.0f, 0.0f});
	CHECK(d.radius() == doctest::Approx(10.0));
	CHECK(same_circle(d, d));
	const GeneralizedCircle l = derive_carrier(V2{0.0f, 0.0f}, V2{10.0f, 0.0f}, V2{5.0f, 0.0f});
	CHECK(l.is_line());
	CHECK(project_onto_carrier(V2{5.0f, 3.0f}, l) == V2{5.0f, 0.0f});
	CHECK(project_onto_carrier(V2{0.0f, 0.0f}, g).distance_to(V2{100.0f, 50.0f}) == doctest::Approx(20.0).epsilon(1e-5));
}

TEST_CASE("visual: segment sides, winding and click distance") {
	const SegGeom line = SegGeom::make(V2{0.0f, 0.0f}, V2{10.0f, 0.0f}, V2{5.0f, 0.0f});
	CHECK(line.is_line());
	CHECK(line.distance_to(V2{5.0f, 3.0f}) == doctest::Approx(3.0));
	CHECK(line.distance_to(V2{20.0f, 0.0f}) == doctest::Approx(10.0));
	// the two sides are distinct and swap with the traversal direction
	const Side above = line.determine_side(V2{5.0f, -1.0f});
	const Side below = line.determine_side(V2{5.0f, 1.0f});
	CHECK(above != below);
	const SegGeom back = SegGeom::make(V2{10.0f, 0.0f}, V2{0.0f, 0.0f}, V2{5.0f, 0.0f});
	CHECK(back.determine_side(V2{5.0f, -1.0f}) == below);
	// an arc: points on the carrier at distance 0, the far side measures to the nearer endpoint
	const SegGeom arcseg = SegGeom::make(V2{10.0f, 0.0f}, V2{-10.0f, 0.0f}, V2{0.0f, 10.0f});
	CHECK(!arcseg.is_line());
	CHECK(arcseg.distance_to(V2{0.0f, 10.0f}) == doctest::Approx(0.0));
	CHECK(arcseg.distance_to(V2{0.0f, -12.0f}) == doctest::Approx(V2{0.0f, -12.0f}.distance_to(V2{10.0f, 0.0f})));
	const SegGeom full = SegGeom::full_from_carrier(from_circle(V2{0.0f, 0.0f}, 10.0));
	CHECK(full.full);
	CHECK(full.distance_to(V2{0.0f, -12.0f}) == doctest::Approx(2.0));
}

TEST_CASE("visual: arc parameters and clipping") {
	const ArcParams p = arc_params(V2{10.0f, 0.0f}, V2{7.0710678f, 7.0710678f}, V2{0.0f, 10.0f});
	CHECK(!p.line);
	CHECK(p.span == doctest::Approx(TAU / 4.0).epsilon(1e-5));
	CHECK(!p.clockwise);
	CHECK(p.point_count == 64);
	CHECK(arc_params(V2{0.0f, 0.0f}, V2{5.0f, 0.0f}, V2{10.0f, 0.0f}).line);
	const Rect2f bounds = Rect2f::of(0, 0, 1920, 1080);
	CHECK(clip_to_bounds(V2{100.0f, 100.0f}, V2{1.0f, 0.0f}, bounds) == V2{1920.0f, 100.0f});
	CHECK(clip_to_bounds(V2{100.0f, 100.0f}, V2{0.0f, -1.0f}, bounds) == V2{100.0f, 0.0f});
	// no edge ahead (a zero direction): 100 units along
	CHECK(clip_to_bounds(V2{100.0f, 100.0f}, V2{0.0f, 0.0f}, bounds) == V2{100.0f, 100.0f});
	CHECK(fposmod(-1.0, TAU) == doctest::Approx(TAU - 1.0));
	CHECK(V2{1.0f, 1.0f}.angle() == std::atan2(1.0f, 1.0f));
}

TEST_CASE("visual: arrow advance along a two-step path") {
	std::vector<StepGeom> steps(2);
	steps[0].start = V2{0.0f, 0.0f};
	steps[0].end = V2{100.0f, 0.0f};
	steps[0].via = V2{50.0f, 0.0f};
	steps[1].start = V2{100.0f, 0.0f};
	steps[1].end = V2{100.0f, 100.0f};
	steps[1].via = V2{100.0f, 50.0f};
	const Rect2f bounds = Rect2f::of(0, 0, 1920, 1080);
	AdvanceResult r = advance(steps, 0, 0.0, V2{}, 150.0, bounds);
	CHECK(r.step_index == 1);
	CHECK(r.progress == doctest::Approx(50.0));
	CHECK(r.position == V2{100.0f, 50.0f});
	CHECK(r.direction == V2{0.0f, 1.0f});
	CHECK(!r.finished);
	r = advance(steps, r.step_index, r.progress, r.position, 1000.0, bounds);
	CHECK(r.finished);
	CHECK(r.position == V2{100.0f, 100.0f});
}

// Stage 136 (the user's F12, open_room_playable): a wrap whose return comes in
// through off-segment crossings far outside the level -- the arrow skips them and
// re-enters where the step crossing into the bounds enters (the bottom edge), not
// at that step's end (it appeared at the floor's crossing)
TEST_CASE("visual: the arrow re-enters the bounds where a step from outside enters them") {
	const Rect2f bounds = Rect2f::of(0, 0, 1920, 1080);
	auto step = [](V2 a, V2 b) {
		StepGeom s;
		s.start = a;
		s.end = b;
		s.via = (a + b) / 2.0f;
		return s;
	};
	const std::vector<StepGeom> steps = {
		step(V2{1481.0f, 240.0f}, V2{1481.0f, 0.0f}),        // the wrap's way out (clipped at the top)
		step(V2{558.0f, 56479.0f}, V2{560.0f, 56379.0f}),    // its way back, outside
		step(V2{560.0f, 56379.0f}, V2{1200.0f, 17378.0f}),   // an off-segment crossing, outside
		step(V2{1200.0f, 17378.0f}, V2{1471.0f, 840.0f}),    // enters through the bottom edge
		step(V2{1471.0f, 840.0f}, V2{1473.0f, 727.0f}),
	};
	AdvanceResult r = advance(steps, 0, 0.0, steps[0].start, 241.0, bounds);
	REQUIRE(r.step_index == 3);
	CHECK(r.position.y == doctest::Approx(1080.0).epsilon(1e-5));
	CHECK(r.position.x > 1200.0f);
	CHECK(r.position.x < 1471.0f);
	CHECK(r.progress == doctest::Approx(double(steps[3].start.distance_to(r.position))).epsilon(1e-6));
	// and flies on along that step to its end
	r = advance(steps, r.step_index, r.progress, r.position, 100.0, bounds);
	CHECK(r.step_index == 3);
	CHECK(r.position.y == doctest::Approx(980.0).epsilon(1e-3));
}

TEST_CASE("visual: display pieces of a path leaving the bounds") {
	std::vector<StepGeom> steps(2);
	steps[0].start = V2{100.0f, 100.0f};
	steps[0].end = V2{200.0f, 100.0f};
	steps[0].via = V2{150.0f, 100.0f};
	steps[0].has_hit = true;
	steps[1].start = V2{200.0f, 100.0f};
	steps[1].end = V2::inf();
	steps[1].via = V2::inf();
	steps[1].has_ray = true;
	steps[1].ray_dir = V2{1.0f, 0.0f};
	const DisplayPlan plan = prepare_for_display(steps, -1, Rect2f::of(0, 0, 1920, 1080), [](int) { return std::nullopt; });
	REQUIRE(plan.pieces.size() == 2);
	CHECK(plan.pieces[0].kind == Piece::Identity);
	CHECK(plan.pieces[1].kind == Piece::Clipped);
	CHECK(plan.pieces[1].end == V2{1920.0f, 100.0f});
	CHECK(plan.pieces[1].via == V2{1060.0f, 100.0f});
	CHECK(!plan.pieces[1].keep_hit);
}

TEST_CASE("visual: surface samples") {
	const LineSample ls = line_sample(V2{0.0f, 0.0f}, V2{10.0f, 0.0f}, 0.5);
	CHECK(ls.position == V2{5.0f, 0.0f});
	CHECK(ls.normal == V2{-0.0f, 1.0f});
	const ArcSample as = arc_sample(V2{0.0f, 0.0f}, 10.0, 0.0, TAU / 2.0, false, 0.5);
	CHECK(as.position.distance_to(V2{0.0f, 10.0f}) < 1e-4f);
	const auto ch = chevron_vertices(V2{0.0f, 0.0f}, V2{1.0f, 0.0f}, 8.0);
	CHECK(ch[0] == V2{0.0f, 0.0f});
	CHECK(ch[1].x == doctest::Approx(-8.0 * std::cos(0.5)).epsilon(1e-5));
	CHECK(ch[1].y == doctest::Approx(-8.0 * std::sin(0.5)).epsilon(1e-5));
}
