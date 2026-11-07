// Stage 146 (GAME_SPEC 25.5-25.6, docs/TDD_51): objects, the pull and the
// grapple. A pullable object falls and rests; a pull brings it back along the
// arrow's path to the player, a grapple takes the player along it to the hit;
// either stops at the first thing that blocks the traveller.
#include "rcg/build.hpp"
#include "rcg/level.hpp"
#include "rcg/model.hpp"
#include "rcg/render.hpp"
#include "rcg/session.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>

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

TEST_CASE("pull and grapple: turned aside by what it brushes past, the traveller bends smoothly -- never into it, the end exact; the preview agrees (Stage 178)") {
	// a wall's end 5 px below the grapple's line: the player's circle (12) slides round it
	rcg::Session s(level_of(R"({"elements": [{"kind": "grapple", "start": [1200, 300], "end": [1200, 800]},
		{"kind": "wall", "start": [900, 545], "end": [900, 800]}]})"));
	s.set_cursor(rc::V2::of(1200, 540));
	s.settle();
	const std::optional<rc::V2> end = s.preview_trip_end();
	REQUIRE(end.has_value());
	REQUIRE(s.start_fire());
	std::vector<rc::V2> at{s.player()};
	for (int frames = 0; s.flying() && frames < 1000; frames++) {
		s.advance(1.0 / 60.0, {});
		if (dist(s.player(), at.back()) > 1e-6) {
			at.push_back(s.player());
		}
	}
	REQUIRE(at.size() > 10);
	double turn = 0.0, clear = INFINITY, lifted = 0.0; // the largest turn between two frames' moves; the nearest it came to the wall's end
	for (size_t i = 1; i < at.size(); i++) {
		clear = std::min(clear, dist(at[i], rc::V2::of(900, 545)));
		lifted = std::max(lifted, 540.0 - at[i].y);
		if (i >= 2) {
			const double ax = double(at[i - 1].x) - at[i - 2].x, ay = double(at[i - 1].y) - at[i - 2].y;
			const double bx = double(at[i].x) - at[i - 1].x, by = double(at[i].y) - at[i - 1].y;
			turn = std::max(turn, std::abs(std::atan2(ax * by - ay * bx, ax * bx + ay * by)));
		}
	}
	CHECK(lifted > 3.0);   // it did go round the wall's end
	CHECK(turn < 0.15);    // (unsmoothed: about 0.8 rad in one frame, back toward the path)
	CHECK(clear >= 12 + 0.08 - 1e-3); // never into the wall
	CHECK(s.player().x == doctest::Approx(1200 - 12 - 0.08).epsilon(1e-4)); // at the grapple
	CHECK(s.player().y == doctest::Approx(540).epsilon(1e-4));
	CHECK(end->x == s.player().x);
	CHECK(end->y == s.player().y);
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

TEST_CASE("platforms: a session engages one, hangs, keeps hanging through a shot and its undo (Stage 185)") {
	// standing on the floor (the feet's at 840) with its feet at a rope
	rcg::Session s(level_of(R"({"gravity": [0, 980], "player": {"start": [900, 820]},
		"elements": [{"kind": "platform", "start": [900, 500], "end": [900, 840]}]})"));
	const float floor = float(840 - 24 - 12 - 0.08);
	s.hold({}, 30);
	CHECK(s.player().y == doctest::Approx(floor).epsilon(1e-4)); // not moving: nothing
	s.hold({"move_up"}, 3);
	const rc::V2 hang = s.player();
	CHECK(hang.y == doctest::Approx(floor - 10.0).epsilon(1e-3)); // the speed without gravity, not a jump
	s.hold({}, 60);
	CHECK(s.player() == hang);
	shoot(s, 1200, 100);
	CHECK(s.player() == hang);
	s.hold({}, 30);
	CHECK(s.player() == hang);
	s.key(rcg::Action::Undo);
	s.settle();
	CHECK(s.player() == hang);
	s.hold({}, 60);
	CHECK(s.player() == hang); // still engaged: no fall after the undo
	s.hold({"move_down"}, 30);
	CHECK(s.player().y == doctest::Approx(floor).epsilon(1e-4));
}

TEST_CASE("shields: drawn unlike a wall -- a turquoise core and band, a plain side of hit effects or the player's stop keeps its colour (Stage 186)") {
	rcg::Session s(level_of(R"({"gravity": [0, 980], "elements": [
		{"kind": "wall", "start": [200, 100], "end": [200, 300]},
		{"kind": "shield", "start": [400, 100], "end": [400, 300]},
		{"kind": "shield", "start": [600, 100], "via": [650, 200], "end": [600, 300]},
		{"kind": "target", "face": "terminal", "start": [800, 100], "end": [800, 300]}]})"));
	s.settle();
	const rcg::render::Color core{0.45f, 1.0f, 0.85f, 1.0f}, band{0.45f, 1.0f, 0.85f, 0.22f}, red = rcg::render::hex(0xFF0000FF);
	std::set<int> shields;
	int cores = 0, bands = 0, reds = 0;
	for (const rcg::render::Node &n : s.draw()) {
		for (const rcg::render::Call &c : n.calls) {
			if (c.kind != rcg::render::Kind::Line && c.kind != rcg::render::Kind::Arc) {
				continue;
			}
			const rcg::render::Color k = c.colors.at(0);
			if (k == core) {
				shields.insert(n.key[1].get<int>()); // (an arc is drawn in pieces)
			}
			cores += k == core;
			bands += k == band;
			reds += k == red;
		}
	}
	CHECK(shields.size() == 2); // not the wall, not the target
	CHECK(bands == cores);
	CHECK(reds >= 2); // the wall, the target
}

TEST_CASE("crossings: by what an element does -- a door or a switchable that can stop the arrow cuts, a platform or a fence does not (Stage 192)") {
	const auto cuts = [](const char *element) {
		const std::string text = std::string(R"({"elements": [{"kind": "mirror", "name": "m", "start": [900, 300], "end": [900, 700]}, )") + element + "]}";
		rcg::Session s(level_of(text.c_str()));
		for (const rcg::BuiltSurface &b : s.world().surfaces()) {
			if (b.label == "Mirror") {
				return s.world().pieces().of(b.id).size() - 1;
			}
		}
		FAIL("no mirror");
		return size_t(0);
	};
	CHECK(cuts(R"({"kind": "platform", "start": [800, 500], "end": [1000, 500]})") == 0);
	CHECK(cuts(R"({"kind": "fence", "start": [800, 500], "end": [1000, 500]})") == 0);
	CHECK(cuts(R"({"kind": "passthrough", "start": [800, 500], "end": [1000, 500]})") == 0);
	CHECK(cuts(R"({"kind": "switchable", "flag": "f", "on": "pass", "off": "pass", "start": [800, 500], "end": [1000, 500]})") == 0);
	CHECK(cuts(R"({"kind": "switchable", "flag": "f", "on": "pass", "off": "wall", "start": [800, 500], "end": [1000, 500]})") == 1);
	CHECK(cuts(R"({"kind": "door", "start": [800, 500], "end": [1000, 500]})") == 1);
	CHECK(cuts(R"({"kind": "shield", "start": [800, 500], "end": [1000, 500]})") == 1);
	CHECK(cuts(R"({"kind": "button", "opens": "x", "start": [800, 500], "end": [1000, 500]})") == 1);
}

TEST_CASE("inert surfaces: the session's solver is not told of them, and a mirror's end on a platform is a plain graze (Stage 193)") {
	const auto preview = [](bool with_platform) {
		const std::string text = std::string(R"({"player": {"start": [700, 500]}, "elements": [
			{"kind": "mirror", "name": "m", "start": [900, 300], "end": [900, 500]})") +
			(with_platform ? R"(, {"kind": "platform", "start": [900, 500], "end": [1100, 500]})" : "") + "]}";
		rcg::Session s(level_of(text.c_str()));
		s.set_cursor(rc::V2::of(1000, 500)); // straight through the mirror's end
		s.settle();
		size_t inert = 0;
		for (const rcg::BuiltSurface &b : s.world().surfaces()) {
			inert += rcg::is_inert(b);
		}
		CHECK(s.world().defs().size() == s.world().surfaces().size() - inert);
		return s.record(nlohmann::json::object())["preview"];
	};
	CHECK(preview(true) == preview(false));
}

TEST_CASE("doors: a door in line with a wall shows where it stops -- posts across its ends, a core; an open one keeps its posts (Stage 190)") {
	const auto posts = [](bool open) { // the amber strokes across the line: {x, alpha}
		const std::string level = std::string(R"({"gravity": [0, 980], "flags": {"open_it": )") + (open ? "true" : "false") + R"(}, "elements": [
			{"kind": "wall", "start": [500, 300], "end": [800, 300]},
			{"kind": "door", "name": "d", "flag": "open_it", "start": [800, 300], "end": [1000, 300]},
			{"kind": "wall", "start": [1000, 300], "end": [1300, 300]}]})";
		rcg::Session s(level_of(level.c_str()));
		s.settle();
		std::vector<std::pair<float, float>> out;
		for (const rcg::render::Node &n : s.draw()) {
			for (const rcg::render::Call &c : n.calls) {
				if (c.kind == rcg::render::Kind::Line && c.colors.at(0)[0] == 1.0f && c.colors.at(0)[1] == 0.72f && c.pts[0].x == c.pts[1].x) {
					out.push_back({c.pts[0].x, c.colors.at(0)[3]});
				}
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	};
	CHECK(posts(false) == std::vector<std::pair<float, float>>{{800.0f, 1.0f}, {1000.0f, 1.0f}});
	CHECK(posts(true) == std::vector<std::pair<float, float>>{{800.0f, 0.5f}, {1000.0f, 0.5f}});
}

// Stage 188 (the user): no shot while the player physically moves; the preview muted, brightening once it is still

namespace {
float path_alpha(const rcg::Session &s) {
	for (const rcg::render::Node &n : s.draw()) {
		if (n.key == nlohmann::json::array({"path"})) {
			return n.modulate[3];
		}
	}
	FAIL("no path node");
	return 0.0f;
}
const char *const FLOORED = R"({"gravity": [0, 980], "player": {"start": [900, 820]},
	"elements": [{"kind": "wall", "start": [1000, 700], "end": [1000, 840]}]})";
} // namespace

TEST_CASE("firing: refused from the first tick of walking, allowed after standing still; the preview dims and brightens by ticks") {
	rcg::Session s(level_of(FLOORED));
	s.hold({}, 60);
	s.set_cursor(rc::V2::of(600, 300));
	s.settle();
	REQUIRE(s.can_fire());
	CHECK(path_alpha(s) == 1.0f);
	// walking: at once no shot, and the preview falls to muted over READY_FALL ticks, still shown
	s.hold({"move_left"}, 1);
	CHECK_FALSE(s.can_fire());
	CHECK_FALSE(s.start_fire());
	CHECK_FALSE(s.click(false)); // (a click over nothing fires no more than the fire action)
	CHECK_FALSE(s.flying());
	s.settle();
	float last = path_alpha(s);
	CHECK(last < 1.0f);
	CHECK(last > float(rcg::Session::READY_MUTED));
	for (int i = 1; i < rcg::Session::READY_FALL; i++) {
		s.hold({"move_left"}, 1);
		s.settle();
		CHECK(path_alpha(s) < last);
		last = path_alpha(s);
	}
	CHECK(last == doctest::Approx(rcg::Session::READY_MUTED));
	s.hold({"move_left"}, 30);
	s.settle();
	CHECK(path_alpha(s) == doctest::Approx(rcg::Session::READY_MUTED)); // always visible while moving
	// a plan is still edited while moving; only the shot waits
	CHECK_FALSE(s.can_fire());
	// released: it slows to a stop (still moving), stays still a moment, then brightens, and only full is ready
	int stop = 0;
	rc::V2 at = s.player();
	for (; stop < 60; stop++) {
		s.hold({}, 1);
		if (s.player() == at) {
			break;
		}
		at = s.player();
		CHECK_FALSE(s.can_fire());
	}
	REQUIRE(stop < 60);
	for (int i = 1; i < rcg::Session::READY_SETTLE; i++) { // (the tick that broke the loop was the first still one)
		s.hold({}, 1);
		s.settle();
		CHECK(path_alpha(s) == doctest::Approx(rcg::Session::READY_MUTED));
	}
	last = path_alpha(s);
	for (int i = 0; i < rcg::Session::READY_RISE + 1 && !s.can_fire(); i++) {
		CHECK_FALSE(s.start_fire());
		s.hold({}, 1);
		s.settle();
		CHECK(path_alpha(s) > last);
		last = path_alpha(s);
	}
	CHECK(s.can_fire());
	CHECK(path_alpha(s) == 1.0f);
	CHECK(s.start_fire());
	CHECK(s.flying());
}

TEST_CASE("firing: movement is physical -- walking into a wall is still, a free fall moves, hanging on a platform is still") {
	SUBCASE("a wall: right held against it, it may fire") {
		rcg::Session s(level_of(FLOORED));
		s.hold({"move_right"}, 120);
		const rc::V2 at = s.player();
		CHECK(at.x == doctest::Approx(1000 - 12 - 0.08).epsilon(1e-4));
		s.hold({"move_right"}, 30);
		CHECK(s.player() == at);
		CHECK(s.can_fire());
	}
	SUBCASE("a free fall: nothing held, it moves; on landing it settles") {
		rcg::Session s(level_of(R"({"gravity": [0, 980], "player": {"start": [900, 100]}})"));
		s.hold({}, 2);
		CHECK_FALSE(s.can_fire());
		s.set_cursor(rc::V2::of(600, 300));
		s.settle();
		CHECK_FALSE(s.start_fire());
		s.hold({}, 20);
		CHECK_FALSE(s.can_fire());
		s.hold({}, 120);
		CHECK(s.can_fire());
	}
	SUBCASE("hanging on a rope, up released: it may fire") {
		rcg::Session s(level_of(R"({"gravity": [0, 980], "player": {"start": [900, 820]},
			"elements": [{"kind": "platform", "start": [900, 500], "end": [900, 840]}]})"));
		s.hold({"move_up"}, 30);
		CHECK_FALSE(s.can_fire());
		s.hold({}, 20);
		CHECK(s.can_fire());
	}
	SUBCASE("top-down: walking moves, standing does not") {
		rcg::Session s(level_of(R"({"player": {"start": [900, 500]}})"));
		CHECK(s.can_fire());
		s.hold({"move_up"}, 2);
		CHECK_FALSE(s.can_fire());
		s.hold({}, 20);
		CHECK(s.can_fire());
	}
	SUBCASE("a grapple's journey is movement: it settles after it") {
		rcg::Session s(level_of(R"({"gravity": [0, 980], "player": {"start": [900, 820]},
			"elements": [{"kind": "platform", "start": [800, 500], "end": [1000, 500]},
				{"kind": "grapple", "start": [850, 300], "end": [950, 300]}]})"));
		s.hold({}, 30);
		shoot(s, 900, 300);
		CHECK_FALSE(s.can_fire());
		s.hold({}, 120);
		CHECK(s.can_fire());
	}
	SUBCASE("a shot that moves nothing leaves it ready for the next; an undo too") {
		rcg::Session s(level_of(FLOORED));
		s.hold({}, 60);
		shoot(s, 600, 300);
		CHECK(s.can_fire());
		s.key(rcg::Action::Undo);
		s.settle();
		CHECK(s.can_fire());
	}
}

TEST_CASE("firing: on every level a player left alone comes to rest, exactly, and may fire") {
	const std::filesystem::path root = RC_REPO_ROOT;
	for (const auto &e : std::filesystem::recursive_directory_iterator(root / "data/levels")) {
		if (e.path().extension() != ".json") {
			continue;
		}
		rcg::Session s(rcg::load_level(e.path().string()));
		s.hold({}, 900);
		INFO(e.path().string());
		CHECK(s.can_fire());
	}
}

// Stage 194 (the user): a shot that went as planned clears its plan; an undo brings it back, a reset can be undone, a redo lasts until anything changes

namespace {
// floating at (900, 500), a mirror ahead: `half` tall either way of y = 500, planned at its middle
rcg::Session planned_at_mirror(double half) {
	const std::string text = R"({"player": {"start": [900, 500]}, "elements": [{"kind": "mirror", "name": "m", "start": [1100, )" + std::to_string(500 - half) +
		R"(], "end": [1100, )" + std::to_string(500 + half) + R"(]}]})";
	rcg::Session s(level_of(text.c_str()));
	s.set_cursor(rc::V2::of(1098, 500));
	s.settle();
	s.click(false);
	s.settle();
	REQUIRE(s.plan().size() == 1);
	return s;
}
} // namespace

TEST_CASE("a shot's plan: cleared when it went as planned, kept when it diverged") {
	SUBCASE("as planned: the reflection lands on the mirror, the plan is done") {
		rcg::Session s = planned_at_mirror(200);
		shoot(s, 1000, 400);
		s.settle();
		CHECK(s.plan().empty());
	}
	SUBCASE("diverged: the hit its cursor needs is off the short mirror, the plan stays") {
		rcg::Session s = planned_at_mirror(20);
		const std::vector<rcg::PlanEntry> before = s.plan();
		shoot(s, 1000, 100);
		s.settle();
		CHECK(s.plan() == before);
	}
	SUBCASE("no plan: nothing to clear, and no trouble") {
		rcg::Session s(level_of(R"({"player": {"start": [900, 500]}})"));
		shoot(s, 1000, 400);
		s.settle();
		CHECK(s.plan().empty());
	}
}

TEST_CASE("undo and redo: exactly the state before the shot, with its plan; exactly the state after it") {
	rcg::Session s = planned_at_mirror(200);
	s.set_cursor(rc::V2::of(1000, 400));
	s.settle();
	const nlohmann::json before = s.record(nlohmann::json::object());
	const std::vector<rcg::PlanEntry> plan = s.plan();
	shoot(s, 1000, 400);
	s.settle();
	const nlohmann::json after = s.record(nlohmann::json::object());
	REQUIRE(s.plan().empty());
	for (int round = 0; round < 2; round++) { // (there and back, twice)
		s.key(rcg::Action::Undo);
		s.settle();
		CHECK(s.plan() == plan);
		CHECK(s.record(nlohmann::json::object()) == before);
		CHECK(s.redo_depth() == 1);
		s.key(rcg::Action::Redo);
		s.settle();
		CHECK(s.record(nlohmann::json::object()) == after);
		CHECK(s.redo_depth() == 0);
	}
	s.key(rcg::Action::Redo); // (nothing more to redo)
	s.settle();
	CHECK(s.record(nlohmann::json::object()) == after);
}

TEST_CASE("undo: a reset is undone like a shot; a reset of nothing is nothing to undo") {
	rcg::Session s(level_of(FLOORED));
	s.hold({}, 60);
	s.key(rcg::Action::Reset); // the fall from the start was a change
	s.hold({}, 60);
	const rc::V2 rest = s.player();
	s.hold({"move_left"}, 30);
	s.hold({}, 30);
	const rc::V2 walked = s.player();
	REQUIRE(walked.x < rest.x);
	s.set_cursor(rc::V2::of(600, 300));
	s.settle();
	s.click(false); // (a shot from there)
	s.fly(-1, 1.0 / 60.0);
	s.settle();
	const nlohmann::json shot = s.record(nlohmann::json::object());
	const int depth = shot["checkpoints"].get<int>();
	s.key(rcg::Action::Reset);
	s.settle();
	CHECK(s.player() != walked);
	CHECK(s.record(nlohmann::json::object())["checkpoints"] == depth + 1);
	const nlohmann::json reset = s.record(nlohmann::json::object());
	s.key(rcg::Action::Undo);
	s.settle();
	CHECK(s.record(nlohmann::json::object()) == shot); // back to before the reset
	s.key(rcg::Action::Redo);
	s.settle();
	CHECK(s.record(nlohmann::json::object()) == reset); // and the reset again
	s.key(rcg::Action::Undo);
	s.key(rcg::Action::Undo); // and on, to before the shot
	s.settle();
	CHECK(s.player() == walked);
	// a reset of an untouched level
	rcg::Session fresh(level_of(R"({"player": {"start": [900, 500]}})"));
	fresh.settle();
	const nlohmann::json start = fresh.record(nlohmann::json::object());
	fresh.key(rcg::Action::Reset);
	fresh.settle();
	CHECK(fresh.record(nlohmann::json::object())["checkpoints"] == start["checkpoints"]);
}

TEST_CASE("redo: lasts while nothing in the game changes, and goes at the slightest change") {
	const auto undone = [] {
		rcg::Session s(level_of(FLOORED));
		s.hold({}, 60);
		shoot(s, 600, 300);
		s.key(rcg::Action::Undo);
		s.settle();
		REQUIRE(s.redo_depth() == 1);
		return s;
	};
	SUBCASE("what changes nothing: time standing still, the cursor, the display, walking into nothing's way") {
		rcg::Session s = undone();
		s.hold({}, 120);
		s.set_cursor(rc::V2::of(700, 200));
		s.settle();
		s.key(rcg::Action::CycleDisplay);
		s.key(rcg::Action::ToggleVisibility);
		s.key(rcg::Action::ClearPlan); // (no plan to clear)
		s.hold({}, 10);
		CHECK(s.redo_depth() == 1);
		s.key(rcg::Action::Redo);
		CHECK(s.redo_depth() == 0);
	}
	SUBCASE("what moves by itself, a moving surface: no change, as the cursor's move is none") {
		const std::filesystem::path root = RC_REPO_ROOT;
		rcg::Session s(rcg::load_level((root / "data/levels/objectives/timing_gate.json").string()));
		s.hold({}, 120); // (at rest)
		s.settle();
		shoot(s, 600, 300);
		s.settle();
		const nlohmann::json shot = s.record(nlohmann::json::object());
		s.key(rcg::Action::Undo);
		s.settle();
		const std::string defs = s.record(nlohmann::json::object())["world"]["defs"];
		s.hold({}, 45);
		s.settle();
		CHECK(s.record(nlohmann::json::object())["world"]["defs"] != defs); // the surfaces did move
		REQUIRE(s.redo_depth() == 1);
		s.key(rcg::Action::Redo);
		s.settle();
		CHECK(s.record(nlohmann::json::object())["world"]["defs"] == shot["world"]["defs"]); // back to the state after the shot, the phase with it
		CHECK(s.record(nlohmann::json::object())["checkpoints"] == shot["checkpoints"]);
	}
	SUBCASE("one tick's walk") {
		rcg::Session s = undone();
		s.hold({"move_left"}, 1);
		CHECK(s.redo_depth() == 0);
	}
	SUBCASE("a plan's change") {
		rcg::Session s(level_of(R"({"player": {"start": [900, 500]}, "elements": [{"kind": "mirror", "start": [1100, 300], "end": [1100, 700]}]})"));
		s.settle();
		shoot(s, 1000, 200);
		s.key(rcg::Action::Undo);
		s.settle();
		REQUIRE(s.redo_depth() == 1);
		s.set_cursor(rc::V2::of(1098, 500));
		s.settle();
		s.click(false);
		s.settle();
		const rc::V2 at = s.player();
		s.key(rcg::Action::Redo);
		s.settle();
		CHECK(s.redo_depth() == 0);
		CHECK(s.plan().size() == 1); // (no redo happened)
		CHECK(s.player() == at);
	}
	SUBCASE("a new shot") {
		rcg::Session s = undone();
		shoot(s, 600, 300);
		CHECK(s.redo_depth() == 0);
	}
	SUBCASE("a reset") {
		rcg::Session s = undone();
		s.key(rcg::Action::Reset);
		CHECK(s.redo_depth() == 0);
	}
	SUBCASE("gravity toggled, and an undo restores it") {
		rcg::Session s(level_of(FLOORED));
		s.hold({}, 60);
		shoot(s, 600, 300);
		const rc::V2 at = s.player();
		s.key(rcg::Action::ToggleGravity);
		s.key(rcg::Action::Undo);
		s.settle();
		CHECK(s.player() == at);
		s.hold({}, 30);
		CHECK(s.player() == at); // standing, as it was: not floating or folded
	}
}

// Stage 195 (the user): the preview to the cursor, a fade past it; past a divergence the physical path fades from there

namespace {
// The path node's strokes: every point with its colour's alpha.
struct DrawnPoint {
	rc::V2 at;
	rcg::render::Color color;
};
std::vector<DrawnPoint> path_points(const rcg::Session &s) {
	std::vector<DrawnPoint> out;
	for (const rcg::render::Node &n : s.draw()) {
		if (n.key != nlohmann::json::array({"path"})) {
			continue;
		}
		for (const rcg::render::Call &c : n.calls) {
			if (c.colors.empty() || (c.kind != rcg::render::Kind::Line && c.kind != rcg::render::Kind::Multiline)) {
				continue;
			}
			for (const rc::V2 &p : c.pts) {
				out.push_back({p, c.colors[0]});
			}
		}
	}
	return out;
}
constexpr double REACH = 220.2725; // a tenth of the default level's diagonal (1920 x 1080)
} // namespace

TEST_CASE("preview: shown to the cursor, fading out a tenth of the level's diagonal past it; nothing further") {
	const char *level = R"({"player": {"start": [400, 500]}, "elements": [{"kind": "wall", "start": [1500, 300], "end": [1500, 700]}]})";
	auto aimed = [&](rcg::SessionConfig cfg) {
		rcg::Session s(level_of(level), cfg);
		s.set_cursor(rc::V2::of(700, 500));
		s.settle();
		return s;
	};
	SUBCASE("by default") {
		const rcg::Session s = aimed({});
		float max_x = 0, before = 1, at_half = -1;
		for (const DrawnPoint &p : path_points(s)) {
			max_x = std::max(max_x, p.at.x);
			if (p.at.x < 690) {
				before = std::min(before, p.color[3]);
			}
			if (std::abs(double(p.at.x) - (700 + REACH / 2)) < 8) {
				at_half = p.color[3];
			}
		}
		CHECK(before == 1.0f); // whole to the cursor
		CHECK(at_half == doctest::Approx(0.5).epsilon(0.1)); // half way out
		CHECK(max_x >= 700 + REACH - 20); // it does reach out
		CHECK(max_x <= 700 + REACH + 1); // and no further: the wall at 1500 is not drawn to
	}
	SUBCASE("alpha only falls along the way out") {
		const rcg::Session s = aimed({});
		std::vector<DrawnPoint> past;
		for (const DrawnPoint &p : path_points(s)) {
			if (p.at.x > 700) {
				past.push_back(p);
			}
		}
		REQUIRE(past.size() > 8);
		std::sort(past.begin(), past.end(), [](const DrawnPoint &a, const DrawnPoint &b) { return a.at.x < b.at.x; });
		for (size_t i = 1; i < past.size(); i++) {
			CHECK(past[i].color[3] <= past[i - 1].color[3] + 1e-6f);
		}
	}
	SUBCASE("a tool's whole preview") {
		rcg::SessionConfig cfg;
		cfg.full_preview = true;
		const rcg::Session s = aimed(cfg);
		float max_x = 0;
		for (const DrawnPoint &p : path_points(s)) {
			max_x = std::max(max_x, p.at.x);
			CHECK(p.color[3] >= 0.99f);
		}
		CHECK(max_x >= 1340); // on to the room's wall, far past the fade's reach
	}
	SUBCASE("the paths' data is the whole of them: only the drawing fades") {
		rcg::SessionConfig cfg;
		cfg.full_preview = true;
		rcg::Session a = aimed({}), b = aimed(cfg);
		CHECK(a.record(nlohmann::json::object())["preview"] == b.record(nlohmann::json::object())["preview"]);
	}
}

TEST_CASE("preview: a key toggles the whole preview, in the draw list at once; no change to the game, so no redo goes") {
	rcg::Session s(level_of(R"({"player": {"start": [400, 500]}})"));
	s.set_cursor(rc::V2::of(700, 500));
	s.settle();
	const auto furthest = [&]() {
		float x = 0;
		for (const DrawnPoint &p : path_points(s)) {
			x = std::max(x, p.at.x);
		}
		return x;
	};
	CHECK(furthest() <= 700 + REACH + 1);
	const nlohmann::json before = s.record(nlohmann::json::object());
	s.key(rcg::Action::ToggleFullPreview);
	s.settle();
	CHECK(furthest() > 700 + REACH + 100);
	nlohmann::json whole = s.record(nlohmann::json::object());
	CHECK(whole["preview"] == before["preview"]);
	s.key(rcg::Action::ToggleFullPreview);
	s.settle();
	CHECK(furthest() <= 700 + REACH + 1);
	CHECK(s.record(nlohmann::json::object()) == before);
	// a redo lasts through it, and an undo leaves it as it is
	shoot(s, 700, 500);
	s.settle();
	s.key(rcg::Action::Undo);
	s.settle();
	REQUIRE(s.redo_depth() == 1);
	s.key(rcg::Action::ToggleFullPreview);
	s.settle();
	CHECK(s.redo_depth() == 1);
	s.key(rcg::Action::Redo);
	s.settle();
	CHECK(furthest() > 700 + REACH + 100); // still on
}

namespace {

// the Glow node's strokes in a colour (its alpha aside): their alpha, the event's marker's times the highlight's own
std::vector<float> consequence_alphas(const rcg::Session &s, float r, float g, float b) {
	std::vector<float> out;
	for (const auto &node : s.draw_frame()) {
		if (node.key.kind != rcg::render::NodeKind::Glow) {
			continue;
		}
		for (const rcg::render::Call &c : *node.calls) {
			if (!c.colors.empty() && c.colors[0][0] == r && c.colors[0][1] == g && c.colors[0][2] == b) {
				out.push_back(c.colors[0][3]);
			}
		}
	}
	return out;
}

} // namespace

TEST_CASE("preview: a consequence takes the opacity of its event's marker: whole to the cursor, fading past it, gone past the fade (Stage 196)") {
	// the door a button opens: it vanishes, red (0.95 at full strength)
	const char *level = R"({"player": {"start": [100, 540]}, "room": {"rect": [0, 240, 1600, 600], "built": true}, "elements": [
		{"kind": "button", "name": "b", "opens": "gate", "op": "set", "start": [1000, 400], "end": [1000, 700]},
		{"kind": "door", "name": "gate", "start": [1200, 300], "end": [1200, 800]}]})";
	const rcg::Level lv = level_of(level);
	const double reach = std::hypot(double(lv.bounds.w), double(lv.bounds.h)) / 10.0;
	rcg::Session s(lv);
	auto alpha_at = [&](double x) {
		s.set_cursor(rc::V2::of(x, 540));
		s.settle();
		const std::vector<float> a = consequence_alphas(s, 1.0f, 0.35f, 0.35f);
		return a.empty() ? 0.0 : double(a[0]) / double(0.95f);
	};
	CHECK(alpha_at(1100) == doctest::Approx(1.0)); // past the button
	CHECK(alpha_at(1000) == doctest::Approx(1.0)); // on it
	CHECK(alpha_at(1000 - reach / 2) == doctest::Approx(0.5).epsilon(0.02));
	CHECK(alpha_at(1000 - reach / 4) == doctest::Approx(0.75).epsilon(0.02));
	CHECK(alpha_at(1000 - reach - 5) == 0.0); // past the fade: nothing
	s.key(rcg::Action::ToggleFullPreview); // the whole preview: whole
	s.settle();
	CHECK(alpha_at(1000 - reach - 5) == doctest::Approx(1.0));
	s.key(rcg::Action::ToggleFullPreview);
	s.settle();
	CHECK(alpha_at(1000 - reach - 5) == 0.0);
}

TEST_CASE("preview: each consequence by its own event: two buttons on the way fade apart; a journey fades too (Stage 196)") {
	const char *level = R"({"player": {"start": [100, 540]}, "room": {"rect": [0, 240, 1600, 600], "built": true}, "elements": [
		{"kind": "button", "name": "a", "opens": "near", "op": "set", "start": [800, 400], "end": [800, 700]},
		{"kind": "door", "name": "near", "start": [1300, 300], "end": [1300, 400]},
		{"kind": "button", "name": "b", "opens": "far", "op": "set", "start": [860, 400], "end": [860, 700]},
		{"kind": "door", "name": "far", "start": [1400, 300], "end": [1400, 400]},
		{"kind": "grapple", "start": [920, 300], "end": [920, 800]}]})";
	rcg::Session s(level_of(level));
	s.set_cursor(rc::V2::of(790, 540));
	s.settle();
	const std::vector<float> doors = consequence_alphas(s, 1.0f, 0.35f, 0.35f);
	REQUIRE(doors.size() == 2);
	CHECK(doors[0] > doors[1]); // the nearer button's door is the stronger
	CHECK(doors[1] > 0.0f);
	// the journey's end, past the two: fainter still
	const std::vector<float> end = consequence_alphas(s, 0.6f, 1.0f, 0.7f);
	REQUIRE(!end.empty());
	CHECK(end[0] / 0.6f < doors[1] / 0.95f);
	REQUIRE(s.preview_trip_end().has_value());
	CHECK(end[0] > 0.0f);
}

TEST_CASE("preview: a consequence past a divergence fades from the divergence, as the physical walk's marker does (Stage 196)") {
	// a mirror planned and missed: the arrow goes on past it and through the button
	rcg::Session s(level_of(R"({"elements": [
		{"kind": "mirror", "name": "m", "start": [800, 250], "end": [800, 400]},
		{"kind": "button", "name": "b", "opens": "gate", "op": "set", "start": [1000, 400], "end": [1000, 700]},
		{"kind": "door", "name": "gate", "start": [1200, 300], "end": [1200, 800]}]})"));
	s.set_cursor(rc::V2::of(800, 300));
	s.settle();
	s.click(false);
	REQUIRE(s.plan().size() == 1);
	s.set_cursor(rc::V2::of(700, 540));
	s.settle();
	bool diverged = false;
	for (const rc::TraceStep &st : s.preview_steps()) {
		diverged = diverged || st.type == rc::StepType::DivergedPhysical;
	}
	REQUIRE(diverged);
	const std::vector<float> door = consequence_alphas(s, 1.0f, 0.35f, 0.35f);
	REQUIRE(door.size() == 1);
	CHECK(door[0] > 0.0f);
	CHECK(door[0] < 0.95f);
}

namespace {

struct DrawnRegion {
	size_t triangles = 0;
	rcg::render::Color color{};
	std::vector<rc::V2> pts;
	std::vector<int> indices;
	bool holds(double x, double y) const {
		const auto side = [&](const rc::V2 &u, const rc::V2 &v) { return (double(v.x) - u.x) * (y - u.y) - (double(v.y) - u.y) * (x - u.x); };
		for (size_t i = 0; i + 2 < indices.size(); i += 3) {
			const rc::V2 &p = pts.at(size_t(indices[i])), &q = pts.at(size_t(indices[i + 1])), &r = pts.at(size_t(indices[i + 2]));
			const double d0 = side(p, q), d1 = side(q, r), d2 = side(r, p);
			if ((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0)) {
				return true;
			}
		}
		return false;
	}
};

DrawnRegion drawn_region(const rcg::Session &s) {
	DrawnRegion out;
	for (const auto &node : s.draw_frame()) {
		if (node.key.kind != rcg::render::NodeKind::Visibility) {
			continue;
		}
		for (const rcg::render::Call &c : *node.calls) {
			if (c.kind == rcg::render::Kind::Triangles) {
				out.triangles = c.indices.size() / 3;
				out.color = c.colors.at(0);
				out.pts = c.pts;
				out.indices = c.indices;
			}
		}
	}
	return out;
}

const rcg::render::Color WHITE_REGION{1.0f, 1.0f, 1.0f, 0.10f}, GREEN_REGION{0.55f, 1.0f, 0.6f, 0.14f};

void plan_at(rcg::Session &s, double x, double y) {
	s.set_cursor(rc::V2::of(x, y));
	s.settle();
	s.click(false);
	s.settle();
}

const char *const BEFORE_TERMINAL = R"({"elements": [
	{"kind": "mirror", "start": [800, 300], "end": [800, 500]},
	{"kind": "grapple", "start": [1100, 300], "end": [1100, 800]}]})";

} // namespace

TEST_CASE("region: a plan that ends at a terminal draws the cones that reach it, light green (Stages 197, 198)") {
	for (const bool async : {false, true}) {
		CAPTURE(async);
		rcg::SessionConfig cfg;
		cfg.async = async;
		rcg::Session s(level_of(BEFORE_TERMINAL), cfg);
		const auto settled = [&]() {
			s.drain();
			return drawn_region(s);
		};
		const DrawnRegion alone = settled();
		REQUIRE(alone.triangles > 0);
		CHECK(alone.color == WHITE_REGION);
		CHECK_FALSE(s.region_reaches_terminal());
		SUBCASE("planned first: the cones from the player; its region is the plan's before it (Stage 201)") {
			plan_at(s, 1100, 540);
			REQUIRE(s.plan().size() == 1);
			const DrawnRegion green = settled();
			CHECK(s.region_reaches_terminal());
			CHECK(green.color == GREEN_REGION);
			CHECK(green.holds(900, 600));      // on the way to the terminal
			CHECK(alone.holds(700, 800));      // steeply down: it meets the room's floor, not the terminal
			CHECK_FALSE(green.holds(700, 800));
			CHECK(alone.holds(600, 540));      // behind the player
			CHECK_FALSE(green.holds(600, 540));
			CHECK_FALSE(green.holds(900, 400)); // in the mirror's shadow
			CHECK(s.record(nlohmann::json::object())["visibility"]["n"][1] > 0);
			s.key(rcg::Action::ClearPlan);
			const DrawnRegion back = settled();
			CHECK_FALSE(s.region_reaches_terminal());
			CHECK(back.color == WHITE_REGION);
			CHECK(back.pts == alone.pts);
		}
		SUBCASE("planned after a mirror, out of the reflection's reach: no cone reaches it") {
			plan_at(s, 800, 400);
			REQUIRE(s.plan().size() == 1);
			const DrawnRegion mirror = settled();
			CHECK(mirror.color == WHITE_REGION);
			CHECK_FALSE(s.region_reaches_terminal());
			REQUIRE(mirror.triangles > 0);
			plan_at(s, 1100, 540);
			REQUIRE(s.plan().size() == 2);
			const DrawnRegion green = settled();
			CHECK(s.region_reaches_terminal());
			CHECK(green.triangles == 0);
		}
		SUBCASE("planned after a mirror that reaches it: the mirror's cones that do") {
			rcg::Session r(level_of(R"({"elements": [
				{"kind": "mirror", "start": [800, 300], "end": [800, 500]},
				{"kind": "grapple", "start": [600, 300], "end": [600, 800]}]})"), cfg);
			plan_at(r, 800, 400);
			REQUIRE(r.plan().size() == 1);
			r.drain();
			const DrawnRegion mirror = drawn_region(r);
			plan_at(r, 603, 400);
			REQUIRE(r.plan().size() == 2);
			r.drain();
			CHECK(r.region_reaches_terminal());
			const DrawnRegion green = drawn_region(r);
			CHECK(green.color == GREEN_REGION);
			CHECK(green.holds(700, 420));
			CHECK(mirror.holds(700, 320)); // past the mirror, but above the terminal's end
			CHECK_FALSE(green.holds(700, 320));
		}
	}
}

TEST_CASE("region: the green cones reach the planned side, within the planned piece (Stage 198)") {
	SUBCASE("the other side of the terminal: no cone reaches it") {
		rcg::Session s(level_of(R"({"elements": [
			{"kind": "mirror", "start": [800, 300], "end": [800, 500]},
			{"kind": "grapple", "start": [600, 300], "end": [600, 800]}]})"));
		plan_at(s, 800, 400);
		plan_at(s, 603, 400);
		REQUIRE(s.plan().size() == 2);
		const rcg::Side near = s.plan()[1].side;
		CHECK(drawn_region(s).triangles > 0);
		s.key(rcg::Action::ClearPlan);
		s.settle();
		plan_at(s, 800, 400);
		plan_at(s, 597, 400);
		REQUIRE(s.plan().size() == 2);
		REQUIRE(s.plan()[1].side != near);
		CHECK(s.region_reaches_terminal());
		CHECK(drawn_region(s).triangles == 0);
	}
	SUBCASE("a piece of the terminal: the cones to that piece") {
		const char *crossed = R"({"elements": [
			{"kind": "mirror", "start": [1050, 600], "end": [1150, 600]},
			{"kind": "grapple", "start": [1100, 300], "end": [1100, 800]}]})";
		rcg::Session upper(level_of(crossed)), lower(level_of(crossed));
		plan_at(upper, 1100, 420);
		plan_at(lower, 1100, 700);
		REQUIRE(upper.plan().size() == 1);
		REQUIRE(lower.plan().size() == 1);
		REQUIRE(upper.plan()[0].span.has_value());
		REQUIRE(lower.plan()[0].span.has_value());
		const DrawnRegion up = drawn_region(upper), low = drawn_region(lower);
		CHECK(up.holds(900, 480));
		CHECK_FALSE(low.holds(900, 480));
		CHECK(low.holds(900, 600));
		CHECK_FALSE(up.holds(900, 600));
	}
}

TEST_CASE("region: the green is where the plan works, cursor by cursor (Stage 198)") {
	const auto check_plan = [](const char *level, const std::vector<std::array<double, 2>> &clicks, double x0, double x1) {
		rcg::Session s(level_of(level));
		for (const auto &c : clicks) {
			plan_at(s, c[0], c[1]);
		}
		REQUIRE(s.plan().size() == clicks.size());
		REQUIRE(s.region_reaches_terminal());
		const DrawnRegion green = drawn_region(s);
		const int terminal = s.plan().back().surface_id;
		int inside = 0;
		for (double x = x0 + 3.5; x < x1; x += 37.0) {
			for (double y = 253.25; y < 830.0; y += 37.0) {
				s.set_cursor(rc::V2::of(x, y));
				s.settle();
				bool diverged = false, hit = false;
				for (const rc::TraceStep &st : s.preview_steps()) {
					diverged = diverged || st.type == rc::StepType::DivergedPhysical;
					hit = hit || (st.surface_id == terminal && st.hit_on_segment && !diverged);
				}
				CAPTURE(x);
				CAPTURE(y);
				CHECK(green.holds(x, y) == (hit && !diverged));
				inside += green.holds(x, y) ? 1 : 0;
			}
		}
		CHECK(inside > 0);
	};
	SUBCASE("first") {
		check_plan(BEFORE_TERMINAL, {{1100, 540}}, 660, 1095);
	}
	SUBCASE("after a mirror") {
		check_plan(R"({"elements": [
			{"kind": "mirror", "start": [800, 300], "end": [800, 500]},
			{"kind": "grapple", "start": [600, 300], "end": [600, 800]}]})", {{800, 400}, {603, 400}}, 604, 795);
	}
}

TEST_CASE("region: a plan that is not ended by a terminal stays white; no region at all, none green (Stage 197)") {
	{
		rcg::Session s(level_of(BEFORE_TERMINAL));
		plan_at(s, 800, 400);
		REQUIRE(s.plan().size() == 1);
		CHECK_FALSE(s.region_reaches_terminal());
		CHECK(drawn_region(s).color == WHITE_REGION);
	}
	{
		rcg::Session s(level_of(BEFORE_TERMINAL));
		plan_at(s, 1100, 540);
		REQUIRE(s.plan().size() == 1);
		CHECK(s.region_reaches_terminal());
		s.key(rcg::Action::ToggleVisibility);
		s.settle();
		CHECK_FALSE(s.region_reaches_terminal());
		CHECK(drawn_region(s).triangles == 0);
	}
}

TEST_CASE("preview: a mark past the fade is not drawn; one within it fades with its step") {
	// a mirror within the fade's reach, then what the reflection meets far past it
	const char *level = R"({"player": {"start": [400, 500]}, "elements": [{"kind": "mirror", "start": [800, 300], "end": [800, 700]}]})";
	const auto marks = [&](bool whole) {
		rcg::SessionConfig cfg;
		cfg.full_preview = whole;
		rcg::Session s(level_of(level), cfg);
		s.set_cursor(rc::V2::of(700, 500));
		s.settle();
		std::vector<float> alphas;
		for (const rcg::render::Node &n : s.draw()) {
			if (n.key != nlohmann::json::array({"path"})) {
				continue;
			}
			for (const rcg::render::Call &c : n.calls) {
				if (c.kind != rcg::render::Kind::Line && c.kind != rcg::render::Kind::Multiline && !c.colors.empty()) {
					alphas.push_back(c.colors[0][3]);
				}
			}
		}
		return alphas;
	};
	const std::vector<float> faded = marks(false), whole = marks(true);
	CHECK_FALSE(faded.empty());         // (the mirror's, 100 past the cursor, is there)
	CHECK(faded.size() < whole.size()); // (the ones further on are gone)
	CHECK(*std::min_element(faded.begin(), faded.end()) > 0.0f); // (nothing invisible left in the list)
	CHECK(std::any_of(faded.begin(), faded.end(), [](float a) { return a < 0.75f; })); // (the mirror's mark, part way out, is faded)
	CHECK(std::none_of(whole.begin(), whole.end(), [](float a) { return a < 0.75f && a > 0.55f; })); // (a whole preview's are not)
}

TEST_CASE("preview: a divergence -- the solid red reaches the cursor whole, then fades; the physical path fades from the divergence") {
	// the plan's short mirror at x = 1100 (y 480-520): the cursor's reflection needs a hit far off it
	rcg::Session s = planned_at_mirror(20);
	const rc::V2 cursor = rc::V2::of(1000, 300);
	const rc::V2 divergence = rc::V2::of(1100, 366.6667); // where the walks part: the reflection's hit on the mirror's carrier
	s.set_cursor(cursor);
	s.settle();
	const rcg::render::Color red_under = rcg::render::hex(0xB30000FF), yellow = rcg::render::hex(0xFFFF00FF);
	bool red_at_cursor = false;
	float furthest_yellow = 0.0f;
	std::vector<DrawnPoint> yellows;
	for (const DrawnPoint &p : path_points(s)) {
		const bool red = p.color[0] == red_under[0] && p.color[1] == red_under[1] && p.color[2] == red_under[2];
		if (red && p.at.distance_to(cursor) < 1.0f) {
			red_at_cursor = true;
			CHECK(p.color[3] == 1.0f);
		}
		if (p.color[0] == yellow[0] && p.color[1] == yellow[1] && p.color[2] == yellow[2]) {
			yellows.push_back(p);
		}
	}
	CHECK(red_at_cursor);
	REQUIRE_FALSE(yellows.empty());
	// the physical path, past the mirror it misses: from the divergence a reach out, no further, and on the way out it fades
	for (const DrawnPoint &p : yellows) {
		const double out = double(p.at.distance_to(divergence));
		furthest_yellow = std::max(furthest_yellow, float(out));
		CHECK(double(p.color[3]) == doctest::Approx(1.0 - out / REACH).epsilon(0.08));
	}
	CHECK(furthest_yellow > REACH - 30);
	CHECK(furthest_yellow <= REACH + 1);
}

TEST_CASE("platforms: the demo level plays to its goal") {
	const std::filesystem::path root = RC_REPO_ROOT;
	rcg::Session s(rcg::load_level((root / "data/levels/objectives/platforms.json").string()));
	s.hold({}, 60);
	CHECK(body(s).y == doctest::Approx(700 - 20 - 0.08).epsilon(1e-6)); // the box fell onto the high platform
	s.hold({"move_up"}, 30); // a jump (on the second tick, up held: Stage 182) up through the low platform, onto it
	s.hold({}, 60);
	CHECK(s.player().y == doctest::Approx(770 - 24 - 12 - 0.08).epsilon(1e-6));
	s.hold({"move_up"}, 30); // and onto the high one
	s.hold({}, 60);
	CHECK(s.player().y == doctest::Approx(700 - 24 - 12 - 0.08).epsilon(1e-6));
	s.hold({"move_down"}, 60); // down through both
	CHECK(s.player().y == doctest::Approx(840 - 24 - 12 - 0.08).epsilon(1e-6));
	shoot(s, 760, 680); // the box pulled down through both onto the plate
	CHECK(body(s).y > 770);
	CHECK(s.flags().is_true("det:plate"));
	CHECK_FALSE(s.won());
	s.hold({"move_right"}, 170);
	s.hold({}, 20); // (Stage 188: stopped, it may fire)
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

namespace {

// the previewed journey's trail: its legs (one per side of each portal crossed)
int trail_legs(const rcg::Session &s) {
	int legs = 0;
	for (const auto &n : s.draw_frame()) {
		if (n.key.kind == rcg::render::NodeKind::Glow) {
			for (const auto &c : *n.calls) {
				legs += c.kind == rcg::render::Kind::Polyline ? 1 : 0;
			}
		}
	}
	return legs;
}

// the user's pulling_2 (as in their dump): the level's walls, a box falling to the floor at y 1080
rcg::Level pulling_2() {
	return rcg::parse_level_json(json::parse(R"({"schema": "rc-level/2", "name": "pulling_2", "edges": "wall",
		"gravity": [0.0, 980.0], "player": {"start": [550.0, 1050.0]}, "room": {"rect": [560.0, 240.0, 800.0, 600.0], "built": false},
		"detectors": [{"flag": "close", "for": "any", "mode": "while", "name": "detector_1", "region": {"rect": [800.0, 1050.0, 150.0, 30.0]}}],
		"elements": [{"kind": "door", "name": "door_1", "flag": "!close", "start": [1100.0, 1080.0], "end": [1100.0, 900.0]}],
		"objective": {"goal": {"rect": [1250.0, 950.0, 100.0, 130.0]}},
		"objects": [{"name": "object_1", "circle": {"center": [890.0, 700.0], "radius": 16.0}}]})"));
}

} // namespace

TEST_CASE("pull: the user's pulling_2 -- the box slides along the floor to the player; no crossing where the cursor splits the path (Stage 174)") {
	// the user's F12 dump: the preview's box froze on the floor at x 499 (a slide's rounding residue into the floor,
	// the box a float32 ulp inside its skin) and crossed a portal that is not there at the cursor (a step's shared
	// start taken for a jump)
	rcg::Session s(pulling_2());
	s.hold({}, 240); // the box falls and rests
	// (Stage 173: the player standing where the dump's did -- its feet there, its head 24 above)
	s.teleport(rc::V2::of(0x1.575542p+8, 0x1.0afae2p+10 - 24));
	s.hold({}, 60);
	REQUIRE(s.player().x == 0x1.575542p+8f);
	REQUIRE(s.player().y == float(0x1.0afae2p+10 - 24));
	REQUIRE(body(s).x == 890.0f);
	s.set_cursor(rc::V2::of(0x1.a638e2p+9, 0x1.0c186p+10)); // short of the box: two steps
	s.settle();
	const std::optional<rc::V2> end = s.preview_trip_end();
	REQUIRE(end.has_value());
	CHECK(dist(*end, s.player()) < 16 + 12 + 1); // at the player, not caught on the floor
	CHECK(trail_legs(s) == 1);                    // no portal: one leg
	shoot(s, 0x1.a638e2p+9, 0x1.0c186p+10);
	CHECK(body(s).x == end->x); // the live journey ends where the preview said
	CHECK(body(s).y == end->y);
}

TEST_CASE("pull: a box resting on the floor comes to the player from anywhere, the cursor on it or short of it (Stage 174)") {
	rcg::Session s(pulling_2());
	s.hold({}, 240); // the box rests on the floor
	for (int i = 0; i < 40; i++) {
		const double px = 100.5 + 17.37 * i; // (scattered: the residue's sign depends on the position)
		s.teleport(rc::V2::of(px, 1060 - 24)); // (Stage 173: its feet at 1060)
		s.hold({}, 30);
		// on the box (one step), short of it on the same aim (two; Stage 173: along the line from the player, whose
		// head is higher than the fixed points short of the box once were)
		const rc::V2 p = s.player();
		auto short_of = [&](double tx, double ty) { return std::pair{p.x + (tx - p.x) * 0.9, p.y + (ty - p.y) * 0.9}; };
		for (const auto &[cx, cy] : {std::pair{890.0, 1056.0}, std::pair{890.0, 1072.0}, short_of(890.0, 1056.0), short_of(890.0, 1072.0)}) {
			CAPTURE(px);
			CAPTURE(cx);
			CAPTURE(cy);
			s.set_cursor(rc::V2::of(cx, cy));
			s.settle();
			const std::optional<rc::V2> end = s.preview_trip_end();
			REQUIRE(end.has_value());
			CHECK(dist(*end, s.player()) < 16 + 12 + 1);
			CHECK(trail_legs(s) == 1);
		}
	}
}

TEST_CASE("pull and grapple: the preview's end is drawn as what moves -- the player's triangle (and its body under gravity), the object's shape; an object drawn in its shape (Stage 181)") {
	using rcg::render::Kind;
	// the closed outlines (point counts) the preview's end draws in its colour, and the circles' radii
	auto ends = [](const rcg::Session &s) {
		std::pair<std::vector<size_t>, std::vector<double>> got;
		for (const rcg::render::FrameNode &n : s.draw_frame()) {
			for (const rcg::render::Call &c : *n.calls) {
				if (c.colors.empty() || c.colors[0] != rcg::render::Color{0.6f, 1.0f, 0.7f, 0.95f}) {
					continue;
				}
				if (c.kind == Kind::Polyline) {
					got.first.push_back(c.pts.size());
				} else if (c.kind == Kind::Arc) {
					got.second.push_back(c.radius);
				}
			}
		}
		return got;
	};
	auto previewed = [](const char *level, double x, double y) {
		rcg::Session s(level_of(level));
		s.set_cursor(rc::V2::of(x, y));
		s.settle();
		REQUIRE(s.preview_trip_end().has_value());
		return s;
	};
	// a grapple: the triangle; under gravity the body too
	const char *grapple = R"({"elements": [{"kind": "grapple", "start": [1200, 300], "end": [1200, 800]}]})";
	CHECK(ends(previewed(grapple, 1200, 540)) == std::pair<std::vector<size_t>, std::vector<double>>{{4}, {}});
	const char *tall = R"({"gravity": [0, 980], "elements": [{"kind": "grapple", "start": [1200, 300], "end": [1200, 800]}]})";
	CHECK(ends(previewed(tall, 1200, 540)) == std::pair<std::vector<size_t>, std::vector<double>>{{5, 4}, {}});
	// a pull: the object's circle, or its shape
	const char *ball = R"({"objects": [{"name": "box", "circle": {"center": [1100, 540], "radius": 16}}]})";
	CHECK(ends(previewed(ball, 1100, 540)) == std::pair<std::vector<size_t>, std::vector<double>>{{}, {16.0}});
	const char *box = R"({"objects": [{"name": "box", "circle": {"center": [1100, 540], "radius": 16}, "shape": "square"}]})";
	CHECK(ends(previewed(box, 1100, 540)) == std::pair<std::vector<size_t>, std::vector<double>>{{5}, {}});
	// the object itself: its square, its circle (what it collides as) faint; the collisions those of the circle
	const rcg::Session s(level_of(box));
	bool square = false, faint = false;
	for (const rcg::render::FrameNode &n : s.draw_frame()) {
		if (n.key.kind != rcg::render::NodeKind::Surface || n.key.a != s.world().bodies()[0].id) {
			continue;
		}
		for (const rcg::render::Call &c : *n.calls) {
			square = square || (c.kind == Kind::Polyline && c.pts.size() == 5);
			faint = faint || (c.kind == Kind::Arc && c.radius == 16.0 && c.colors[0][3] < 0.5f);
		}
	}
	CHECK(square);
	CHECK(faint);
	CHECK(rcg::describe_surface(s.world().bodies()[0].built) == rcg::describe_surface(rcg::Session(level_of(ball)).world().bodies()[0].built));
	// checked
	try {
		level_of(R"({"objects": [{"name": "a", "circle": {"center": [0, 0]}, "shape": "hexagon"}]})");
		FAIL("no error");
	} catch (const rcg::LevelError &e) {
		CHECK(std::string(e.what()).find("objects[0].shape: circle, square, triangle, diamond, star or cross expected") != std::string::npos);
	}
}

namespace {

const char *const OPEN_LEVEL = R"({"room": {"rect": [560, 240, 800, 600], "built": false}, "elements": [
	{"kind": "mirror", "start": [1200, 800], "end": [1300, 900]}]%s})";

rcg::Level open_level(const char *extra) {
	char text[512];
	std::snprintf(text, sizeof(text), OPEN_LEVEL, extra);
	return level_of(text);
}

int infinities(const std::vector<rc::TraceStep> &steps) {
	int n = 0;
	for (const rc::TraceStep &st : steps) {
		n += st.to_infinity ? 1 : 0;
	}
	return n;
}

} // namespace

TEST_CASE("level: infinity_loop is off by default, read from the level, kept by the lowering (Stage 199)") {
	CHECK_FALSE(open_level("").infinity_loop);
	CHECK_FALSE(open_level(R"(, "infinity_loop": false)").infinity_loop);
	CHECK(open_level(R"(, "infinity_loop": true)").infinity_loop);
	CHECK_THROWS_WITH_AS(open_level(R"(, "infinity_loop": 1)"), doctest::Contains("infinity_loop"), rcg::LevelError);
}

TEST_CASE("session: off, a walk is cut at infinity and comes back from nowhere; on, it goes round as before (Stage 199)") {
	for (const bool async : {false, true}) {
		CAPTURE(async);
		rcg::SessionConfig cfg;
		cfg.async = async;
		const auto preview = [&](const char *extra) {
			rcg::Session s(open_level(extra), cfg);
			s.set_cursor(rc::V2::of(800, 300));
			s.settle();
			s.drain();
			return std::pair<int, size_t>{infinities(s.preview_steps()), s.preview_steps().size()};
		};
		const auto off = preview(R"(, "infinity_loop": false)");
		CHECK(off == preview(""));
		CHECK(off.first == 0);
		CHECK(off.second <= 2); // (to the cursor, past it)
		CHECK(preview(R"(, "infinity_loop": true)").first == 5);
	}
}

TEST_CASE("session: off, the level's edge is no entry to plan; on, it is (Stage 199)") {
	const auto planned = [](const char *extra) {
		rcg::Session s(open_level(extra));
		s.set_cursor(rc::V2::of(1920, 300));
		s.settle();
		s.click(false);
		return s.plan().size();
	};
	CHECK(planned("") == 0);
	CHECK(planned(R"(, "infinity_loop": true)") == 1);
}

TEST_CASE("session: off, a shot that passes infinity ends, nothing past it fires (Stage 199)") {
	// straight up from the player, round through the top edge: the way back meets the button below the player
	const auto opened = [](bool loop) {
		nlohmann::json j = nlohmann::json::parse(R"({"room": {"rect": [560, 240, 800, 600], "built": false}, "elements": [
			{"kind": "button", "name": "b", "opens": "gate", "op": "set", "start": [600, 800], "end": [720, 800]},
			{"kind": "door", "name": "gate", "start": [1700, 300], "end": [1700, 800]}]})");
		if (loop) {
			j["infinity_loop"] = true;
		}
		rcg::Session s(level_of(j.dump().c_str()));
		s.set_cursor(rc::V2::of(660, 300));
		s.settle();
		s.fire(-1, 1.0 / 60.0);
		return s.flags();
	};
	CHECK_FALSE(opened(true) == opened(false));
}

// the user's F12 dump (platforms_2, 2026-11-03): a planned object the aim misses: the planned walk never consumes its
// entry, so it never reaches the cursor and went five times round through infinity, none of it past a cursor to fade
TEST_CASE("session: off, a planned entry the aim misses goes no further than infinity (Stage 199)") {
	const auto steps = [](bool loop) {
		nlohmann::json j = nlohmann::json::parse(R"({"room": {"rect": [560, 240, 800, 600], "built": false},
			"objects": [{"name": "box", "circle": {"center": [1200, 300], "radius": 16}}]})");
		if (loop) {
			j["infinity_loop"] = true;
		}
		rcg::Session s(level_of(j.dump().c_str()));
		s.set_cursor(rc::V2::of(1200, 284));
		s.settle();
		s.click(false);
		s.settle();
		REQUIRE(s.plan().size() == 1);
		s.set_cursor(rc::V2::of(900, 800)); // (far from the box's way)
		s.settle();
		return s.preview_steps();
	};
	CHECK(infinities(steps(false)) == 0);
	CHECK(infinities(steps(true)) > 1);
	CHECK(steps(false).size() < steps(true).size());
	// (Stage 206: the box is a terminal -- the cursor counts before it, Stage 201 -- and past the cursor the aim meets the
	// box's counterpart, the tangent-length circle about the player, where the planned walk ends, in both modes)
	for (const bool loop : {false, true}) {
		CAPTURE(loop);
		const std::vector<rc::TraceStep> m = steps(loop);
		REQUIRE(m.size() >= 2);
		CHECK(m[0].type == rc::StepType::Aligned);
		CHECK(m[1].counterpart);
	}
}

// Stage 206 (the user): counterpart surfaces -- a planned surface the aim cannot reach before the frame's infinity: the
// planned path runs whole to the surface's counterpart (the circle about the ray's origin with the tangent length as
// radius; a line's mirror image through the origin) and fades from there, so an aim a hair outside a planned circle's
// tangent cone ends where a grazing aim hits; a red dot marks the point, no ring
TEST_CASE("preview: a planned circle the aim misses runs whole to the tangent length and fades there, continuous across the cone's edge (Stage 206)") {
	for (const bool loop : {false, true}) {
		CAPTURE(loop);
		nlohmann::json j = nlohmann::json::parse(R"({"room": {"rect": [560, 240, 800, 600], "built": false},
			"objects": [{"name": "ball", "circle": {"center": [1200, 300], "radius": 16}, "face": "mirror", "kind": "static"}]})");
		j["infinity_loop"] = loop;
		rcg::Session s(level_of(j.dump().c_str()));
		plan_at(s, 1200, 284);
		REQUIRE(s.plan().size() == 1);
		const rc::V2 P = s.player();
		const rc::V2 C = rc::V2::of(1200, 300);
		const double tlen = std::sqrt(dist(P, C) * dist(P, C) - 16.0 * 16.0);
		// the aim's end: the box's counterpart (a miss) or its hit point; the merged step's index
		struct End {
			bool miss;
			rc::V2 at;
			size_t step;
		};
		const auto end_of = [&](const rc::V2 &cursor) -> std::optional<End> {
			s.set_cursor(cursor);
			s.settle();
			const std::vector<rc::TraceStep> &m = s.preview_steps();
			for (size_t i = 0; i < m.size(); i++) {
				if (m[i].counterpart) {
					return End{true, m[i].end, i};
				}
				if (m[i].surface_id == s.plan()[0].surface_id && m[i].hit_on_segment && m[i].significant) {
					return End{false, m[i].end, i};
				}
			}
			return std::nullopt;
		};
		// the cursor on a small circle about the box's centre: its aim image lies far out along the cursor's bearing
		// from the centre, so the aim turns through the tangent cone's edge as the bearing leaves the player's
		const double axis = std::atan2(double(P.y - C.y), double(P.x - C.x));
		std::optional<End> prev;
		int hits = 0, misses = 0;
		for (int deg = 0; deg <= 70; deg += 1) {
			const double a = axis + deg * std::acos(-1.0) / 180.0;
			const std::optional<End> e = end_of(rc::V2::of(double(C.x) + 8.0 * std::cos(a), double(C.y) + 8.0 * std::sin(a)));
			REQUIRE(e.has_value());
			CAPTURE(deg);
			if (e->miss) {
				misses++;
				CHECK(std::abs(dist(e->at, P) - tlen) < 0.05); // (at the tangent length from the ray's origin)
				const std::vector<std::array<double, 2>> seen = s.preview_seen();
				CHECK(seen[e->step][1] == 1.0); // (whole to the counterpart)
				if (!loop) {
					CHECK(infinities(s.preview_steps()) == 0);
				}
			} else {
				hits++;
				CHECK(std::abs(dist(e->at, C) - 16.0) < 0.05);
			}
			if (prev) {
				CHECK(dist(prev->at, e->at) < 20.0); // (the end moves continuously, across the cone's edge too)
			}
			prev = e;
		}
		CHECK(hits > 5);
		CHECK(misses > 5);
	}
}

TEST_CASE("preview: a counterpart point gets the planned walk's dot, no ring (Stage 206)") {
	nlohmann::json j = nlohmann::json::parse(R"({"room": {"rect": [560, 240, 800, 600], "built": false},
		"objects": [{"name": "ball", "circle": {"center": [1200, 300], "radius": 16}, "face": "mirror", "kind": "static"}]})");
	rcg::Session s(level_of(j.dump().c_str()));
	plan_at(s, 1200, 284);
	REQUIRE(s.plan().size() == 1);
	// a cursor off the player's bearing from the ball's centre: the aim misses (the test above)
	const rc::V2 C = rc::V2::of(1200, 300);
	const double axis = std::atan2(double(s.player().y - C.y), double(s.player().x - C.x)) + 60.0 * std::acos(-1.0) / 180.0;
	s.set_cursor(rc::V2::of(double(C.x) + 8.0 * std::cos(axis), double(C.y) + 8.0 * std::sin(axis)));
	s.settle();
	const std::vector<rc::TraceStep> &m = s.preview_steps();
	std::optional<rc::V2> at;
	for (const rc::TraceStep &st : m) {
		if (st.counterpart) {
			at = st.end;
		}
	}
	REQUIRE(at.has_value());
	std::vector<rcg::render::Call> calls;
	s.paint_steps(m, calls);
	bool dot = false, ring = false;
	for (const rcg::render::Call &c : calls) {
		if (c.kind != rcg::render::Kind::Circle || c.pts.empty() || dist(c.pts[0], *at) > 4.0) { // (the lanes shift the mark a little)
			continue;
		}
		dot = dot || (c.radius == 2.0 && c.colors[0][0] > c.colors[0][1]); // (red: the planned walk alone)
		ring = ring || c.radius > 3.0;
	}
	CHECK(dot);
	CHECK_FALSE(ring);
}

// a planned terminal the aim faces away from (the cursor before it, Stage 201): the planned walk goes on past the cursor
// to the terminal's counterpart -- the terminal's mirror image through the player -- and ends there, whole
TEST_CASE("preview: a planned terminal behind the aim: whole to its counterpart, the mirror image through the player (Stage 206)") {
	for (const bool loop : {false, true}) {
		CAPTURE(loop);
		nlohmann::json j = nlohmann::json::parse(R"({"room": {"rect": [560, 240, 800, 600], "built": false}, "elements": [
			{"kind": "grapple", "start": [1200, 300], "end": [1200, 500]}]})");
		j["infinity_loop"] = loop;
		rcg::Session s(level_of(j.dump().c_str()));
		plan_at(s, 1200, 400);
		REQUIRE(s.plan().size() == 1);
		s.set_cursor(rc::V2::of(600, 540)); // (the other way from it: the aim runs west, the grapple behind)
		s.settle();
		const rc::TracePath *planned = s.planned_trace();
		REQUIRE(planned != nullptr);
		REQUIRE(planned->cursor_index >= 0);
		REQUIRE(!planned->steps.empty());
		CHECK(planned->steps.back().counterpart);
		CHECK(std::abs(double(planned->steps.back().end.x) - (2.0 * double(s.player().x) - 1200.0)) < 0.05);
		CHECK(std::abs(double(planned->steps.back().end.y) - double(s.player().y)) < 0.05);
		// whole to it: no planned step fades
		const std::vector<rc::TraceStep> &m = s.preview_steps();
		const std::vector<std::array<double, 2>> seen = s.preview_seen();
		REQUIRE(seen.size() == m.size());
		for (size_t i = 0; i < m.size(); i++) {
			if (m[i].type != rc::StepType::DivergedPhysical) {
				CAPTURE(i);
				CHECK(seen[i][1] == 1.0);
			}
		}
		CHECK(s.debug_summary().find("cursor: preview visible, region inside") != std::string::npos);
	}
}

// Stage 201 (the user's F12 dumps): a planned terminal the aim misses: the cursor counts, and past it the path fades
TEST_CASE("session: the cursor counts before a planned terminal, reached or not (Stage 201)") {
	for (const bool loop : {false, true}) {
		CAPTURE(loop);
		nlohmann::json j = nlohmann::json::parse(R"({"room": {"rect": [560, 240, 800, 600], "built": false}, "elements": [
			{"kind": "grapple", "start": [1200, 300], "end": [1200, 500]}]})");
		j["infinity_loop"] = loop;
		rcg::Session s(level_of(j.dump().c_str()));
		plan_at(s, 1200, 400);
		REQUIRE(s.plan().size() == 1);
		const auto check = [&](double x, double y, bool on_terminal) {
			CAPTURE(x);
			CAPTURE(y);
			s.set_cursor(rc::V2::of(x, y));
			s.settle();
			bool aligned_to_cursor = false, diverged = false, hit = false;
			for (const rc::TraceStep &st : s.preview_steps()) {
				aligned_to_cursor = aligned_to_cursor || st.type == rc::StepType::AlignedPostPlanned;
				// (diverged before the cursor; past it a miss parts the walks at the grapple's line: Stage 205)
				diverged = diverged || st.type == rc::StepType::DivergedPlanned;
				hit = hit || (st.surface_id == s.plan()[0].surface_id && st.hit_on_segment);
			}
			CHECK(aligned_to_cursor); // (past the cursor, as past any cursor)
			CHECK_FALSE(diverged);
			CHECK(hit == on_terminal);
			CHECK(s.debug_summary().find("cursor: preview visible, region inside") != std::string::npos);
		};
		check(900, 480, true);   // on the way to it
		check(900, 700, false);  // the aim misses it: it is never reached
		check(600, 540, false);  // the other way from it: no aim away from the cursor
	}
}

TEST_CASE("session: past the terminal, on the way to it, no cursor is reached (Stage 201)") {
	nlohmann::json j = nlohmann::json::parse(R"({"room": {"rect": [560, 240, 800, 600], "built": false}, "elements": [
		{"kind": "grapple", "start": [1200, 300], "end": [1200, 500]}]})");
	rcg::Session s(level_of(j.dump().c_str()));
	plan_at(s, 1200, 400);
	s.set_cursor(rc::V2::of(1300, 390));
	s.settle();
	CHECK(s.debug_summary().find("cursor: preview not visible, region outside") != std::string::npos);
}

// Stage 200 (the user): a planned walk that never reaches its cursor fades past its first unplanned event
TEST_CASE("preview: a planned path that never reaches the cursor fades past its first unplanned event (Stage 200)") {
	const auto faintest = [](bool whole) {
		rcg::SessionConfig cfg;
		cfg.full_preview = whole;
		nlohmann::json j = nlohmann::json::parse(R"({"room": {"rect": [560, 240, 800, 600], "built": false}, "elements": [
			{"kind": "mirror", "start": [1200, 300], "end": [1200, 500]}]})");
		j["infinity_loop"] = true;
		rcg::Session s(level_of(j.dump().c_str()), cfg);
		plan_at(s, 1200, 400);
		REQUIRE(s.plan().size() == 1);
		s.set_cursor(rc::V2::of(800, 20)); // (the aim leaves through the top: the plan unmet, the walk round through infinity)
		s.settle();
		const std::vector<rc::TraceStep> &m = s.preview_steps();
		REQUIRE(std::any_of(m.begin(), m.end(), [](const rc::TraceStep &st) { return st.type == rc::StepType::DivergedPlanned; }));
		std::vector<rcg::render::Call> calls;
		s.paint_steps(m, calls);
		float least = 1.0f;
		for (const rcg::render::Call &c : calls) {
			for (const rcg::render::Color &col : c.colors) {
				least = std::min(least, col[3]);
			}
		}
		return std::pair<float, size_t>{least, calls.size()};
	};
	const auto faded = faintest(false), whole = faintest(true);
	CHECK(faded.first < whole.first);
	CHECK(faded.second < 2 * whole.second + 64); // (bands, not the whole walk over and over)
}

// Stage 205 (the user): only physical sections fade -- the planned path to a planned terminal the cursor comes before
// shows whole to it (its hit, or where it crosses its line off its segment), the physical walk to the divergence
TEST_CASE("preview: planned to a planned terminal, whole to its hit or its miss; physical sections fade (Stage 205)") {
	const auto drawn = [](const char *elements, double cx, double cy, bool whole) {
		rcg::SessionConfig cfg;
		cfg.full_preview = whole;
		nlohmann::json j = nlohmann::json::parse(elements);
		j["room"] = nlohmann::json::parse(R"({"rect": [560, 240, 800, 600], "built": false})");
		rcg::Session s(level_of(j.dump().c_str()), cfg);
		plan_at(s, 1200, 400);
		REQUIRE(s.plan().size() == 1);
		s.set_cursor(rc::V2::of(cx, cy));
		s.settle();
		return path_points(s);
	};
	const char *grapple = R"({"elements": [{"kind": "grapple", "start": [1200, 300], "end": [1200, 500]}]})";
	// on the way to it: the cursor 309 short of the hit (past the fade's 220), yet the path reaches the grapple whole
	const std::vector<DrawnPoint> aligned = drawn(grapple, 900, 480, false), aligned_whole = drawn(grapple, 900, 480, true);
	REQUIRE(aligned.size() == aligned_whole.size());
	for (size_t k = 0; k < aligned.size(); k++) {
		CHECK(aligned[k].color[3] == aligned_whole[k].color[3]);
	}
	CHECK(std::any_of(aligned.begin(), aligned.end(), [](const DrawnPoint &p) { return p.at.x > 1190.0f; }));
	// the aim misses it, just above (the user's grapple_1 dump): whole to where it crosses the grapple's line off its
	// segment (335 past the cursor), then fading on its way up, as past a cursor
	const std::vector<DrawnPoint> missed = drawn(grapple, 900, 420, false), missed_whole = drawn(grapple, 900, 420, true);
	CHECK(missed.size() < missed_whole.size());
	std::vector<float> before, before_whole, after;
	for (const DrawnPoint &p : missed) {
		(p.at.x < 1199.0f ? before : after).push_back(p.color[3]);
	}
	for (const DrawnPoint &p : missed_whole) {
		if (p.at.x < 1199.0f) {
			before_whole.push_back(p.color[3]);
		}
	}
	CHECK(before == before_whole);
	CHECK(std::any_of(after.begin(), after.end(), [](float a) { return a < 0.9f; }));
	// the user's dump (grapple_1, 2026-11-06): the grapple planned, the cursor before the aim crosses its line off its
	// segment at (1132.8, 50): the path whole to there, fading on to the top edge
	{
		const std::filesystem::path root = RC_REPO_ROOT;
		rcg::SessionConfig cfgs[2];
		cfgs[1].full_preview = true;
		std::vector<DrawnPoint> got[2];
		for (int w = 0; w < 2; w++) {
			rcg::Session s(rcg::load_level((root / "data/levels/grapple_1.json").string()), cfgs[w]);
			s.teleport(rc::V2::of(914.0872802734375, 1043.9200439453125));
			plan_at(s, 998.0951538085938, 62.85713195800781);
			REQUIRE(s.plan().size() == 1);
			s.set_cursor(rc::V2::of(1026.03173828125, 535.2380981445312));
			s.settle();
			got[w] = path_points(s);
		}
		std::vector<float> to[2];
		for (int w = 0; w < 2; w++) {
			for (const DrawnPoint &p : got[w]) {
				if (p.at.y > 50.5f) {
					to[w].push_back(p.color[3]);
				}
			}
		}
		CHECK_FALSE(to[0].empty());
		CHECK(to[0] == to[1]); // (whole to the miss, 500 past the cursor)
	}
	// an unplanned mirror on the way: the walks part there -- the planned path goes on whole to the grapple, the physical
	// one fades past the mirror
	const char *mirrored = R"({"elements": [{"kind": "grapple", "start": [1200, 300], "end": [1200, 500]},
		{"kind": "mirror", "start": [1000, 420], "end": [1000, 500]}]})";
	const std::vector<DrawnPoint> diverged = drawn(mirrored, 900, 480, false), diverged_whole = drawn(mirrored, 900, 480, true);
	CHECK(diverged.size() < diverged_whole.size());
	CHECK(std::any_of(diverged.begin(), diverged.end(), [](const DrawnPoint &p) { return p.at.x > 1190.0f && p.color[3] > 0.99f; }));
	CHECK(std::any_of(diverged.begin(), diverged.end(), [](const DrawnPoint &p) { return p.at.x < 1000.0f && p.color[3] < 0.99f; }));
	// the user's second dump (grapple_1, 2026-11-07): the aim meets an unplanned terminal wall before the planned grapple:
	// the physical walk whole to the wall (the divergence), the planned one whole on to the grapple's line; nothing fades
	{
		const std::filesystem::path root = RC_REPO_ROOT;
		rcg::SessionConfig cfgs[2];
		cfgs[1].full_preview = true;
		std::vector<DrawnPoint> got[2];
		for (int w = 0; w < 2; w++) {
			rcg::Session s(rcg::load_level((root / "data/levels/grapple_1.json").string()), cfgs[w]);
			s.teleport(rc::V2::of(410.0, 563.9199829101562));
			plan_at(s, 1005.7142333984375, 60.31745910644531);
			REQUIRE(s.plan().size() == 1);
			s.set_cursor(rc::V2::of(554.9205932617188, 437.4603271484375));
			s.settle();
			const std::vector<rc::TraceStep> &m = s.preview_steps();
			if (w == 0) {
				CHECK(std::any_of(m.begin(), m.end(), [](const rc::TraceStep &st) { return st.type == rc::StepType::DivergedPostPlanned; }));
			}
			got[w] = path_points(s);
		}
		REQUIRE(got[0].size() == got[1].size());
		for (size_t k = 0; k < got[0].size(); k++) {
			CHECK(got[0][k].color[3] == got[1][k].color[3]);
		}
		CHECK(std::any_of(got[0].begin(), got[0].end(), [](const DrawnPoint &p) { return p.at.x > 950.0f && p.at.y < 70.0f; }));
	}
}

// Stage 205 (the user): the preview's fade as invariants, over sweeps of plans and cursors -- the planned path is whole
// as far as the plan's rules take it (to its cursor; on to a planned terminal the cursor comes before), only physical
// sections fade (the physical walk past the divergence, the planned walk's continuation by physical rules), each walk
// fades once, from one point on, never brightening again, the full preview fades nothing, and the display modes (Tab)
// show each walk alone as the merge does.
namespace {

enum class Walk { Physical, Planned };
bool in_walk(rc::StepType t, Walk w) {
	if (t == rc::StepType::Aligned || t == rc::StepType::AlignedPostPlanned) {
		return true;
	}
	return w == Walk::Physical ? t == rc::StepType::DivergedPhysical : t == rc::StepType::DivergedPlanned || t == rc::StepType::DivergedPostPlanned;
}

// the alphas of the path node's points in each display mode (0 merged, 1 physical, 2 planned)
std::array<std::vector<float>, 3> alphas_by_mode(rcg::Session &s) {
	std::array<std::vector<float>, 3> out;
	for (int m = 0; m < 3; m++) {
		REQUIRE(s.display_mode() == m);
		for (const DrawnPoint &p : path_points(s)) {
			out[size_t(m)].push_back(p.color[3]);
		}
		s.key(rcg::Action::CycleDisplay);
	}
	return out;
}

void check_fade(rcg::Session &s, rcg::Session &whole, const std::string &where, bool modes) {
	INFO(where);
	const std::vector<rc::TraceStep> &m = s.preview_steps();
	const std::vector<std::array<double, 2>> seen = s.preview_seen();
	REQUIRE(seen.size() == m.size());
	const rc::TracePath *planned = s.planned_trace();
	const bool reached = planned != nullptr && planned->cursor_index >= 0;
	// the planned walk went on past its cursor by the plan's rules and ended at the plan's last entry, its first significant
	// event there: a planned terminal stopped it (an unplanned passage of infinity or the origin ends the plan's rules)
	bool to_terminal = false;
	if (reached && !s.plan().empty() && !planned->truncated && int(planned->steps.size()) > planned->cursor_index) {
		size_t first = planned->steps.size() - 1;
		for (size_t i = size_t(planned->cursor_index); i < planned->steps.size(); i++) {
			if (planned->steps[i].significant) {
				first = i;
				break;
			}
		}
		to_terminal = first + 1 == planned->steps.size() && planned->steps.back().surface_id == s.plan().back().surface_id;
	}
	// Stage 206: a planned walk that misses its next entry runs whole to the entry's counterpart
	std::optional<size_t> counterpart;
	for (size_t i = 0; i < m.size() && !counterpart; i++) {
		if (m[i].counterpart) {
			counterpart = i;
		}
	}
	for (size_t i = 0; i < m.size(); i++) {
		const rc::StepType t = m[i].type;
		INFO("step " << i << " type " << int(t) << " seen " << seen[i][0] << " -> " << seen[i][1]);
		CHECK(seen[i][0] >= seen[i][1]); // (a step fades on, never back)
		const bool shown = seen[i][0] == 1.0 && seen[i][1] == 1.0;
		if (counterpart && i <= *counterpart && in_walk(t, Walk::Planned)) {
			CHECK(shown);
		}
		// the planned path to the cursor is whole: the common part before it, and a diverged planned walk to it
		if (t == rc::StepType::Aligned || (t == rc::StepType::DivergedPlanned && reached)) {
			CHECK(shown);
		}
		// on to a planned terminal the cursor comes before, whole too (its hit, or its miss off the segment)
		if (to_terminal && in_walk(t, Walk::Planned)) {
			CHECK(shown);
		}
		// only physical sections fade (and a planned walk that never reaches its cursor: Stage 200)
		if (!shown) {
			CHECK((t == rc::StepType::DivergedPhysical || t == rc::StepType::AlignedPostPlanned || t == rc::StepType::DivergedPostPlanned
				|| (t == rc::StepType::DivergedPlanned && !reached)));
		}
	}
	// past the cursor the planned path shows whole only on its way to a planned terminal: otherwise it fades
	if (!to_terminal) {
		bool post = false, fades = false;
		for (size_t i = 0; i < m.size(); i++) {
			if ((m[i].type == rc::StepType::AlignedPostPlanned || m[i].type == rc::StepType::DivergedPostPlanned) && !(m[i].start == m[i].end)) {
				post = true;
				fades = fades || seen[i][1] < 1.0;
			}
		}
		CHECK((!post || fades));
	}
	// each walk fades continuously along its way: a step begins as faded as the one before it ended
	for (Walk w : {Walk::Physical, Walk::Planned}) {
		std::optional<size_t> prev;
		for (size_t i = 0; i < m.size(); i++) {
			if (!in_walk(m[i].type, w)) {
				continue;
			}
			if (prev) {
				INFO("walk " << int(w) << " steps " << *prev << " -> " << i);
				CHECK(std::abs(seen[i][0] - seen[*prev][1]) < 1e-6);
			}
			prev = i;
		}
	}
	// the full preview fades nothing, and draws the same steps
	REQUIRE(whole.preview_steps().size() == m.size());
	for (const std::array<double, 2> &a : whole.preview_seen()) {
		CHECK((a[0] == 1.0 && a[1] == 1.0));
	}
	if (modes) {
		// each walk alone (Tab) fades exactly when it does in the merge
		const std::array<std::vector<float>, 3> faded = alphas_by_mode(s), full = alphas_by_mode(whole);
		for (Walk w : {Walk::Physical, Walk::Planned}) {
			bool walk_whole = true;
			for (size_t i = 0; i < m.size(); i++) {
				walk_whole = walk_whole && (!in_walk(m[i].type, w) || seen[i][1] > 0.999);
			}
			const size_t mode = w == Walk::Physical ? 1 : 2;
			INFO("display mode " << mode);
			CHECK((faded[mode] == full[mode]) == walk_whole);
		}
	}
}

void sweep_fade(const std::function<rcg::Level()> &level, const std::function<void(rcg::Session &)> &setup, double x0, double x1,
		double y0, double y1, double step, const std::string &name) {
	rcg::SessionConfig full;
	full.full_preview = true;
	rcg::Session s(level()), whole(level(), full);
	setup(s);
	setup(whole);
	int k = 0;
	for (double y = y0; y <= y1; y += step) {
		for (double x = x0; x <= x1; x += step) {
			for (rcg::Session *q : {&s, &whole}) {
				q->set_cursor(rc::V2::of(x, y));
				q->settle();
			}
			check_fade(s, whole, name + " cursor (" + std::to_string(x) + ", " + std::to_string(y) + ")", k++ % 5 == 0);
		}
	}
}

} // namespace

TEST_CASE("preview: the fade's invariants over plans and cursors (Stage 205)") {
	const char *elements = R"({"elements": [{"kind": "grapple", "start": [1200, 300], "end": [1200, 500]},
		{"kind": "mirror", "start": [1000, 600], "end": [1000, 800]}, {"kind": "wall", "start": [900, 260], "end": [900, 380]},
		{"kind": "mirror", "start": [700, 600], "end": [700, 800]}]})";
	const auto room = [&] { return level_of(elements); };
	const std::vector<std::pair<std::string, std::vector<std::array<double, 2>>>> plans = {
		{"no plan", {}}, {"the grapple", {{1200, 400}}}, {"the mirror", {{1000, 700}}}, {"the mirror, the grapple", {{1000, 700}, {1200, 400}}}};
	for (const auto &[name, clicks] : plans) {
		sweep_fade(room, [&](rcg::Session &s) {
			for (const std::array<double, 2> &c : clicks) {
				plan_at(s, c[0], c[1]);
			}
			REQUIRE(s.plan().size() == clicks.size());
		}, 580, 1340, 260, 820, 40, "room, " + name);
	}
	// Stage 206: a planned circle the aim misses from most places (the cone is narrow): the counterpart's fade
	const char *ball = R"({"room": {"rect": [560, 240, 800, 600], "built": false},
		"objects": [{"name": "ball", "circle": {"center": [1200, 300], "radius": 16}, "face": "mirror", "kind": "static"}]})";
	sweep_fade([&] { return level_of(ball); }, [&](rcg::Session &s) {
		plan_at(s, 1200, 284);
		REQUIRE(s.plan().size() == 1);
	}, 580, 1340, 260, 820, 40, "the mirror ball");
	// the user's grapple_1 dumps (2026-11-06, 2026-11-07): the grapple planned from two places
	const std::filesystem::path root = RC_REPO_ROOT;
	const auto grapple_1 = [&] { return rcg::load_level((root / "data/levels/grapple_1.json").string()); };
	const std::vector<std::array<double, 4>> dumps = {{914.0872802734375, 1043.9200439453125, 998.0951538085938, 62.85713195800781},
		{410.0, 563.9199829101562, 1005.7142333984375, 60.31745910644531}};
	for (const std::array<double, 4> &d : dumps) {
		sweep_fade(grapple_1, [&](rcg::Session &s) {
			s.teleport(rc::V2::of(d[0], d[1]));
			plan_at(s, d[2], d[3]);
			REQUIRE(s.plan().size() == 1);
		}, 20, 1900, 20, 1060, 60, "grapple_1 from (" + std::to_string(d[0]) + ", " + std::to_string(d[1]) + ")");
	}
}



// Stage 206 (the user's dumps, pulling_1): a planned terminal missed, the cursor beyond its counterpart: the planned walk
// runs to infinity and is clipped onto the level's edge, where the physical walk hits the edge wall -- the same drawn
// point an ulp apart. The truncation marker there fades with the step (it matched the step's end by float32 equality
// and flickered between white, whole, and the step's colour, faded, as the cursor moved)
TEST_CASE("preview: a truncation marker at the same drawn point as a faded step's end fades with it (Stage 206)") {
	const std::filesystem::path root = RC_REPO_ROOT;
	for (const double cx : {736.507874, 735.238037}) {
		CAPTURE(cx);
		rcg::Session s(rcg::load_level((root / "data/levels/pulling_1.json").string()));
		s.teleport(rc::V2::of(550, 1043.92004));
		plan_at(s, 684.4443969726562, 1050.7935791015625);
		REQUIRE(s.plan().size() == 1);
		s.set_cursor(rc::V2::of(cx, 688.888855));
		s.settle();
		const std::vector<rc::TraceStep> &m = s.preview_steps();
		REQUIRE(m.size() == 2);
		REQUIRE(s.planned_trace()->truncated);
		const rc::V2 at = m.back().end;
		float strongest = 0.0f;
		for (const rcg::render::FrameNode &n : s.draw_frame()) {
			for (const rcg::render::Call &c : *n.calls) {
				if (c.kind == rcg::render::Kind::Polyline && c.pts.size() == 9 && dist(c.pts[0], at) < 12.0) {
					strongest = std::max(strongest, c.colors[0][3]);
				}
			}
		}
		CHECK(strongest < 0.01f); // (the stop marker sits where the step has faded out)
	}
}
