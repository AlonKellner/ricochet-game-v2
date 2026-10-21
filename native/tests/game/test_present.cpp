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
	return s.region() ? sm::shape_of(*s.region(), sm::carriers_of(*s.region(), &s.world())) : sm::Shape{};
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
		for (const rcg::render::Call &c : *vis_node(p.frame(s.draw_frame(), s))->calls) {
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
	// no budget: every region arrives a frame or more late
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
	CHECK(vis_node(p.frame(s.draw_frame(), s))->version != vis_node(s.draw_frame())->version); // followed
	s.drain(); // the late region arrives
	p.advance(0.016);
	CHECK(vis_node(p.frame(s.draw_frame(), s))->version != vis_node(s.draw_frame())->version); // morphing in
	CHECK(p.animating());
	p.advance(0.2);
	CHECK(vis_node(p.frame(s.draw_frame(), s))->version == vis_node(s.draw_frame())->version);
}

TEST_CASE("present: one late region does not keep on-time regions morphing") {
	// (the user saw main morphing while walking: the first region, late, began a morph that
	// every on-time region after it restarted)
	rcg::SessionConfig cfg;
	cfg.async = true;
	rcg::Session s(level("main.json"), cfg);
	rcg::Presenter p;
	s.settle();
	p.frame(s.draw_frame(), s); // the first region pending: late
	s.drain();
	p.advance(0.016);
	p.frame(s.draw_frame(), s); // it arrives: a morph
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

TEST_CASE("present: cells that do not overlap morph as one outline (no edge between cells to part)") {
	rcg::Session s(level("main.json"));
	s.settle();
	const sm::Shape a = outline(s);
	CHECK(a.outline);
	CHECK(a.rings.size() == 1);
	CHECK(a.pieces.size() > 1);
	s.teleport(s.player() + rc::V2{-12, 6});
	s.settle();
	const sm::Plan p = sm::plan(a, outline(s), sm::Pt{0, 0});
	CHECK(p.outline);
	CHECK(sm::at(p, 0.5).rings.size() == 1);
	// a planned inversion's cells overlap: its shape is its pieces
	rcg::Session v(level("playground/inversion_grid.json"));
	plan_side(v, "FullInversion", 3, 0);
	v.teleport(v.player() + rc::V2{0, 7});
	v.settle();
	const sm::Shape inv = outline(v);
	CHECK_FALSE(inv.outline);
	CHECK(inv.rings.size() == inv.pieces.size());
}
