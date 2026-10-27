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
	CHECK(dist(body(s), s.player()) < 1e-3); // (Stage 173: at the arrow's origin -- it passes the player)
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

TEST_CASE("a planned stopping surface: a path that hits it is aligned, one that misses it diverges (Stage 162, the user's grapple_gap dumps)") {
	const std::filesystem::path root = RC_REPO_ROOT;
	rcg::Session s(rcg::load_level((root / "data/levels/objectives/grapple_gap.json").string()));
	s.teleport(rc::V2::of(891.676758, 987.919983));
	s.set_cursor(rc::V2::of(1329.523681640625, 478.09521484375)); // the grapple (surface 5), its right side
	s.settle();
	s.click(false);
	REQUIRE(s.plan().size() == 1);
	CHECK(s.plan()[0].surface_id == 5);
	auto types = [&](double x, double y) {
		s.set_cursor(rc::V2::of(x, y));
		s.settle();
		std::vector<rc::StepType> out;
		for (const auto &st : s.preview_steps()) {
			out.push_back(st.type);
		}
		return out;
	};
	auto diverged = [](const std::vector<rc::StepType> &t) {
		return std::any_of(t.begin(), t.end(), [](rc::StepType k) {
			return k == rc::StepType::DivergedPhysical || k == rc::StepType::DivergedPlanned || k == rc::StepType::DivergedPostPlanned;
		});
	};
	CHECK(diverged(types(1348, 260)));       // past the grapple's end: the path misses it
	CHECK_FALSE(diverged(types(1318, 452))); // toward the grapple: the path hits it
}

TEST_CASE("a journey's motion ends its tick at a partial move: no endless loop on a rounding residue (Stage 163, the user's freeze)") {
	// grapple_gap, the grapple planned: at this cursor the previewed grapple's tick left a remainder of 2^-1074,
	// whose quotient underflowed to no move -- the preview spun for ever and the game froze
	const std::filesystem::path root = RC_REPO_ROOT;
	rcg::Session s(rcg::load_level((root / "data/levels/objectives/grapple_gap.json").string()));
	s.teleport(rc::V2::of(891.676758, 987.919983));
	s.set_cursor(rc::V2::of(1329.523681640625, 478.09521484375));
	s.settle();
	s.click(false);
	s.set_cursor(rc::V2::of(1393.7, 222.1));
	s.settle(); // (returns)
	const std::optional<rc::V2> end = s.preview_trip_end();
	REQUIRE(end.has_value());
	shoot(s, 1393.7, 222.1);
	CHECK(s.player().x == end->x); // the live journey ends where the preview said
	CHECK(s.player().y == end->y);
}

TEST_CASE("colliders are drawn in play (Stage 164)") {
	const std::filesystem::path root = RC_REPO_ROOT;
	rcg::Session s(rcg::load_level((root / "data/levels/objectives/grapple_gap.json").string()));
	bool drawn = false;
	for (const auto &n : s.draw_frame()) {
		drawn = drawn || (n.key.kind == rcg::render::NodeKind::Colliders && n.calls->size() == 2);
	}
	CHECK(drawn);
}

TEST_CASE("pull: the box follows a guide along the arrow's path -- hit high or low on the floor, it comes to the player; the preview agrees (Stage 166)") {
	for (const double aim_y : {812.0, 832.0}) { // the box's top half, its bottom half
		CAPTURE(aim_y);
		rcg::Session s(level_of(R"({"gravity": [0, 980], "player": {"start": [700, 827.92]},
			"objects": [{"name": "box", "circle": {"center": [1000, 820], "radius": 16}}]})"));
		s.hold({}, 60); // the box rests on the floor
		const rc::V2 rest = body(s);
		CHECK(rest.y > 820);
		s.set_cursor(rc::V2::of(1000, aim_y));
		s.settle();
		const std::optional<rc::V2> end = s.preview_trip_end();
		REQUIRE(end.has_value());
		shoot(s, 1000, aim_y);
		CHECK(body(s).x == end->x);
		CHECK(body(s).y == end->y);
		CHECK(dist(body(s), s.player()) < 16 + 12 + 1); // at the player, not caught on the floor
	}
}

TEST_CASE("pull and grapple through a portal: the traveller crosses, its copy comes out of the other end; the preview agrees (Stage 166)") {
	const char *portal = R"({"elements": [{"kind": "portal", "start": [800, 440], "end": [800, 640], "d": [300, 0]}],
		"objects": [{"name": "box", "circle": {"center": [1250, 540], "radius": 16}}]})";
	auto plan_portal = [](rcg::Session &s) { // (planned: the walks stay aligned through it -- a journey shows only so)
		s.set_cursor(rc::V2::of(799, 540));
		s.settle();
		s.click(false);
		REQUIRE(s.plan().size() == 1);
	};
	{
		rcg::Session s(level_of(portal));
		plan_portal(s);
		s.set_cursor(rc::V2::of(1250, 540));
		s.settle();
		const std::optional<rc::V2> end = s.preview_trip_end();
		REQUIRE(end.has_value());
		shoot(s, 1250, 540);
		CHECK(body(s).x == end->x);
		CHECK(body(s).x < 800); // back through the portal, on the player's side
		CHECK(dist(body(s), s.player()) < 16 + 12 + 1);
	}
	{
		rcg::Session s(level_of(R"({"elements": [{"kind": "portal", "start": [800, 440], "end": [800, 640], "d": [300, 0]},
			{"kind": "grapple", "start": [1250, 440], "end": [1250, 640]}]})"));
		plan_portal(s);
		s.set_cursor(rc::V2::of(1250, 540));
		s.settle();
		const std::optional<rc::V2> end = s.preview_trip_end();
		REQUIRE(end.has_value());
		shoot(s, 1250, 540);
		CHECK(s.player().x == end->x);
		CHECK(s.player().x > 1100); // through the portal, at the grapple
		CHECK(s.player().x == doctest::Approx(1250 - 12).epsilon(1e-3));
	}
}

// Stage 172 (GAME_SPEC 25.1, 25.5-25.6): platforms -- an object stands on one; a
// pulled object and a grappled player pass one; ending half through it, the
// traveller's centre decides (above: put on top; below: through), in the
// preview as in the shot.

TEST_CASE("platforms: an object falls onto one and rests; a pull brings it down through it") {
	rcg::Session s(level_of(R"({"gravity": [0, 980], "player": {"start": [900, 820]},
		"elements": [{"kind": "platform", "start": [800, 500], "end": [1000, 500]}],
		"objects": [{"name": "box", "circle": {"center": [900, 400], "radius": 16}}]})"));
	s.hold({}, 90);
	CHECK(body(s).y == doctest::Approx(500 - 16 - 0.08).epsilon(1e-6)); // on the platform
	s.set_cursor(rc::V2::of(900, 480));
	s.settle();
	const std::optional<rc::V2> end = s.preview_trip_end();
	REQUIRE(end.has_value());
	shoot(s, 900, 480); // the arrow passes the platform, the box comes down through it to the player
	CHECK(end->x == body(s).x);
	CHECK(end->y == body(s).y);
	CHECK(dist(body(s), s.player()) < 1e-3); // (Stage 173: at the arrow's origin -- it passes the player)
	s.hold({}, 60);
	CHECK(body(s).y == doctest::Approx(840 - 16 - 0.08).epsilon(1e-6)); // and falls to the floor
}

TEST_CASE("platforms: a grapple takes the player up through one; the fall after lands on it") {
	rcg::Session s(level_of(R"({"gravity": [0, 980], "player": {"start": [900, 820]},
		"elements": [{"kind": "platform", "start": [800, 500], "end": [1000, 500]},
			{"kind": "grapple", "start": [850, 300], "end": [950, 300]}]})"));
	s.hold({}, 30);
	s.set_cursor(rc::V2::of(900, 300));
	s.settle();
	const std::optional<rc::V2> end = s.preview_trip_end();
	REQUIRE(end.has_value());
	shoot(s, 900, 300);
	CHECK(end->y == s.player().y);
	CHECK(s.player().y == doctest::Approx(300 + 12 + 0.08).epsilon(1e-4)); // at the grapple, past the platform
	s.hold({}, 120);
	CHECK(s.player().y == doctest::Approx(500 - 24 - 12 - 0.08).epsilon(1e-4)); // fell onto the platform (its feet on it)
	s.hold({"move_down"}, 60);
	CHECK(s.player().y == doctest::Approx(840 - 24 - 12 - 0.08).epsilon(1e-4)); // dropped through to the floor
}

TEST_CASE("platforms: a pull ending half through one -- the centre above is put on top, below stays through") {
	// (the player floating -- no gravity: the start is its head, where the object arrives -- 5 above or below the platform)
	auto pulled = [](double head, double bx, double by) {
		const std::string level = R"({"player": {"start": [900, )" + std::to_string(head) + R"(]},
			"elements": [{"kind": "platform", "start": [800, 500], "end": [1350, 500]}],
			"objects": [{"name": "box", "circle": {"center": [)" + std::to_string(bx) + ", " + std::to_string(by) + R"(], "radius": 16}}]})";
		rcg::Session s(level_of(level.c_str()));
		s.set_cursor(rc::V2::of(bx, by));
		s.settle();
		const std::optional<rc::V2> end = s.preview_trip_end();
		REQUIRE(end.has_value());
		shoot(s, bx, by);
		CHECK(end->x == body(s).x);
		CHECK(end->y == body(s).y);
		return body(s);
	};
	// its centre arrives above the line, overlapping the platform -- put on top
	CHECK(pulled(495, 1300, 560).y == doctest::Approx(500 - 16 - 0.08).epsilon(1e-6));
	// its centre arrives below the line -- left through, where the journey ended
	CHECK(pulled(505, 1300, 560).y == doctest::Approx(505).epsilon(1e-6));
}

TEST_CASE("platforms: the demo level plays to its goal") {
	const std::filesystem::path root = RC_REPO_ROOT;
	rcg::Session s(rcg::load_level((root / "data/levels/objectives/platforms.json").string()));
	s.hold({}, 60);
	CHECK(body(s).y == doctest::Approx(700 - 20 - 0.08).epsilon(1e-6)); // the box fell onto the high platform
	s.hold({"move_up"}, 2); // a jump (on the second tick) up through the low platform, onto it
	s.hold({}, 60);
	CHECK(s.player().y == doctest::Approx(770 - 24 - 12 - 0.08).epsilon(1e-6));
	s.hold({"move_up"}, 2); // and onto the high one
	s.hold({}, 60);
	CHECK(s.player().y == doctest::Approx(700 - 24 - 12 - 0.08).epsilon(1e-6));
	s.hold({"move_down"}, 60); // down through both
	CHECK(s.player().y == doctest::Approx(840 - 24 - 12 - 0.08).epsilon(1e-6));
	shoot(s, 760, 680); // the box pulled down through both onto the plate
	CHECK(body(s).y > 770);
	CHECK(s.flags().is_true("det:plate"));
	CHECK_FALSE(s.won());
	s.hold({"move_right"}, 170);
	shoot(s, s.player().x, 300); // grappled up through the ledge to the hook
	s.hold({}, 60); // and down onto the ledge, in the goal
	CHECK(s.player().y == doctest::Approx(450 - 24 - 12 - 0.08).epsilon(1e-6));
	CHECK(s.won());
}

// Stage 173 (the user): the player is taller and passes objects; a pull ends at the arrow's origin, the
// player's head, so standing on a platform and pulling an object up from below brings it onto the platform

TEST_CASE("tall player: standing on a platform, a pull from below brings the object up onto it") {
	for (const double r : {16.0, 20.0, 25.0}) {
		CAPTURE(r);
		const std::string level = R"({"gravity": [0, 980], "player": {"start": [900, 588]},
			"elements": [{"kind": "platform", "start": [700, 600], "end": [1100, 600]}],
			"objects": [{"name": "box", "circle": {"center": [1050, 700], "radius": )" + std::to_string(r) + "}}]}";
		rcg::Session s(level_of(level.c_str()));
		s.hold({}, 60);
		REQUIRE(body(s).y == doctest::Approx(840 - r - 0.08).epsilon(1e-6)); // the box on the floor below
		REQUIRE(s.player().y == doctest::Approx(600 - 24 - 12 - 0.08).epsilon(1e-6)); // the player on the platform
		shoot(s, 1050, 840 - 2 * r + 4); // down at the box's top
		CHECK(dist(body(s), s.player()) < 1e-3); // at the arrow's origin, over the player's feet
		s.hold({}, 60);
		CHECK(body(s).x == doctest::Approx(900).epsilon(1e-6));
		CHECK(body(s).y == doctest::Approx(600 - r - 0.08).epsilon(1e-6)); // fell onto the platform
	}
}

TEST_CASE("tall player: the player and objects pass each other") {
	rcg::Session s(level_of(R"({"gravity": [0, 980], "player": {"start": [700, 828]},
		"objects": [{"name": "post", "circle": {"center": [900, 810], "radius": 30}, "kind": "static"},
			{"name": "box", "circle": {"center": [1100, 500], "radius": 16}}]})"));
	s.hold({"move_right"}, 120); // through the solid post
	CHECK(s.player().x > 1000.0f);
	s.hold({}, 1);
	s.teleport(rc::V2::of(1100, 816 - 24)); // under the falling box: it falls through the player to the floor
	s.hold({}, 90);
	CHECK(body(s, 1).y == doctest::Approx(840 - 16 - 0.08).epsilon(1e-6));
}
