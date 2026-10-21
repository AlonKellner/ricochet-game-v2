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
	// no budget: every region arrives a frame or more late; slower than 30 Hz, it is smoothed
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
	CHECK(vis_node(p.frame(s.draw_frame(), s))->version == vis_node(s.draw_frame())->version); // 25 ms old: not late yet
	p.advance(0.02);
	REQUIRE(s.region_pending());
	CHECK(vis_node(p.frame(s.draw_frame(), s))->version != vis_node(s.draw_frame())->version); // 45 ms old: followed
	s.drain(); // the late region arrives
	p.advance(0.016);
	CHECK(vis_node(p.frame(s.draw_frame(), s))->version != vis_node(s.draw_frame())->version); // morphing in
	CHECK(p.animating());
	p.advance(0.2);
	CHECK(vis_node(p.frame(s.draw_frame(), s))->version == vis_node(s.draw_frame())->version);
}

TEST_CASE("present: a region faster than 30 Hz is drawn as it is, though it missed its frame") {
	// (the user, Stage 124: smoothing only when the computation is slower than 30 Hz)
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
	s.drain();                 // it arrives 16 ms after its request (a frame or two late)
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
	rcg::Presenter p;
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
