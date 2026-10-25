// Stage 146 (GAME_SPEC 25.5-25.6, docs/TDD_51): objects, the pull and the
// grapple. A pullable object falls and rests; a pull brings it back along the
// arrow's path to the player, a grapple takes the player along it to the hit;
// either stops at the first thing that blocks the traveller.
#include "rcg/level.hpp"
#include "rcg/model.hpp"
#include "rcg/session.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <string>

namespace {

using json = nlohmann::json;

rcg::Level level_of(const char *extra) {
	json lv = json::parse(R"({"schema": "rc-level/2", "player": {"start": [660, 540]}, "room": {"rect": [560, 240, 800, 600], "built": true},
		"elements": []})");
	const json e = json::parse(extra);
	for (auto it = e.begin(); it != e.end(); ++it) {
		lv[it.key()] = it.value();
	}
	return rcg::parse_level_json(lv);
}

rc::V2 body(const rcg::Session &s, size_t i = 0) { return s.world().bodies().at(i).pos; }

double dist(const rc::V2 &a, const rc::V2 &b) { return std::hypot(double(a.x) - b.x, double(a.y) - b.y); }

void shoot(rcg::Session &s, double x, double y) {
	s.set_cursor(rc::V2::of(x, y));
	s.settle();
	s.fire(-1, 1.0 / 60.0);
}

} // namespace

TEST_CASE("bodies: a pullable object falls and rests, a static one stays; nothing is fired while one falls") {
	rcg::Session s(level_of(R"({"gravity": [0, 980], "objects": [
		{"name": "box", "circle": {"center": [900, 400], "radius": 16}},
		{"name": "post", "circle": {"center": [1000, 400], "radius": 16}, "kind": "static"}]})"));
	REQUIRE(s.world().bodies().size() == 2);
	CHECK(s.world().bodies()[0].id > 4); // after the level's surfaces
	s.hold({}, 1);
	CHECK(body(s).y > 400);
	s.set_cursor(rc::V2::of(1200, 300));
	CHECK_FALSE(s.start_fire()); // the box is falling
	s.hold({}, 90);
	CHECK(body(s).y == doctest::Approx(840 - 16 - 0.08).epsilon(1e-4)); // on the floor
	CHECK(body(s, 1).y == 400);
	s.hold({}, 1); // at rest: no structural change
	CHECK(s.start_fire());
	while (s.flying()) {
		s.advance(1.0 / 60.0, {});
	}
	s.key(rcg::Action::Reset);
	CHECK(body(s).y == 400); // the initial world
}

TEST_CASE("pull: a shot at a pullable object brings it back along the path, to the player") {
	rcg::Session s(level_of(R"({"objects": [{"name": "box", "circle": {"center": [1100, 540], "radius": 16}}]})"));
	s.set_cursor(rc::V2::of(1100, 540));
	s.settle();
	REQUIRE(s.start_fire());
	int frames = 0;
	bool journeyed = false;
	while (s.flying() && frames < 1000) {
		s.advance(1.0 / 60.0, {});
		journeyed = journeyed || (s.flying() && body(s).x < 1100);
		frames++;
	}
	CHECK(journeyed); // the game waits for the journey as for a flight
	CHECK(dist(body(s), s.player()) == doctest::Approx(12 + 16 + 0.08).epsilon(1e-3)); // touching the player
	CHECK(body(s).y == doctest::Approx(540).epsilon(1e-4));
	s.key(rcg::Action::Undo);
	CHECK(body(s).x == 1100); // the checkpoint holds the bodies
}

TEST_CASE("pull and grapple: a fence stops the traveller, a pass-through does not") {
	{
		rcg::Session s(level_of(R"({"elements": [{"kind": "fence", "start": [900, 300], "end": [900, 800]}],
			"objects": [{"name": "box", "circle": {"center": [1100, 540], "radius": 16}}]})"));
		shoot(s, 1100, 540); // the arrow passes the fence, the box does not
		CHECK(body(s).x == doctest::Approx(900 + 16 + 0.08).epsilon(1e-4));
	}
	{
		rcg::Session s(level_of(R"({"elements": [{"kind": "grapple", "start": [1200, 300], "end": [1200, 800]},
			{"kind": "passthrough", "start": [900, 300], "end": [900, 800]}]})"));
		shoot(s, 1200, 540);
		CHECK(s.player().x == doctest::Approx(1200 - 12 - 0.08).epsilon(1e-4)); // at the grapple
		CHECK(s.player().y == doctest::Approx(540).epsilon(1e-4));
	}
	{
		rcg::Session s(level_of(R"({"elements": [{"kind": "grapple", "start": [1200, 300], "end": [1200, 800]},
			{"kind": "fence", "start": [900, 300], "end": [900, 800]}]})"));
		shoot(s, 1200, 540);
		CHECK(s.player().x == doctest::Approx(900 - 12 - 0.08).epsilon(1e-4)); // the fence holds the player
	}
}

TEST_CASE("objects: a detector for objects sees a pulled object pass") {
	rcg::Session s(level_of(R"({"objects": [{"name": "box", "circle": {"center": [1200, 540], "radius": 20}}],
		"detectors": [{"name": "plate", "for": "object", "region": {"rect": [760, 500, 80, 80]}, "mode": "enter"}]})"));
	CHECK(s.flags().get("det:plate") == nullptr);
	shoot(s, 1200, 540);
	REQUIRE(s.flags().get("det:plate") != nullptr);
	CHECK(std::get<bool>(*s.flags().get("det:plate")));
}

TEST_CASE("objects: the demo levels play to their goals") {
	const std::filesystem::path root = RC_REPO_ROOT;
	{
		rcg::Session s(rcg::load_level((root / "data/levels/objectives/pull_box.json").string()));
		shoot(s, 1200, 540); // the box comes back over the plate
		s.hold({"move_down"}, 70);
		CHECK(s.won());
	}
	{
		rcg::Session s(rcg::load_level((root / "data/levels/objectives/grapple_gap.json").string()));
		s.hold({}, 30); // standing on the left platform
		shoot(s, 1340, 450);
		s.hold({}, 60); // the fall onto the right platform
		CHECK(s.won());
	}
}

TEST_CASE("objects: the schema") {
	auto err = [](const char *extra) {
		try {
			level_of(extra);
		} catch (const rcg::LevelError &e) {
			return std::string(e.what());
		}
		return std::string("no error");
	};
	CHECK(err(R"({"objects": [{"name": "a", "circle": {"center": [0, 0], "radius": 0}}]})").find("a positive radius") != std::string::npos);
	CHECK(err(R"({"objects": [{"name": "a", "circle": {"center": [0, 0]}, "face": "glass"}]})").find("wall, mirror or pass") != std::string::npos);
	CHECK(err(R"({"objects": [{"name": "a", "circle": {"center": [0, 0]}}, {"name": "a", "circle": {"center": [9, 0]}}]})").find("names another object")
			!= std::string::npos);
	CHECK(err(R"({"objects": [{"name": "a", "circle": {"center": [0, 0]}, "kind": "heavy"}]})").find("pullable or static") != std::string::npos);
}

TEST_CASE("bubbles: a bubble floats, carries a box, and the arrow pops it (Stage 150)") {
	rcg::Session s(level_of(R"({"gravity": [0, 980], "player": {"start": [700, 827.92]}, "objects": [
		{"name": "bubble", "circle": {"center": [900, 500], "radius": 24}, "gravity": -0.5, "pops": true, "face": "pass", "solid": false,
		 "carries": "box"},
		{"name": "box", "circle": {"center": [900, 540], "radius": 14}}]})"));
	s.hold({}, 120);
	CHECK(body(s).y == doctest::Approx(240 + 24 + 0.08).epsilon(1e-4)); // up against the ceiling
	CHECK(body(s, 1).y == doctest::Approx(240 + 24 + 0.08 + 40).epsilon(1e-4)); // the box held under it
	shoot(s, 880, 262); // its left side, clear of the box below it
	CHECK(s.flags().is_true("obj:bubble:popped"));
	CHECK_FALSE(rcg::present(s.world().bodies()[0].built, s.flags())); // gone
	s.hold({}, 120);
	CHECK(body(s, 1).y == doctest::Approx(840 - 14 - 0.08).epsilon(1e-4)); // the box fell
	s.key(rcg::Action::Undo);
	CHECK_FALSE(s.flags().is_true("obj:bubble:popped")); // the bubble back
}

TEST_CASE("bubbles: the demo level plays to its goal; the schema") {
	const std::filesystem::path root = RC_REPO_ROOT;
	rcg::Session s(rcg::load_level((root / "data/levels/objectives/bubble_drop.json").string()));
	s.hold({}, 90);
	shoot(s, 980, 262); // the bubble's left side, clear of the box
	s.hold({}, 90); // the box falls onto the plate
	CHECK(s.flags().is_true("det:plate"));
	s.hold({"move_left"}, 60); // to the goal, away from the box
	CHECK(s.won());
	auto err = [](const char *extra) {
		try {
			level_of(extra);
		} catch (const rcg::LevelError &e) {
			return std::string(e.what());
		}
		return std::string("no error");
	};
	CHECK(err(R"({"objects": [{"name": "a", "circle": {"center": [0, 0]}, "carries": "nobody"}]})").find("no other object") != std::string::npos);
	CHECK(err(R"({"objects": [{"name": "a", "circle": {"center": [0, 0]}, "carries": "b"}, {"name": "b", "circle": {"center": [9, 0]}, "carries": "c"},
		{"name": "c", "circle": {"center": [19, 0]}}]})").find("one level of carrying") != std::string::npos);
}

TEST_CASE("pull and grapple: the preview shows where the traveller ends -- the same place as the shot, a fence stopping it on the way (Stage 159)") {
	auto previewed_then_shot = [](const char *level, double x, double y, bool player) {
		rcg::Session s(level_of(level));
		s.set_cursor(rc::V2::of(x, y));
		s.settle();
		const std::optional<rc::V2> end = s.preview_trip_end();
		REQUIRE(end.has_value());
		shoot(s, x, y);
		const rc::V2 got = player ? s.player() : body(s);
		CHECK(end->x == got.x);
		CHECK(end->y == got.y);
		return *end;
	};
	// a grapple, free and with a fence on the way (the arrow passes it, the player does not)
	CHECK(previewed_then_shot(R"({"elements": [{"kind": "grapple", "start": [1200, 300], "end": [1200, 800]}]})", 1200, 540, true).x
			== doctest::Approx(1200 - 12 - 0.08).epsilon(1e-4));
	CHECK(previewed_then_shot(R"({"elements": [{"kind": "grapple", "start": [1200, 300], "end": [1200, 800]},
		{"kind": "fence", "start": [900, 300], "end": [900, 800]}]})", 1200, 540, true).x == doctest::Approx(900 - 12 - 0.08).epsilon(1e-4));
	// a pull, stopped by a fence
	CHECK(previewed_then_shot(R"({"elements": [{"kind": "fence", "start": [900, 300], "end": [900, 800]}],
		"objects": [{"name": "box", "circle": {"center": [1100, 540], "radius": 16}}]})", 1100, 540, false).x
			== doctest::Approx(900 + 16 + 0.08).epsilon(1e-4));
}

TEST_CASE("preview: the shot's lasting changes to the divergence -- a door a button opens (Stage 159)") {
	rcg::Session s(level_of(R"({"elements": [{"kind": "button", "name": "b", "opens": "gate", "op": "set", "start": [1000, 400], "end": [1000, 700]},
		{"kind": "door", "name": "gate", "start": [1200, 300], "end": [1200, 800]}]})"));
	s.set_cursor(rc::V2::of(1000, 540));
	s.settle();
	REQUIRE(s.preview_predicted().has_value());
	CHECK(s.preview_predicted()->is_true("door:gate"));
	CHECK_FALSE(s.flags().has("door:gate"));
	bool drawn = false;
	for (const auto &n : s.draw_frame()) {
		drawn = drawn || n.key.kind == rcg::render::NodeKind::Glow;
	}
	CHECK(drawn);
}
