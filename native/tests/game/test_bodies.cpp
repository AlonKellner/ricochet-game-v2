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

#include <cmath>
#include <filesystem>
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
