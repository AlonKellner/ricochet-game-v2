// Stage 122: the presentation layer (rcg/present.hpp): a region sampled along
// rays from its apex is the region; a morph starts and ends on the two
// regions, its ends slide along the surfaces they share; a region that does
// not match collapses into the player and the new one grows; the presenter
// with no animation is the session's frame.
#include "rcg/level.hpp"
#include "rcg/present.hpp"
#include "rcg/session.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>

namespace {

namespace rf = rcg::region_field;

const std::filesystem::path ROOT = RC_REPO_ROOT;

rcg::Level level(const char *name) { return rcg::load_level((ROOT / "data" / "levels" / name).string()); }

double area(const std::vector<rc::V2> &pts, const std::vector<int> &idx) {
	double a = 0.0;
	for (size_t t = 0; t + 2 < idx.size(); t += 3) {
		const rc::V2 &p = pts[size_t(idx[t])], &q = pts[size_t(idx[t + 1])], &r = pts[size_t(idx[t + 2])];
		a += std::abs((double(q.x) - double(p.x)) * (double(r.y) - double(p.y)) - (double(q.y) - double(p.y)) * (double(r.x) - double(p.x))) / 2.0;
	}
	return a;
}

double area(const rc::vis::Fill &f) {
	std::vector<rc::V2> pts;
	std::vector<int> idx;
	for (const rc::vis::Pt &p : f.points) {
		pts.push_back(rc::V2::of(p.x, p.y));
	}
	for (uint32_t i : f.indices) {
		idx.push_back(int(i));
	}
	return area(pts, idx);
}

double area(const rf::Field &f) {
	std::vector<rc::V2> pts;
	std::vector<int> idx;
	rf::fill_of(f, pts, idx);
	return area(pts, idx);
}

std::vector<rc::V2> plannable(const rcg::Session &s) {
	std::vector<rc::V2> out;
	for (const rcg::BuiltSurface &b : s.world().surfaces()) {
		if (b.label && b.left && b.left->interactive && !b.pre_transform && b.seg.is_line()) {
			const rc::V2 d = (b.seg.e - b.seg.s).normalized();
			out.push_back(b.seg.v + rc::V2{-d.y, d.x} * 6.0f);
		}
	}
	return out;
}

} // namespace

TEST_CASE("present: a region sampled along rays from its apex is the region") {
	for (const char *name : {"test_levels/crossings.json", "test_levels/hit_effects.json", "main.json", "playground/portals.json"}) {
		CAPTURE(name);
		rcg::Session s(level(name));
		s.settle();
		REQUIRE(s.region());
		const rf::Field f = rf::field_of(*s.region());
		REQUIRE(f.ok);
		CHECK_FALSE(f.empty);
		const double exact = area(rc::vis::fill_triangles(*s.region()));
		CHECK(area(f) == doctest::Approx(exact).epsilon(0.005));
	}
	// behind a planned mirror: the apex is the player's image, the spans start on the mirror
	rcg::Session s(level("test_levels/crossings.json"));
	s.set_cursor(plannable(s).at(0));
	s.settle();
	s.click(false);
	s.settle();
	REQUIRE(s.region());
	const rf::Field f = rf::field_of(*s.region());
	REQUIRE(f.ok);
	CHECK(area(f) == doctest::Approx(area(rc::vis::fill_triangles(*s.region()))).epsilon(0.005));
	int on_carrier = 0;
	for (const rf::Sample &sm : f.samples) {
		for (const rf::Span &sp : sm.spans) {
			on_carrier += sp.near.kind == rf::End::Carrier ? 1 : 0;
		}
	}
	CHECK(on_carrier > 0);
}

TEST_CASE("present: a morph starts on the old region, ends on the new, and its ends slide along the surfaces they share") {
	// hit_effects: circle carriers among the walls
	rcg::Session s(level("test_levels/hit_effects.json"));
	s.settle();
	const rf::Field a = rf::field_of(*s.region());
	s.teleport(s.player() + rc::V2{24, 10});
	s.settle();
	const rf::Field b = rf::field_of(*s.region());
	REQUIRE(a.ok);
	REQUIRE(b.ok);
	CHECK(area(rf::morph(a, b, 0.0)) == doctest::Approx(area(a)).epsilon(0.002));
	CHECK(area(rf::morph(a, b, 1.0)) == doctest::Approx(area(b)).epsilon(0.002));
	// halfway, an end found on one circle before and after is on that circle
	const rf::Field m = rf::morph(a, b, 0.5);
	int circle_ends = 0, slid = 0;
	for (const rf::Sample &sm : m.samples) {
		const std::vector<rf::Span> A = rf::spans_at(a, sm.phi), B = rf::spans_at(b, sm.phi);
		if (A.size() != 1 || B.size() != 1 || sm.spans.size() != 1) {
			continue;
		}
		const rf::End &ea = A[0].far, &eb = B[0].far, &em = sm.spans[0].far;
		if (ea.kind == rf::End::Carrier && eb.kind == rf::End::Carrier && ea.surface == eb.surface && ea.circle) {
			circle_ends++;
			const double r = std::hypot(em.p.x - ea.center.x, em.p.y - ea.center.y);
			slid += std::abs(r - ea.radius) < 0.5 ? 1 : 0;
		}
	}
	CHECK(circle_ends > 0);
	CHECK(slid == circle_ends);
}

TEST_CASE("present: a span with no partner grows out of its near end") {
	rcg::Session s(level("test_levels/crossings.json"));
	s.settle();
	const rf::Field b = rf::field_of(*s.region());
	const rf::Field none{true, true, {}, {}};
	CHECK(area(rf::morph(none, b, 0.0)) == doctest::Approx(0.0));
	const double half = area(rf::morph(none, b, 0.5)), full = area(rf::morph(none, b, 1.0));
	CHECK(half > 0.0);
	CHECK(half < full);
	CHECK(full == doctest::Approx(area(b)).epsilon(0.002));
	CHECK(area(rf::morph(b, none, 1.0)) == doctest::Approx(0.0));
}

TEST_CASE("present: a mesh clipped to a disk") {
	const std::vector<rc::V2> pts{rc::V2{0, 0}, rc::V2{100, 0}, rc::V2{0, 100}};
	const std::vector<int> idx{0, 1, 2};
	std::vector<rc::V2> op;
	std::vector<int> oi;
	rf::clip_disk(pts, idx, rf::Pt{0, 0}, 1000.0, op, oi);
	CHECK(area(op, oi) == doctest::Approx(5000.0));
	op.clear();
	oi.clear();
	rf::clip_disk(pts, idx, rf::Pt{0, 0}, 50.0, op, oi);
	CHECK(area(op, oi) == doctest::Approx(3.14159265 * 2500.0 / 4.0).epsilon(0.01)); // a quarter disk
	op.clear();
	oi.clear();
	rf::clip_disk(pts, idx, rf::Pt{0, 0}, 0.0, op, oi);
	CHECK(oi.empty());
}

TEST_CASE("present: without animation the presenter's frame is the session's") {
	rcg::Session s(level("test_levels/crossings.json"));
	rcg::PresentConfig cfg;
	cfg.region_seconds = 0.0;
	cfg.interpolate = false;
	rcg::Presenter p(cfg);
	for (int k = 0; k < 6; k++) {
		s.teleport(s.player() + rc::V2{8, 0});
		s.settle();
		p.advance(1.0 / 60.0);
		const std::vector<rcg::render::FrameNode> want = s.draw_frame();
		const std::vector<rcg::render::FrameNode> got = p.frame(s.draw_frame(), s);
		REQUIRE(got.size() == want.size());
		for (size_t i = 0; i < got.size(); i++) {
			CHECK(got[i].key == want[i].key);
			CHECK(got[i].version == want[i].version);
			CHECK(got[i].position == want[i].position);
		}
	}
}

TEST_CASE("present: a region's change morphs for its duration, then shows the region") {
	rcg::Session s(level("test_levels/crossings.json"));
	rcg::Presenter p;
	s.settle();
	p.frame(s.draw_frame(), s);
	auto vis = [](const std::vector<rcg::render::FrameNode> &nodes) {
		for (const rcg::render::FrameNode &n : nodes) {
			if (n.key.kind == rcg::render::NodeKind::Visibility) {
				return n;
			}
		}
		FAIL("no region node");
		return rcg::render::FrameNode{};
	};
	// the player moves: matched, a field morph
	s.teleport(s.player() + rc::V2{20, 0});
	s.settle();
	p.advance(0.02);
	CHECK(vis(p.frame(s.draw_frame(), s)).version != vis(s.draw_frame()).version);
	CHECK(p.animating());
	p.advance(0.1);
	CHECK(vis(p.frame(s.draw_frame(), s)).version == vis(s.draw_frame()).version);
	CHECK_FALSE(p.animating());
	// a planned mirror: the apex jumps, the old region collapses into the player, then the new grows
	s.set_cursor(plannable(s).at(0));
	s.settle();
	s.click(false);
	s.settle();
	std::vector<double> areas;
	for (int k = 0; k < 7; k++) {
		const rcg::render::FrameNode n = vis(p.frame(s.draw_frame(), s));
		double a = 0.0;
		for (const rcg::render::Call &c : *n.calls) {
			if (c.kind == rcg::render::Kind::Triangles) {
				a += area(c.pts, c.indices);
			}
		}
		areas.push_back(a);
		p.advance(0.016);
	}
	CHECK(areas[1] < areas[0]); // collapsing
	CHECK(areas[2] < areas[1]);
	CHECK(areas[4] > areas[3]); // growing
	CHECK(areas[6] > areas[4]);
	p.advance(0.1);
	CHECK(vis(p.frame(s.draw_frame(), s)).version == vis(s.draw_frame()).version);
	// a region switched off collapses; no frame has a mesh call without triangles (a host refuses one)
	s.key(rcg::Action::ToggleVisibility);
	s.settle();
	for (int k = 0; k < 8; k++) {
		for (const rcg::render::Call &c : *vis(p.frame(s.draw_frame(), s)).calls) {
			if (c.kind == rcg::render::Kind::Triangles) {
				CHECK_FALSE(c.indices.empty());
				CHECK_FALSE(c.pts.empty());
			}
		}
		p.advance(0.016);
	}
}

TEST_CASE("present: the player is drawn between its last two physics ticks") {
	rcg::Session s(level("main.json"));
	rcg::Presenter p;
	rcg::kinematics::Input in;
	in.axis_x = 1.0f;
	s.advance(rcg::kinematics::STEP * 1.5, in); // one tick, half of the next
	const rc::V2 drawn = p.player(s);
	CHECK(s.tick_alpha() == doctest::Approx(0.5));
	CHECK(double(drawn.x) == doctest::Approx((double(s.tick_from().x) + double(s.player().x)) / 2.0));
	CHECK(drawn.x < s.player().x);
}
