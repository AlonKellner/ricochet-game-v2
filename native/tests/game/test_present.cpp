// Stage 122: the presentation layer (rcg/present.hpp): a region's outline is
// its fill; the diff of two outlines starts and ends on them, pairs the
// corners they share, and every frame between is simple polygons of straight
// edges (a planned inversion's regions too: the flicker the user saw); the
// shadows follow the drawn player before the next region; the presenter with
// no animation is the session's frame.
#include "rcg/level.hpp"
#include "rcg/present.hpp"
#include "rcg/session.hpp"

#include <doctest/doctest.h>
#include <set>

#include <cmath>
#include <filesystem>
#include <functional>
#include <optional>

namespace {

namespace sm = rcg::shape_morph;

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

double area(const sm::Shape &s) {
	std::vector<rc::V2> pts;
	std::vector<int> idx;
	sm::fill_of(s, pts, idx);
	return area(pts, idx);
}

std::optional<rcg::render::FrameNode> vis_node(const std::vector<rcg::render::FrameNode> &nodes) {
	for (const rcg::render::FrameNode &n : nodes) {
		if (n.key.kind == rcg::render::NodeKind::Visibility) {
			return n; // (a copy: the list is a temporary)
		}
	}
	return std::nullopt;
}

const rcg::render::Call *mesh(const rcg::render::FrameNode &n) {
	for (const rcg::render::Call &c : *n.calls) {
		if (c.kind == rcg::render::Kind::Triangles) {
			return &c;
		}
	}
	return nullptr;
}

double mesh_area(const rcg::render::FrameNode &n) {
	const rcg::render::Call *m = mesh(n);
	return m ? area(m->pts, m->indices) : 0.0;
}

// the session's region as the presenter outlines it
sm::Shape outline(const rcg::Session &s) {
	return s.region() ? sm::shape_of(s.region_outline(), sm::carriers_of(*s.region(), &s.world())) : sm::Shape{};
}

// plans the side of the nth surface with a label, 6 px off its middle
void plan_side(rcg::Session &s, const char *label, int nth, int side) {
	int k = 0;
	for (const rcg::BuiltSurface &b : s.world().surfaces()) {
		if (b.label && *b.label == label && k++ == nth) {
			const rc::V2 d = (b.seg.e - b.seg.s).normalized();
			s.set_cursor(b.seg.v + (side == 0 ? rc::V2{d.y, -d.x} : rc::V2{-d.y, d.x}) * 6.0f);
			s.settle();
			s.click(false);
			s.settle();
			return;
		}
	}
	FAIL("no such surface");
}

bool proper_cross(const sm::Pt &a, const sm::Pt &b, const sm::Pt &c, const sm::Pt &d) {
	auto orient = [](const sm::Pt &p, const sm::Pt &q, const sm::Pt &r) { return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x); };
	const double o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
	const double eps = 1e-6;
	return ((o1 > eps && o2 < -eps) || (o1 < -eps && o2 > eps)) && ((o3 > eps && o4 < -eps) || (o3 < -eps && o4 > eps));
}

// a ring's edges that cross each other properly
int self_crossings(const sm::Ring &r) {
	int n = 0;
	for (size_t i = 0; i < r.size(); i++) {
		for (size_t j = i + 2; j < r.size(); j++) {
			if (i == 0 && j == r.size() - 1) {
				continue;
			}
			n += proper_cross(r[i].p, r[(i + 1) % r.size()].p, r[j].p, r[(j + 1) % r.size()].p) ? 1 : 0;
		}
	}
	return n;
}

// regions a few px apart as the player walks (each settled)
std::vector<sm::Shape> walk(rcg::Session &s, rc::V2 step, int n) {
	std::vector<sm::Shape> out{outline(s)};
	for (int k = 0; k < n; k++) {
		s.teleport(s.player() + step);
		s.settle();
		out.push_back(outline(s));
	}
	return out;
}

} // namespace

TEST_CASE("present: a region's outline is its fill") {
	for (const char *name : {"test_levels/crossings.json", "test_levels/hit_effects.json", "main.json", "playground/portals.json"}) {
		CAPTURE(name);
		rcg::Session s(level(name));
		s.settle();
		const sm::Shape o = outline(s);
		CHECK_FALSE(o.rings.empty());
		CHECK(area(o) == doctest::Approx(mesh_area(*vis_node(s.draw_frame()))).epsilon(0.01));
		size_t tagged = 0, all = 0;
		for (const sm::Ring &r : o.rings) {
			for (const sm::Vertex &v : r) {
				tagged += v.tag != 0 ? 1 : 0;
				all++;
			}
			CHECK(self_crossings(r) == 0);
		}
		CHECK(tagged > all / 2); // (most corners lie on surfaces)
	}
	// a planned inversion: a pencil of circles, outlined all the same
	rcg::Session s(level("playground/inversion_grid.json"));
	plan_side(s, "FullInversion", 3, 0);
	REQUIRE(s.plan().size() == 1);
	const sm::Shape o = outline(s);
	CHECK_FALSE(o.rings.empty());
	CHECK(area(o) == doctest::Approx(mesh_area(*vis_node(s.draw_frame()))).epsilon(0.01));
}

TEST_CASE("present: the diff of two regions starts and ends on them, pairs their corners, and stays simple polygons") {
	struct Case {
		const char *name;
		const char *label;
		rc::V2 step;
	};
	for (const Case &c : {Case{"test_levels/crossings.json", nullptr, rc::V2{9, 4}}, Case{"main.json", nullptr, rc::V2{-12, 6}},
			 Case{"test_levels/hit_effects.json", nullptr, rc::V2{8, -5}}, Case{"playground/inversion_grid.json", "FullInversion", rc::V2{0, 7}}}) {
		CAPTURE(std::string(c.name));
		rcg::Session s(level(c.name));
		s.settle();
		if (c.label) {
			plan_side(s, c.label, 3, 0);
		}
		const std::vector<sm::Shape> regions = walk(s, c.step, 4);
		for (size_t k = 0; k + 1 < regions.size(); k++) {
			CAPTURE(k);
			const sm::Shape &a = regions[k], &b = regions[k + 1];
			const sm::Plan p = sm::plan(a, b, sm::Pt{double(s.player().x), double(s.player().y)});
			const double aa = area(a), ab = area(b);
			CHECK(area(sm::at(p, 0.0)) == doctest::Approx(aa).epsilon(0.002));
			CHECK(area(sm::at(p, 1.0)) == doctest::Approx(ab).epsilon(0.002));
			for (double e : {0.25, 0.5, 0.75}) {
				CAPTURE(e);
				const sm::Shape m = sm::at(p, e);
				// no collapse, no flicker: the area stays between the two (a little slack for the shapes' change)
				CHECK(area(m) >= 0.8 * std::min(aa, ab));
				CHECK(area(m) <= 1.2 * std::max(aa, ab));
				// simple polygons: no crossing the regions do not have themselves (a planned
				// inversion's sub-pixel slivers: a few more while they move)
				int crossings = 0, own = 0;
				for (const sm::Ring &r : m.rings) {
					crossings += self_crossings(r);
				}
				for (const sm::Shape *x : {&a, &b}) {
					for (const sm::Ring &r : x->rings) {
						own += self_crossings(r);
					}
				}
				CHECK(crossings <= own + (c.label ? 16 : 0));
			}
			// a corner of two surfaces in both regions is paired with itself
			int corners = 0, paired = 0;
			for (const auto &ring : p.rings) {
				for (const sm::Track &t : ring) {
					if (t.from.tag == 0 || t.from.carrier >= 0) {
						continue; // (a corner: two surfaces or more)
					}
					bool in_a = false, in_b = false;
					for (const sm::Ring &r : a.rings) {
						for (const sm::Vertex &v : r) {
							in_a = in_a || (v.tag == t.from.tag && v.p.x == t.from.p.x && v.p.y == t.from.p.y);
						}
					}
					for (const sm::Ring &r : b.rings) {
						for (const sm::Vertex &v : r) {
							in_b = in_b || v.tag == t.from.tag;
						}
					}
					if (in_a && in_b) {
						corners++;
						paired += t.to.tag == t.from.tag ? 1 : 0;
					}
				}
			}
			CHECK(paired >= corners * 9 / 10);
		}
	}
}

TEST_CASE("present: a ring with no partner grows out of the anchor") {
	rcg::Session s(level("test_levels/crossings.json"));
	s.settle();
	const sm::Shape b = outline(s);
	const sm::Plan p = sm::plan(sm::Shape{}, b, sm::Pt{double(s.player().x), double(s.player().y)});
	CHECK(area(sm::at(p, 0.0)) == doctest::Approx(0.0));
	const double half = area(sm::at(p, 0.5)), full = area(sm::at(p, 1.0));
	CHECK(half > 0.0);
	CHECK(half < full);
	CHECK(full == doctest::Approx(area(b)).epsilon(0.002));
}

TEST_CASE("present: without animation the presenter's frame is the session's") {
	rcg::Session s(level("test_levels/crossings.json"));
	rcg::PresentConfig cfg;
	cfg.region_seconds = 0.0;
	cfg.interpolate = false;
	cfg.follow = false;
	cfg.dash_speed = 0.0; // (Stage 130: the dashes still)
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

TEST_CASE("present: a late region's change morphs for its duration, then shows the region") {
	rcg::Session s(level("main.json"));
	rcg::PresentConfig late;
	late.smooth_on_time = true; // (as if every region were late)
	rcg::Presenter p(late);
	s.settle();
	p.frame(s.draw_frame(), s);
	s.teleport(s.player() + rc::V2{20, 0});
	s.settle();
	p.advance(0.02);
	CHECK(vis_node(p.frame(s.draw_frame(), s))->version != vis_node(s.draw_frame())->version);
	CHECK(p.animating());
	p.advance(0.1);
	CHECK(vis_node(p.frame(s.draw_frame(), s))->version == vis_node(s.draw_frame())->version);
	CHECK_FALSE(p.animating());
	// a planned mirror: no collapse, the area moves from one region's to the other's
	const double before = mesh_area(*vis_node(s.draw_frame()));
	plan_side(s, "Mirror", 0, 0);
	REQUIRE(s.plan().size() == 1);
	const double after = mesh_area(*vis_node(s.draw_frame()));
	for (int k = 0; k < 7; k++) {
		CAPTURE(k);
		const double a = mesh_area(*vis_node(p.frame(s.draw_frame(), s)));
		CHECK(a >= 0.5 * std::min(before, after));
		p.advance(0.016);
	}
	p.advance(0.1);
	CHECK(vis_node(p.frame(s.draw_frame(), s))->version == vis_node(s.draw_frame())->version);
	// switched off: shrinks away; no frame has a mesh call without triangles (a host refuses one)
	s.key(rcg::Action::ToggleVisibility);
	s.settle();
	for (int k = 0; k < 8; k++) {
		const rcg::render::FrameNode shown = *vis_node(p.frame(s.draw_frame(), s)); // (held: a range-for's temporary dies first)
		for (const rcg::render::Call &c : *shown.calls) {
			if (c.kind == rcg::render::Kind::Triangles) {
				CHECK_FALSE(c.indices.empty());
				CHECK_FALSE(c.pts.empty());
			}
		}
		p.advance(0.016);
	}
}

TEST_CASE("present: while its next region is late, the region's shadows follow the drawn player") {
	// main.json: the player in a room of mirrors (occluders), no plan: the region's rays leave the player
	rcg::Session s(level("main.json"));
	rcg::PresentConfig late;
	late.smooth_on_time = true; // (as if every region were late)
	rcg::Presenter p(late);
	rcg::kinematics::Input in;
	in.axis_x = 1.0f;
	s.advance(rcg::kinematics::STEP * 1.5, in); // one tick, half of the next: the drawn player half a tick behind
	p.frame(s.draw_frame(), s);
	const rc::V2 drawn = p.player(s);
	REQUIRE(std::abs(double(drawn.x) - double(s.player().x)) > 1e-3);
	const rcg::render::FrameNode n = *vis_node(p.frame(s.draw_frame(), s));
	CHECK(n.version != vis_node(s.draw_frame())->version); // followed, not the exact region
	sm::Shape exact = outline(s);
	sm::mark_shadows(exact, sm::Pt{double(s.player().x), double(s.player().y)});
	int shadows = 0;
	for (const sm::Ring &r : exact.rings) {
		for (const sm::Vertex &v : r) {
			shadows += v.shadow ? 1 : 0;
		}
	}
	CHECK(shadows > 0);
	// the shown shape: the region's, its shadows recast from the drawn player
	const sm::Shape followed = sm::follow(exact, sm::Pt{double(drawn.x), double(drawn.y)});
	CHECK(mesh_area(n) == doctest::Approx(area(followed)).epsilon(0.001));
	CHECK(mesh_area(n) != doctest::Approx(area(exact)).epsilon(1e-6));
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

TEST_CASE("present: regions on time are drawn as they are: no morph, no following") {
	// an asynchronous session whose frames wait for their work (a generous budget): every region on time
	rcg::SessionConfig cfg;
	cfg.async = true;
	cfg.budget = 5.0;
	rcg::Session s(level("test_levels/crossings.json"), cfg);
	rcg::Presenter p;
	s.settle();
	p.frame(s.draw_frame(), s);
	rcg::kinematics::Input in;
	in.axis_x = 1.0f;
	for (int k = 0; k < 12; k++) {
		s.advance(1.0 / 90.0, in); // (between ticks: the drawn player is not the game's)
		p.advance(1.0 / 90.0);
		CHECK_FALSE(s.region_pending());
		const std::vector<rcg::render::FrameNode> got = p.frame(s.draw_frame(), s);
		CHECK(vis_node(got)->version == vis_node(s.draw_frame())->version);
		CHECK_FALSE(p.animating());
	}
}

TEST_CASE("present: a region that misses its frame follows, then morphs in") {
	// no budget: every region arrives a frame or more late; computed slower than the
	// threshold (here 0: main's regions are fast), it is smoothed
	rcg::SessionConfig cfg;
	cfg.async = true;
	rcg::Session s(level("main.json"), cfg);
	rcg::PresentConfig slow;
	slow.late_seconds = 0.0;
	rcg::Presenter p(slow);
	s.settle();
	s.drain();
	p.frame(s.draw_frame(), s);
	rcg::kinematics::Input in;
	in.axis_x = 1.0f;
	s.advance(rcg::kinematics::STEP * 1.5, in);
	p.advance(rcg::kinematics::STEP * 1.5);
	REQUIRE(s.region_pending());
	CHECK(vis_node(p.frame(s.draw_frame(), s))->version != vis_node(s.draw_frame())->version); // followed
	s.drain(); // the late region arrives
	p.advance(0.016);
	CHECK(vis_node(p.frame(s.draw_frame(), s))->version != vis_node(s.draw_frame())->version); // morphing in
	CHECK(p.animating());
	p.advance(0.2);
	CHECK(vis_node(p.frame(s.draw_frame(), s))->version == vis_node(s.draw_frame())->version);
}

TEST_CASE("present: a region faster than 30 Hz is drawn as it is, though it missed its frame") {
	// (the user, Stage 124: smoothing only when the computation is slower than 30 Hz;
	// Stage 126: the computation, not the latency -- here 66 ms on the presenter's
	// clock, for a region main computes in a millisecond or two)
	rcg::SessionConfig cfg;
	cfg.async = true;
	rcg::Session s(level("main.json"), cfg);
	rcg::Presenter p;
	s.settle();
	s.drain();
	p.frame(s.draw_frame(), s);
	rcg::kinematics::Input in;
	in.axis_x = 1.0f;
	s.advance(rcg::kinematics::STEP * 1.5, in);
	p.advance(rcg::kinematics::STEP * 1.5);
	REQUIRE(s.region_pending());
	p.frame(s.draw_frame(), s); // its request seen
	p.advance(0.05);
	CHECK(vis_node(p.frame(s.draw_frame(), s))->version == vis_node(s.draw_frame())->version); // not followed
	s.drain();                 // it arrives
	p.advance(0.016);
	CHECK(vis_node(p.frame(s.draw_frame(), s))->version == vis_node(s.draw_frame())->version);
	CHECK_FALSE(p.animating());
}

TEST_CASE("present: one late region does not keep on-time regions morphing") {
	// (the user saw main morphing while walking: the first region, late, began a morph that
	// every on-time region after it restarted)
	rcg::SessionConfig cfg;
	cfg.async = true;
	rcg::Session s(level("main.json"), cfg);
	rcg::PresentConfig slow;
	slow.late_seconds = 0.0; // (main's regions are fast: every late one counts as slow here)
	rcg::Presenter p(slow);
	s.settle();
	p.frame(s.draw_frame(), s); // the first region pending
	s.drain();
	p.advance(0.05);
	p.frame(s.draw_frame(), s); // it arrives 50 ms after its request: late, a morph
	CHECK(p.animating());
	s.set_budget(5.0); // from now on every region is on time
	rcg::kinematics::Input in;
	in.axis_x = 1.0f;
	int morphing = 0;
	for (int k = 0; k < 30; k++) {
		s.advance(1.0 / 60.0, in);
		p.advance(1.0 / 60.0);
		REQUIRE_FALSE(s.region_pending());
		p.frame(s.draw_frame(), s);
		morphing += p.animating() ? 1 : 0;
	}
	CHECK(morphing <= 6); // (the first morph's 100 ms, at most)
	CHECK_FALSE(p.animating());
}

namespace {

// pixel-centre coverage of a triangle mesh, as a GPU fills it (the top-left rule)
struct Coverage {
	int x0 = 0, y0 = 0, w = 0, h = 0;
	std::vector<uint8_t> n;
	Coverage(const std::vector<rc::V2> &pts, const std::vector<int> &idx) {
		if (pts.empty()) {
			return;
		}
		double ax = INFINITY, ay = INFINITY, bx = -INFINITY, by = -INFINITY;
		for (const rc::V2 &q : pts) {
			ax = std::min(ax, double(q.x));
			ay = std::min(ay, double(q.y));
			bx = std::max(bx, double(q.x));
			by = std::max(by, double(q.y));
		}
		x0 = int(std::floor(ax)) - 1;
		y0 = int(std::floor(ay)) - 1;
		w = int(std::ceil(bx)) + 2 - x0;
		h = int(std::ceil(by)) + 2 - y0;
		n.assign(size_t(w * h), 0);
		for (size_t t = 0; t + 2 < idx.size(); t += 3) {
			const rc::V2 &a = pts[size_t(idx[t])], &b = pts[size_t(idx[t + 1])], &c = pts[size_t(idx[t + 2])];
			const double area = (double(b.x) - a.x) * (double(c.y) - a.y) - (double(b.y) - a.y) * (double(c.x) - a.x);
			if (area == 0.0) {
				continue;
			}
			const double sg = area > 0 ? 1.0 : -1.0;
			auto inside = [&](const rc::V2 &p, const rc::V2 &q, double X, double Y) {
				const double e = sg * ((double(q.x) - p.x) * (Y - p.y) - (double(q.y) - p.y) * (X - p.x));
				if (e != 0.0) {
					return e > 0.0;
				}
				const double dx = sg * (double(q.x) - p.x), dy = sg * (double(q.y) - p.y);
				return (dy == 0.0 && dx < 0.0) || dy > 0.0;
			};
			const int xa = int(std::floor(std::min({a.x, b.x, c.x}))), xb = int(std::ceil(std::max({a.x, b.x, c.x})));
			const int ya = int(std::floor(std::min({a.y, b.y, c.y}))), yb = int(std::ceil(std::max({a.y, b.y, c.y})));
			for (int y = ya; y <= yb; y++) {
				for (int x = xa; x <= xb; x++) {
					const double X = x + 0.5, Y = y + 0.5;
					if (inside(a, b, X, Y) && inside(b, c, X, Y) && inside(c, a, X, Y)) {
						n[size_t((y - y0) * w + (x - x0))]++;
					}
				}
			}
		}
	}
	int at(int x, int y) const { return x < x0 || y < y0 || x >= x0 + w || y >= y0 + h ? 0 : n[size_t((y - y0) * w + (x - x0))]; }
	// pixels covered twice; uncovered pixels with covered neighbours both sides (a crack)
	// that `inside` says belong to the region
	std::pair<int, int> faults(const std::function<bool(double, double)> &inside_region) const {
		int twice = 0, cracks = 0;
		for (int y = y0 + 1; y < y0 + h - 1; y++) {
			for (int x = x0 + 1; x < x0 + w - 1; x++) {
				const int v = at(x, y);
				twice += v >= 2 ? 1 : 0;
				if (v == 0 && ((at(x - 1, y) && at(x + 1, y)) || (at(x, y - 1) && at(x, y + 1))) && inside_region(x + 0.5, y + 0.5)) {
					cracks++;
				}
			}
		}
		return {twice, cracks};
	}
};

} // namespace

TEST_CASE("present: the region is drawn as the union of its cells: no pixel twice, no crack, still or morphing") {
	// (the user saw hairline overlaps and cracks: the fill drawn cell by cell, the
	// morph cell by cell; now one outline, holes and all)
	struct Case {
		const char *name;
		const char *label;
	};
	for (const Case &c : {Case{"main.json", nullptr}, Case{"test_levels/crossings.json", nullptr}, Case{"playground/tangent_infinity.json", nullptr},
			 Case{"playground/pre_inverted.json", nullptr}, Case{"playground/inversion_grid.json", "FullInversion"}}) {
		CAPTURE(std::string(c.name));
		rcg::Session s(level(c.name));
		s.settle();
		if (c.label) {
			plan_side(s, c.label, 3, 0);
		}
		rcg::PresentConfig late;
		late.smooth_on_time = true;
		rcg::Presenter p(late);
		p.frame(s.draw_frame(), s);
		for (int k = 0; k < 4; k++) {
			s.teleport(s.player() + rc::V2{9, 4});
			s.settle();
			// a pixel that is a crack must lie in the region, clear of its boundary
			// by more than the arcs' chords (0.25 px)
			auto in_region = [&](double x, double y) {
				const rc::vis::Pt q{x, y};
				return s.region() && rc::vis::contains(*s.region(), q) == rc::vis::Membership::Inside && rc::vis::margin(*s.region(), q) > 0.3;
			};
			const rcg::render::Call *m = mesh(*vis_node(s.draw_frame()));
			if (m) {
				const auto [twice, cracks] = Coverage(m->pts, m->indices).faults(in_region);
				CHECK(twice == 0);
				CHECK(cracks == 0);
			}
			for (int f = 0; f < 3; f++) { // the morph's frames
				p.advance(0.03);
				const auto n = vis_node(p.frame(s.draw_frame(), s));
				if (const rcg::render::Call *mm = mesh(*n)) {
					CHECK(Coverage(mm->pts, mm->indices).faults([](double, double) { return false; }).first == 0);
				}
			}
			p.advance(0.2);
			p.frame(s.draw_frame(), s);
		}
	}
}

// ---- Stage 126: the path preview's morph

namespace {

std::optional<rcg::render::FrameNode> path_node(const std::vector<rcg::render::FrameNode> &nodes) {
	for (const rcg::render::FrameNode &n : nodes) {
		if (n.key.kind == rcg::render::NodeKind::Path) {
			return n;
		}
	}
	return std::nullopt;
}

rc::TraceStep step(rc::V2 a, rc::V2 b, int sid, rc::StepType type = rc::StepType::Aligned) {
	rc::TraceStep s = rc::TraceStep::make(a, b, 0, std::nullopt, std::nullopt, rc::V2{}, false);
	s.surface_id = sid;
	s.type = type;
	return s;
}

bool same_points(const std::vector<rc::TraceStep> &a, const std::vector<rc::TraceStep> &b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); i++) {
		if (!(a[i].start == b[i].start) || !(a[i].end == b[i].end)) {
			return false;
		}
	}
	return true;
}

} // namespace

TEST_CASE("present: the path's diff starts and ends on the two paths, stays joined, and grows a new step out of its point") {
	namespace pm = rcg::path_morph;
	// old: player -> mirror 3 -> wall 1;  new: player -> mirror 3 (moved) -> mirror 4 -> wall 1
	const std::vector<rc::TraceStep> from{step({0, 0}, {100, 0}, 3), step({100, 0}, {100, 100}, 1, rc::StepType::DivergedPhysical)};
	const std::vector<rc::TraceStep> to{step({0, 0}, {110, 0}, 3), step({110, 0}, {200, 50}, 4), step({200, 50}, {150, 120}, 1)};
	const std::vector<pm::Pair> plan = pm::plan(from, to);
	REQUIRE(plan.size() == 3); // 3 and 1 paired; 4 grows
	CHECK(plan[0].from.surface_id == 3);
	CHECK(plan[1].from.start == plan[1].from.end);              // 4's partner: a point...
	CHECK(plan[1].from.start == rc::V2{100, 0});                 // ... where the old path had reached
	CHECK(same_points(pm::at(plan, 1.0), to));
	std::vector<rc::TraceStep> start = pm::at(plan, 0.0);
	start.erase(start.begin() + 1); // (the grown step, still a point)
	CHECK(same_points(start, from));
	for (double e : {0.25, 0.5, 0.75}) {
		const std::vector<rc::TraceStep> mid = pm::at(plan, e);
		for (size_t k = 0; k + 1 < mid.size(); k++) {
			CHECK((mid[k].end - mid[k + 1].start).length() < 1e-4f); // joined
		}
	}
	// a step's colour switches at the midpoint, never blends
	CHECK(pm::at(plan, 0.49).back().type == rc::StepType::DivergedPhysical);
	CHECK(pm::at(plan, 0.51).back().type == rc::StepType::Aligned);
}

TEST_CASE("present: a preview whose exact climb changes nothing is drawn as it is; a changed path morphs in when it is late") {
	// inversion_grid: a cursor whose preview declines float64 (Stage 131: the event
	// limits leave one of the circle's, and its exact path is its provisional one)
	const rcg::Level lv = level("playground/inversion_grid.json");
	for (const double late : {0.0, 1.0 / 30.0}) {
		rcg::SessionConfig cfg;
		cfg.async = true;
		rcg::Session s(lv, cfg);
		rcg::PresentConfig pc;
		pc.late_seconds = late; // (0: every late preview counts as slow)
		pc.dash_speed = 0.0;     // (Stage 130: the dashes still -- a repaint here is a morph)
		rcg::Presenter p(pc);
		const rc::V2 home = s.player();
		s.settle();
		s.drain();
		p.frame(s.draw_frame(), s);
		s.set_cursor(home + rc::V2::of(420.0, 0.0));
		s.settle();
		REQUIRE_FALSE(s.preview_exact());
		p.advance(0.016);
		p.frame(s.draw_frame(), s); // the provisional path shown
		s.drain();                 // the exact one arrives: the same drawing
		p.advance(0.016);
		const auto shown = path_node(p.frame(s.draw_frame(), s));
		CHECK_FALSE(p.animating_path()); // nothing to morph, slow or not
		CHECK(shown->version == path_node(s.draw_frame())->version);
	}
	// a path that changes, late (smooth_on_time: every path counts as late): it morphs in, then is the session's
	rcg::Session s(lv);
	rcg::PresentConfig pc;
	pc.smooth_on_time = true;
	pc.dash_speed = 0.0;
	rcg::Presenter p(pc);
	const rc::V2 home = s.player();
	s.set_cursor(home + rc::V2::of(300.0, -40.0));
	s.settle();
	p.frame(s.draw_frame(), s);
	s.set_cursor(home + rc::V2::of(-300.0, 60.0));
	s.settle();
	p.advance(0.016);
	const auto shown = path_node(p.frame(s.draw_frame(), s));
	CHECK(p.animating_path());
	CHECK(shown->version != path_node(s.draw_frame())->version); // morphing in
	p.advance(0.2);
	CHECK(path_node(p.frame(s.draw_frame(), s))->version == path_node(s.draw_frame())->version); // then the session's
	CHECK_FALSE(p.animating_path());
}

// Stage 130: the preview's look -- a solid step is a dark stroke under bright
// dashes; the dashes run the way the arrow flies; steps drawn on one another
// are set side by side
namespace {
std::vector<rc::TraceStep> two_steps(rc::StepType a, rc::StepType b, bool reversed) {
	rc::TraceStep s1;
	s1.start = rc::V2{100, 100};
	s1.end = rc::V2{300, 100};
	s1.via = rc::V2{200, 100};
	s1.type = a;
	rc::TraceStep s2 = s1;
	s2.type = b;
	if (reversed) {
		std::swap(s2.start, s2.end);
	}
	return {s1, s2};
}
} // namespace

TEST_CASE("present: a solid step is a dark stroke under bright dashes, and the dashes run along the arrow's way") {
	rcg::Session s(level("test_levels/crossings.json"));
	rc::TraceStep st;
	st.start = rc::V2{100, 100};
	st.end = rc::V2{300, 100};
	st.via = rc::V2{200, 100};
	st.type = rc::StepType::Aligned;
	std::vector<rcg::render::Call> c0, c1;
	s.paint_steps({st}, c0, 0.0);
	s.paint_steps({st}, c1, 4.0);
	REQUIRE(c0.size() >= 2);
	CHECK(c0[0].kind == rcg::render::Kind::Line);      // the solid stroke
	CHECK(c0[1].kind == rcg::render::Kind::Multiline); // the dashes over it
	CHECK(c0[0].colors[0][1] < c0[1].colors[0][1]);    // darker under, brighter over
	CHECK(c0[1].colors[0][0] > c0[0].colors[0][0]);
	// the dashes (10 px on, 5 off) start at the step's start; 4 px of phase moves them 4 px along
	REQUIRE(c1.size() >= 2);
	CHECK(c0[1].pts[0].x == 100.0f);
	CHECK(c0[1].pts[1].x == 110.0f);
	CHECK(c1[1].pts[0].x == 104.0f);
	CHECK(c1[1].pts[1].x == 114.0f);
}

TEST_CASE("present: steps drawn on one another are set side by side, the planned on the left") {
	rcg::Session s(level("test_levels/crossings.json"));
	for (const bool reversed : {false, true}) {
		std::vector<rcg::render::Call> out;
		s.paint_steps(two_steps(rc::StepType::DivergedPlanned, rc::StepType::DivergedPhysical, reversed), out, 0.0);
		// the planned step's solid stroke, then its dashes, then the physical's dashes
		REQUIRE(out.size() >= 3);
		const float planned_y = out[0].pts[0].y;
		const float physical_y = out[2].pts[0].y;
		CHECK(planned_y == doctest::Approx(100.0 - 1.75)); // (y down: the left of a way to +x is -y)
		CHECK(physical_y == doctest::Approx(100.0 + 1.75));
	}
	// steps apart are drawn where they are
	std::vector<rc::TraceStep> apart = two_steps(rc::StepType::DivergedPlanned, rc::StepType::DivergedPhysical, false);
	apart[1].start.y += 20;
	apart[1].end.y += 20;
	apart[1].via.y += 20;
	std::vector<rcg::render::Call> out;
	s.paint_steps(apart, out, 0.0);
	CHECK(out[0].pts[0].y == 100.0f);
}

// Stage 130 (the user's F12 dump of 2026-10-22 15:43): after the divergence the
// planned step ends at the cursor and the physical one runs on to the next hit
// -- the same circle, different ends: set side by side all the same
TEST_CASE("present: steps on one carrier with different ends are set side by side") {
	rcg::Session s(level("test_levels/crossings.json"));
	s.teleport(rc::V2::of(0x1.617668p+10, 0x1.ea1932p+8));
	rc::PlanItem it;
	it.surface_id = 17;
	it.left = true;
	it.hint = rc::effects::F32Point{1352.40869140625f, 393.7291564941406f};
	it.span = std::array<double, 6>{1400.0, 390.0, 1400.0, 210.0, 1310.0, 300.0};
	s.set_plan({it});
	s.set_cursor(rc::V2::of(0x1.792492p+10, 0x1.79b31p+8));
	s.settle();
	const std::vector<rc::TraceStep> &m = s.preview_steps();
	REQUIRE(m.size() >= 3);
	REQUIRE(m[1].type == rc::StepType::DivergedPlanned);
	REQUIRE(m[2].type == rc::StepType::DivergedPhysical);
	REQUIRE(m[1].end != m[2].end); // (the cursor; the next hit)
	const rc::arc::ArcParams p = rc::arc::arc_params(m[1].start, m[1].via, m[1].end);
	std::vector<rcg::render::Call> out;
	s.paint_steps(m, out, 0.0);
	// the aligned step (a stroke, its dashes), the planned arc (a stroke, its dashes), the physical dashes
	REQUIRE(out.size() >= 5);
	REQUIRE(out[2].kind == rcg::render::Kind::Arc);
	const double planned_r = out[2].radius;
	const rc::V2 q = out[4].pts[0];
	const double physical_r = std::hypot(double(q.x) - double(p.center.x), double(q.y) - double(p.center.y));
	CHECK(std::abs(planned_r - p.radius) == doctest::Approx(1.75).epsilon(0.01));
	CHECK(std::abs(physical_r - p.radius) == doctest::Approx(1.75).epsilon(0.01));
	CHECK((planned_r - p.radius) * (physical_r - p.radius) < 0.0); // on either side
}

// Stage 131 (the user): a significant event is marked apart -- a ringed dot (a
// white disc under one in the step's colour); any other crossing a small dot
TEST_CASE("present: significant events are ringed dots, other crossings small dots") {
	rcg::Session s(level("test_levels/crossings.json"));
	rc::TraceStep a;
	a.start = rc::V2{100, 100};
	a.end = rc::V2{300, 100};
	a.via = rc::V2{200, 100};
	a.type = rc::StepType::AlignedPostPlanned;
	a.significant = true;
	rc::TraceStep b = a;
	b.start = a.end;
	b.end = rc::V2{300, 300};
	b.via = rc::V2{300, 200};
	b.significant = false;
	std::vector<rcg::render::Call> out;
	s.paint_steps({a, b}, out, 0.0);
	std::vector<const rcg::render::Call *> discs;
	for (const auto &c : out) {
		if (c.kind == rcg::render::Kind::Circle) {
			discs.push_back(&c);
		}
	}
	REQUIRE(discs.size() == 3);
	CHECK(discs[0]->pts[0] == rc::V2{300, 100});
	CHECK(discs[0]->radius == 5.5); // the ring
	CHECK(discs[1]->radius == 4.0); // the dot in it
	CHECK(discs[2]->pts[0] == rc::V2{300, 300});
	CHECK(discs[2]->radius == 2.0); // a crossing
}

// Stage 131 (the user): a passage of infinity off the level and a portal are one
// significant event drawn as two half rings -- out and back in, each facing the level
TEST_CASE("present: a wrap and a portal are marked by two half rings") {
	auto polys = [](const std::vector<rcg::render::Call> &out) {
		std::vector<const rcg::render::Call *> v;
		for (const auto &c : out) {
			if (c.kind == rcg::render::Kind::Polygon) {
				v.push_back(&c);
			}
		}
		return v;
	};
	auto step = [](rc::V2 a, rc::V2 b) {
		rc::TraceStep st;
		st.start = a;
		st.end = b;
		st.via = (a + b) * 0.5f;
		st.type = rc::StepType::AlignedPostPlanned;
		return st;
	};
	// the wrap as the game draws it (the user: the half rings were off screen): the
	// path leaves the level at the right edge two steps before the passage of
	// infinity, and enters again at the left edge a step after it
	{
		rcg::Session s(level("test_levels/crossings.json")); // (bounds 0..1920 x 0..1080)
		rc::TraceStep a = step(rc::V2{1420, 840}, rc::V2{1920, 1006.67f});
		rc::TraceStep b = step(rc::V2{1920, 1006.67f}, rc::V2{2140, 1080});
		rc::TraceStep c = step(rc::V2{2140, 1080}, rc::V2{2234.87f, 1111.62f});
		c.significant = true; // (the passage of infinity: no id -- an unplanned wrap)
		rc::TraceStep d = step(rc::V2{-1194.87f, -31.62f}, rc::V2{-1100, 0});
		d.after_infinity = true;
		rc::TraceStep e = step(rc::V2{-1100, 0}, rc::V2{-380, 240});
		rc::TraceStep f = step(rc::V2{-380, 240}, rc::V2{0, 366.667f});
		rc::TraceStep g = step(rc::V2{0, 366.667f}, rc::V2{560, 553.333f});
		std::vector<rcg::render::Call> out;
		s.paint_steps({a, b, c, d, e, f, g}, out, 0.0);
		const auto wrap = polys(out);
		REQUIRE(wrap.size() == 4); // (two half rings: a white half and a coloured one each)
		CHECK(wrap[0]->pts[0].x == doctest::Approx(1920.0)); // leaving the bounds, on screen
		for (const auto &q : wrap[0]->pts) {
			CHECK(q.x <= 1920.0f + 1e-3f); // facing back into the level
		}
		CHECK(wrap[2]->pts[0].x == doctest::Approx(0.0)); // entering them
		for (const auto &q : wrap[2]->pts) {
			CHECK(q.x >= -1e-3f);
		}
	}
	// a portal: the halves normal to the portal (the user), not along the way --
	// portals x = 550 (10) and x = 1350 (11), a beam in at a slant
	{
		rcg::Session s(level("playground/portals.json"));
		rc::TraceStep to_portal = step(rc::V2{350, 600}, rc::V2{550, 700});
		to_portal.surface_id = 10;
		to_portal.significant = true;
		to_portal.teleports = true;
		rc::TraceStep from_portal = step(rc::V2{1350, 700}, rc::V2{1550, 800});
		from_portal.after_portal = true;
		std::vector<rcg::render::Call> out;
		s.paint_steps({to_portal, from_portal}, out, 0.0);
		const auto portal = polys(out);
		REQUIRE(portal.size() == 4);
		auto symmetric_about = [](const rcg::render::Call *c, float y0) {
			float lo = 1e9f, hi = -1e9f;
			for (const auto &q : c->pts) {
				lo = std::min(lo, q.y);
				hi = std::max(hi, q.y);
			}
			return std::abs((hi - y0) - (y0 - lo)) < 1e-3f;
		};
		CHECK(portal[0]->pts[0] == rc::V2{550, 700});
		CHECK(symmetric_about(portal[0], 700.0f)); // (along the normal: no tilt)
		for (const auto &q : portal[0]->pts) {
			CHECK(q.x <= 550.0f + 1e-3f); // the side the beam came from
		}
		CHECK(portal[2]->pts[0] == rc::V2{1350, 700});
		CHECK(symmetric_about(portal[2], 700.0f));
		for (const auto &q : portal[2]->pts) {
			CHECK(q.x >= 1350.0f - 1e-3f); // the side it leaves by
		}
	}
	// a circular portal (the user): the ring's part on the beam's side of the
	// portal's circle, its arc meeting the circle at both ends -- portals 12
	// (centre (1576.6, 675), r 146.6) and 13 (centre (476.6, 675)), a beam from the
	// west into 12 at its westmost point, out of 13 at its westmost point, inward
	{
		rcg::Session s(level("playground/portals.json"));
		rc::TraceStep to_portal = step(rc::V2{1200, 675}, rc::V2{1430, 675});
		to_portal.surface_id = 12;
		to_portal.significant = true;
		to_portal.teleports = true;
		rc::TraceStep from_portal = step(rc::V2{330, 675}, rc::V2{450, 675});
		from_portal.after_portal = true;
		std::vector<rcg::render::Call> out;
		s.paint_steps({to_portal, from_portal}, out, 0.0);
		const auto portal = polys(out);
		REQUIRE(portal.size() == 4);
		const double R = 146.607142857; // (both circles)
		auto check_side = [&](const rcg::render::Call *c, rc::V2 centre, bool outside) {
			int on_circle = 0;
			for (const auto &q : c->pts) {
				const double d = std::hypot(double(q.x - centre.x), double(q.y - centre.y));
				CHECK((outside ? d >= R - 1e-2 : d <= R + 1e-2));
				on_circle += std::abs(d - R) < 1e-2 ? 1 : 0;
			}
			CHECK(on_circle >= 3); // (`at` and the arc's two ends, along the circle)
		};
		check_side(portal[0], rc::V2{1576.607142857f, 675}, true);  // entry: the side the beam came from
		check_side(portal[2], rc::V2{476.607142857f, 675}, false); // exit: the side it leaves by
	}
}

// Stage 135 (the user's F12 dump, 2026-10-23 18:24): on open_room_playable a shot
// runs back and forth on one line (five laps through infinity). Lanes went by
// steps, so a pass changed lane at the cursor and at every crossing. Now each
// pass keeps one lane; the lanes are packed and centred on the path
TEST_CASE("present: side by side by passes -- one lane per pass, packed, centred") {
	rcg::Session s(level("open_room_playable.json"));
	s.teleport(rc::V2::of(0x1.7e805ep+10, 0x1.45a09p+9));
	s.set_cursor(rc::V2::of(0x1.86a82cp+10, 0x1.047b8p+9));
	s.settle();
	const std::vector<rc::TraceStep> &m = s.preview_steps();
	REQUIRE(m.size() > 10);
	const std::vector<double> off = rcg::lane_offsets(m);
	REQUIRE(off.size() == m.size());
	std::string text;
	for (size_t i = 0; i < m.size(); i++) {
		char b[160];
		std::snprintf(b, sizeof b, "\n  %zu (%.1f, %.1f) -> (%.1f, %.1f) off %.2f", i, m[i].start.x, m[i].start.y, m[i].end.x, m[i].end.y, off[i]);
		text += b;
	}
	INFO(text);
	std::set<double> lanes;
	for (size_t i = 0; i < m.size(); i++) {
		lanes.insert(off[i]);
		// a step that goes on from the one before (no wrap) keeps its lane
		if (i > 0 && m[i].start == m[i - 1].end) {
			CHECK(off[i] == off[i - 1]);
		}
	}
	// evenly spaced and centred on the path
	REQUIRE(lanes.size() >= 2);
	const std::vector<double> l(lanes.begin(), lanes.end());
	for (size_t k = 1; k < l.size(); k++) {
		CHECK(l[k] - l[k - 1] == doctest::Approx(3.5));
	}
	CHECK(l.front() + l.back() == doctest::Approx(0.0));
}
