// Stage 153 (GAME_SPEC 9.9, docs/TDD_58): light and shadow. Dark and light
// regions, bounded by elements; the arrow stops where it would enter the
// shadow; surfaces wholly in it are gone for the arrow; light undoes it.
#include "rcg/build.hpp"
#include "rcg/level.hpp"
#include "rcg/session.hpp"
#include "rcg/shade.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <string>

namespace {

using json = nlohmann::json;

rcg::Level level_of(const std::string &extra) {
	json lv = json::parse(R"({"schema": "rc-level/2", "player": {"start": [660, 540]}, "room": {"rect": [560, 240, 800, 600], "built": true},
		"elements": []})");
	const json e = json::parse(extra);
	for (auto it = e.begin(); it != e.end(); ++it) {
		lv[it.key()] = it.value();
	}
	return rcg::parse_level_json(lv);
}

void shoot(rcg::Session &s, double x, double y) {
	s.set_cursor(rc::V2::of(x, y));
	s.settle();
	s.fire(-1, 1.0 / 60.0);
}

// a button inside a dark disk (opening the door "d", far away)
std::string disk_level(const std::string &more_elements, const std::string &flags = "{}") {
	return R"({"flags": )" + flags + R"(, "elements": [
		{"kind": "dark", "name": "night", "circle": {"center": [1150, 540], "radius": 150}},
		{"kind": "button", "name": "b", "opens": "d", "start": [1150, 500], "end": [1150, 580]},
		{"kind": "door", "name": "d", "start": [1300, 700], "end": [1300, 800]})" +
			more_elements + "]}";
}

} // namespace

TEST_CASE("shade: a region's sides are the surface's sides") {
	const rc::arc::SegGeom line = rc::arc::SegGeom::make(rc::V2::of(1000, 300), rc::V2::of(1000, 700), rc::V2::of(1000, 500));
	for (const rc::V2 p : {rc::V2::of(900, 500), rc::V2::of(1100, 420)}) {
		const bool left = line.determine_side(p) == rc::Side::Left;
		CHECK(rcg::shade::region_of(line, "left", true).contains(p.x, p.y) == left);
		CHECK(rcg::shade::region_of(line, "right", true).contains(p.x, p.y) == !left);
		CHECK(rcg::shade::region_of(line, "auto", true).contains(p.x, p.y) == left);
	}
	const rc::arc::SegGeom circle = rc::arc::SegGeom::full_from_carrier(rc::arc::from_circle(rc::V2::of(500, 500), 100));
	CHECK(rcg::shade::region_of(circle, "auto", true).contains(520, 480));
	CHECK_FALSE(rcg::shade::region_of(circle, "outside", true).contains(520, 480));
	CHECK(rcg::shade::region_of(circle, "outside", true).contains(700, 480));
	const bool left_in = circle.determine_side(rc::V2::of(520, 500)) == rc::Side::Left;
	CHECK(rcg::shade::region_of(circle, "left", true).inside == left_in);
}

TEST_CASE("shade: the arrow stops where it would enter the shadow; a surface in it is gone for the arrow") {
	rcg::Session s(level_of(disk_level("")));
	// the button is darkened, the boundary is in the world (not drawn)
	const rcg::BuiltSurface *b = s.world().by_id(6); // (the room 1-4, the dark barrier 5)
	REQUIRE(b != nullptr);
	CHECK_FALSE(b->left.has_value());
	shoot(s, 1150, 540);
	CHECK_FALSE(s.flags().has("door:d")); // stopped at the shadow's edge
	// the shadow is drawn over the surfaces
	bool drawn = false;
	for (const auto &n : s.draw_frame()) {
		drawn = drawn || n.key.kind == rcg::render::NodeKind::Shadow;
	}
	CHECK(drawn);
}

TEST_CASE("shade: light undoes the shadow; a light switched on by a flag; undo") {
	const std::string lamp = R"(, {"kind": "light", "name": "lamp", "when": "lamp", "circle": {"center": [1150, 540], "radius": 200}})";
	{
		rcg::Session s(level_of(disk_level(lamp, R"({"lamp": true})")));
		shoot(s, 1150, 540);
		CHECK(s.flags().is_true("door:d")); // lit: the button is pressed
	}
	{
		// the lamp off: a switch turns it on, then the shot passes; undo turns it off again
		rcg::Session s(level_of(disk_level(lamp + R"(, {"kind": "button", "name": "sw", "flag": "lamp", "start": [800, 600], "end": [800, 700]})")));
		shoot(s, 1150, 540);
		CHECK_FALSE(s.flags().has("door:d"));
		shoot(s, 800, 650);
		CHECK(s.flags().is_true("lamp"));
		shoot(s, 1150, 540);
		CHECK(s.flags().is_true("door:d"));
		s.key(rcg::Action::Undo);
		s.key(rcg::Action::Undo);
		CHECK_FALSE(s.flags().has("lamp"));
		CHECK_FALSE(s.world().by_id(6)->left.has_value()); // darkened again
		shoot(s, 1150, 540);
		CHECK_FALSE(s.flags().has("door:d"));
	}
}

TEST_CASE("shade: an arrow in the shadow passes its surfaces and leaves it") {
	// the player in the shadow (left of x = 800); a button in the light
	rcg::Session s(level_of(R"({"elements": [
		{"kind": "dark", "name": "night", "start": [800, 300], "end": [800, 700]},
		{"kind": "wall", "name": "w", "start": [700, 400], "end": [700, 700]},
		{"kind": "button", "name": "b", "opens": "d", "start": [1000, 500], "end": [1000, 600]},
		{"kind": "door", "name": "d", "start": [1300, 700], "end": [1300, 800]}]})"));
	const rcg::shade::Region r = rcg::shade::region_of(rcg::segment_of(s.level().surfaces[4].spec), "auto", true);
	const char *side = r.contains(660, 540) ? "auto" : "right";
	if (std::string(side) == "right") { // (the player's side, whichever the convention calls it)
		rcg::Session t(level_of(R"({"elements": [
			{"kind": "dark", "name": "night", "side": "right", "start": [800, 300], "end": [800, 700]},
			{"kind": "wall", "name": "w", "start": [700, 400], "end": [700, 700]},
			{"kind": "button", "name": "b", "opens": "d", "start": [1000, 500], "end": [1000, 600]},
			{"kind": "door", "name": "d", "start": [1300, 700], "end": [1300, 800]}]})"));
		CHECK_FALSE(t.world().by_id(6)->left.has_value());
		shoot(t, 1000, 550);
		CHECK(t.flags().is_true("door:d"));
		return;
	}
	MESSAGE("a vertical line's left (downward) is the player's side");
	CHECK_FALSE(s.world().by_id(6)->left.has_value()); // the wall in the shadow
	shoot(s, 1000, 550); // through the darkened wall, out of the shadow, onto the button
	CHECK(s.flags().is_true("door:d"));
}

TEST_CASE("shade: the level's errors") {
	auto err = [](const std::string &extra) {
		try {
			level_of(extra);
		} catch (const rcg::LevelError &e) {
			return std::string(e.what());
		}
		return std::string("no error");
	};
	CHECK(err(R"({"elements": [{"kind": "dark", "side": "inside", "start": [800, 300], "end": [800, 700]}]})").find("not an inside") != std::string::npos);
	CHECK(err(R"({"elements": [{"kind": "dark", "start": [800, 300], "end": [800, 700], "motion": {"keys": [[0, 0, 0, 0]]}}]})").find("does not move")
			!= std::string::npos);
	CHECK(err(R"({"templates": {"glow": {"surfaces": [{}], "shade": {"type": "light", "radius": 3}}}, "elements": []})").find("unknown key 'radius'")
			!= std::string::npos);
	CHECK(err(R"({"templates": {"glow": {"surfaces": [{}], "shade": {"type": "dusk"}}}, "elements": [{"kind": "glow", "start": [800, 300], "end": [800, 700]}]})")
					.find("dark or light") != std::string::npos);
}

TEST_CASE("shade: the fill is the dark minus the light, within the bounds") {
	const rc::arc::SegGeom big = rc::arc::SegGeom::full_from_carrier(rc::arc::from_circle(rc::V2::of(500, 500), 200));
	const rc::arc::SegGeom small = rc::arc::SegGeom::full_from_carrier(rc::arc::from_circle(rc::V2::of(500, 500), 100));
	const std::vector<rcg::shade::Region> regions{rcg::shade::region_of(big, "auto", true), rcg::shade::region_of(small, "auto", false)};
	const rcg::Rect bounds{0, 0, 1920, 1080};
	auto area = [](const rc::vis::Fill &f) {
		double a = 0;
		for (size_t i = 0; i + 2 < f.indices.size(); i += 3) {
			const rc::vis::Pt &p = f.points[f.indices[i]], &q = f.points[f.indices[i + 1]], &r = f.points[f.indices[i + 2]];
			a += std::abs((q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x)) / 2;
		}
		return a;
	};
	const double annulus = 3.14159265358979 * (200.0 * 200 - 100.0 * 100);
	CHECK(area(rcg::shade::fill(regions, {true, true}, bounds)) == doctest::Approx(annulus).epsilon(0.01));
	CHECK(area(rcg::shade::fill(regions, {true, false}, bounds)) == doctest::Approx(3.14159265358979 * 200 * 200).epsilon(0.01));
	CHECK(rcg::shade::fill(regions, {false, true}, bounds).indices.empty());
	// the boundary: the inner circle's piece faces the light inside, the outer one the light outside
	const std::vector<rcg::shade::Piece> pieces = rcg::shade::arrange(regions, bounds);
	CHECK(pieces.size() == 2);
	CHECK(rcg::shade::boundary(pieces, regions, {true, true}, 100).size() == 2);
	CHECK(rcg::shade::boundary(pieces, regions, {true, false}, 100).size() == 1);
	CHECK(rcg::shade::boundary(pieces, regions, {false, true}, 100).empty());
}

TEST_CASE("shade: the player's light -- the distance to the shadow and its bands around a point (Stage 168)") {
	const rc::arc::SegGeom big = rc::arc::SegGeom::full_from_carrier(rc::arc::from_circle(rc::V2::of(500, 500), 200));
	const rc::arc::SegGeom small = rc::arc::SegGeom::full_from_carrier(rc::arc::from_circle(rc::V2::of(500, 500), 100));
	const std::vector<rcg::shade::Region> regions{rcg::shade::region_of(big, "auto", true), rcg::shade::region_of(small, "auto", false)};
	const rcg::shade::Area a = rcg::shade::area(regions, {true, true}, rcg::Rect{0, 0, 1920, 1080});
	CHECK(rcg::shade::distance(a, 650, 500) == 0.0);
	CHECK(rcg::shade::distance(a, 500, 500) == doctest::Approx(100).epsilon(0.01)); // in the light inside
	CHECK(rcg::shade::distance(a, 1000, 500) == doctest::Approx(300).epsilon(0.01));
	auto area = [](const rc::vis::Fill &f) {
		double s = 0;
		for (size_t i = 0; i + 2 < f.indices.size(); i += 3) {
			const rc::vis::Pt &p = f.points[f.indices[i]], &q = f.points[f.indices[i + 1]], &r = f.points[f.indices[i + 2]];
			s += std::abs((q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x)) / 2;
		}
		return s;
	};
	const double pi = 3.14159265358979, annulus = pi * (200.0 * 200 - 100.0 * 100);
	const std::vector<rc::vis::Fill> bands = rcg::shade::around(a, 650, 500, {20, 40});
	REQUIRE(bands.size() == 3);
	CHECK(area(bands[0]) == doctest::Approx(pi * 20 * 20).epsilon(0.02));
	CHECK(area(bands[1]) == doctest::Approx(pi * (40.0 * 40 - 20 * 20)).epsilon(0.02));
	CHECK(area(bands[2]) == doctest::Approx(annulus - pi * 40 * 40).epsilon(0.01));
	// far from the shadow: no bands, the rest is the whole fill
	const std::vector<rc::vis::Fill> far = rcg::shade::around(a, 1500, 500, {20, 40});
	CHECK(far[0].indices.empty());
	CHECK(far[1].indices.empty());
	CHECK(area(far[2]) == doctest::Approx(annulus).epsilon(0.01));
}

TEST_CASE("shade: the player's light -- drawn round them near the shadow, the arrow's rules unchanged (Stage 168)") {
	rcg::Session s(level_of(disk_level("")));
	auto shadow_calls = [&] {
		for (const auto &n : s.draw_frame()) {
			if (n.key.kind == rcg::render::NodeKind::Shadow) {
				return int(std::count_if(n.calls->begin(), n.calls->end(), [](const rcg::render::Call &c) { return c.kind == rcg::render::Kind::Triangles; }));
			}
		}
		return -1;
	};
	CHECK(shadow_calls() == 1); // far from the dark disk: the plain shadow
	s.teleport(rc::V2::of(1150, 620));
	s.settle();
	CHECK(shadow_calls() == 5); // inside it: the veil, three bands, the rest
	// still dark for the arrow: the button stays darkened, a shot from the light stops at the shadow's edge
	const rcg::BuiltSurface *b = s.world().by_id(6);
	REQUIRE(b != nullptr);
	CHECK_FALSE(b->left.has_value());
}

TEST_CASE("shade: the demo level -- the button is in the dark until the lamp is switched on") {
	const std::filesystem::path root = RC_REPO_ROOT;
	rcg::Session s(rcg::load_level((root / "data/levels/objectives/lantern.json").string()));
	s.hold({}, 30);
	shoot(s, 1250, 500);
	CHECK_FALSE(s.flags().has("door:gate")); // in the dark
	shoot(s, 850, 300);
	CHECK(s.flags().is_true("lamp"));
	shoot(s, 1250, 500);
	CHECK(s.flags().is_true("door:gate"));
	s.hold({"move_right"}, 180);
	CHECK(s.won());
}
