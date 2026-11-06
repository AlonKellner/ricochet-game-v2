// Stage 202 (GAME_SPEC 9.12, docs/TDD_80): openings (Stage 204: a motion in the mode "opening"). An element's opening plays
// its steps (rotations about a pivot, translations; Stage 203: by two points, given
// where the step begins) from its closed pose
// while its flag holds, and back while it does not; the shot sees the world as
// fired, the swing plays once time runs, and no shot is fired mid-swing.
#include "rcg/level.hpp"
#include "rcg/render.hpp"
#include "rcg/session.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <string>

namespace {

using json = nlohmann::json;

const double QUARTER = 1.5707963267948966;

rcg::Level level_of(const char *extra) {
	json lv = json::parse(R"({"schema": "rc-level/2", "player": {"start": [660, 540]}, "room": {"rect": [560, 240, 800, 600], "built": true},
		"elements": []})");
	const json e = json::parse(extra);
	for (auto it = e.begin(); it != e.end(); ++it) {
		lv[it.key()] = it.value();
	}
	return rcg::parse_level_json(lv);
}

// the first surface of the level's opening `o`
const rcg::BuiltSurface &swinging(const rcg::Session &s, size_t o = 0) {
	const rcg::BuiltSurface *b = s.world().by_id(s.level().surfaces[s.level().openings[o].surfaces.front()].ids.front());
	REQUIRE(b != nullptr);
	return *b;
}

// a button on the shot's line, then a wall that swings a quarter turn about its top (its start, the hinge)
const char *const GATE = R"({"elements": [
	{"kind": "button", "name": "b", "opens": "gate", "start": [900, 450], "end": [900, 630]},
	{"kind": "wall", "name": "gate", "start": [1000, 400], "end": [1000, 600],
	 "motion_mode": "opening", "motion": {"steps": [{"rotate": 1.5707963267948966}]}}]})";

} // namespace

TEST_CASE("opening: a wall swings about its hinge while its flag holds and back when it does not; the shot sees it as fired") {
	rcg::Session s(level_of(GATE));
	CHECK(s.openings() == std::vector<double>{0.0});
	CHECK(swinging(s).swings);
	CHECK(rcg::is_door(swinging(s))); // (the door's look)
	s.set_cursor(rc::V2::of(1250, 540));
	s.settle();
	s.fire(-1, 1.0 / 60.0);
	CHECK(s.flags().is_true("door:gate"));
	CHECK(s.world().by_id(swinging(s).id)->seg.e == rc::V2::of(1000, 600)); // not moved by the shot
	CHECK(s.swinging());
	CHECK_FALSE(s.can_fire()); // no shot mid-swing
	s.hold({}, 15); // a quarter second: half the turn
	CHECK(s.openings()[0] == doctest::Approx(0.25));
	CHECK(swinging(s).seg.e.x == doctest::Approx(1000 - 200 * std::sin(QUARTER / 2)).epsilon(1e-5));
	CHECK(swinging(s).seg.e.y == doctest::Approx(400 + 200 * std::cos(QUARTER / 2)).epsilon(1e-5));
	CHECK(s.ready() < 1.0); // (the preview muted while it swings)
	s.hold({}, 15);
	CHECK(s.openings()[0] == 0.5);
	CHECK(swinging(s).seg.s == rc::V2::of(1000, 400)); // the hinge stays
	CHECK(swinging(s).seg.e.x == doctest::Approx(800).epsilon(1e-5));
	CHECK(swinging(s).seg.e.y == doctest::Approx(400).epsilon(1e-5));
	CHECK_FALSE(s.swinging());
	CHECK_FALSE(s.can_fire()); // ... and settled, it is ready as after a move
	s.hold({}, 20);
	CHECK(s.can_fire());
	s.fire(-1, 1.0 / 60.0); // the button toggles it back; the arrow passes where the gate stood
	CHECK_FALSE(s.flags().is_true("door:gate"));
	s.hold({}, 30);
	CHECK(s.openings()[0] == 0.0);
	CHECK(swinging(s).seg.e == rc::V2::of(1000, 600)); // exactly closed again
}

TEST_CASE("opening: a flag that switches mid-swing turns it where it stands; undo and reset restore its time") {
	rcg::Session s(level_of(GATE));
	s.set_cursor(rc::V2::of(1250, 540));
	s.settle();
	s.fire(-1, 1.0 / 60.0);
	s.hold({}, 12);
	const double at = s.openings()[0];
	CHECK(at == doctest::Approx(0.2));
	s.key(rcg::Action::Undo); // the checkpoint of the shot: closed, the flag unset
	CHECK(s.openings()[0] == 0.0);
	CHECK(swinging(s).seg.e == rc::V2::of(1000, 600));
	CHECK_FALSE(s.swinging());
	s.key(rcg::Action::Redo); // back to where it was, still swinging
	CHECK(s.openings()[0] == at);
	CHECK(s.swinging());
	s.hold({}, 40);
	CHECK(s.openings()[0] == 0.5);
	s.key(rcg::Action::Reset);
	CHECK(s.openings()[0] == 0.0);
	CHECK(swinging(s).seg.e == rc::V2::of(1000, 600));
	// a detector's flag (an expression): it opens while the player stands in it, and turns back on leaving
	rcg::Session d(level_of(R"({"detectors": [{"name": "plate", "region": {"rect": [600, 500, 100, 80]}}],
		"elements": [{"kind": "wall", "name": "w", "start": [1000, 400], "end": [1000, 600],
		  "motion_mode": "opening", "motion": {"flag": "det:plate && !locked", "steps": [{"translate": [0, -100], "seconds": 1}]}}]})"));
	d.hold({}, 10);
	CHECK(d.flags().is_true("det:plate"));
	CHECK(d.openings()[0] == doctest::Approx(9.0 / 60.0)); // (the detector writes after the tick's swing)
	d.hold({"move_right"}, 30); // off the plate: back where it stands
	CHECK_FALSE(d.flags().is_true("det:plate"));
	CHECK(d.openings()[0] < 9.0 / 60.0);
	d.hold({}, 60);
	CHECK(d.openings()[0] == 0.0);
	CHECK(swinging(d).seg.s == rc::V2::of(1000, 400));
}

TEST_CASE("opening: steps in order -- a rotation's pivot is where its step begins; a level starts settled") {
	const rcg::Level lv = level_of(R"({"elements": [{"kind": "wall", "name": "g", "start": [0, 0], "end": [100, 0],
		"motion_mode": "opening", "motion": {"steps": [{"translate": [100, 0]}, {"rotate": 1.5707963267948966, "pivot": [100, 0], "seconds": 1}]}}]})");
	REQUIRE(lv.openings.size() == 1);
	const rcg::Opening &o = lv.openings[0];
	CHECK(o.flag == "door:g");
	CHECK(o.total() == 1.5);
	CHECK(rcg::opening_pose(o, 0.0) == rc::Xf2::identity());
	const rc::Xf2 half = rcg::opening_pose(o, 0.25), mid = rcg::opening_pose(o, 1.0), open = rcg::opening_pose(o, 1.5);
	CHECK(half.xform(rc::V2::of(0, 0)).x == doctest::Approx(50));
	CHECK(open.xform(rc::V2::of(0, 0)).x == doctest::Approx(100)); // the pivot, where the translation put the start
	CHECK(open.xform(rc::V2::of(0, 0)).y == doctest::Approx(0).epsilon(1e-4));
	CHECK(open.xform(rc::V2::of(100, 0)).x == doctest::Approx(100).epsilon(1e-4));
	CHECK(open.xform(rc::V2::of(100, 0)).y == doctest::Approx(100));
	CHECK(mid.xform(rc::V2::of(100, 0)).x == doctest::Approx(100 + 100 * std::cos(QUARTER / 2)));
	CHECK(mid.xform(rc::V2::of(100, 0)).y == doctest::Approx(100 * std::sin(QUARTER / 2)));
	// its flag set from the start: open from the start, and nothing swings
	rcg::Session s(level_of(R"({"flags": {"door:gate": true}, "elements": [
		{"kind": "wall", "name": "gate", "start": [1000, 400], "end": [1000, 600], "motion_mode": "opening", "motion": {"steps": [{"rotate": 1.5707963267948966}]}}]})"));
	CHECK(s.openings()[0] == 0.5);
	CHECK_FALSE(s.swinging());
	CHECK(swinging(s).seg.e.x == doctest::Approx(800).epsilon(1e-5));
	CHECK(s.record(json::object())["openings"] == json::array({0.5}));
	// (Stage 203) a step by numbers shows derived points: a rotation's pivot (default: the start where the step
	// begins) and its aim (where the start -> end turns to), a translation's from (the start there) and to
	const rcg::Opening::Step &t = o.steps[0], &r = o.steps[1];
	CHECK(t.points == rcg::PointPair{{{0, 0}, {100, 0}}});
	CHECK(r.points[0] == std::array<double, 2>{100, 0});
	CHECK(r.points[1][0] == doctest::Approx(100));
	CHECK(r.points[1][1] == doctest::Approx(100));
}

TEST_CASE("opening: steps by two points (Stage 203) -- a translation's from and to, a rotation's pivot and aim") {
	// up 50, then about the start (where the translation put it) until start -> end points down
	const rcg::Level lv = level_of(R"({"elements": [{"kind": "wall", "name": "g", "start": [0, 0], "end": [100, 0],
		"motion_mode": "opening", "motion": {"steps": [{"translate": [[30, 30], [30, -20]]}, {"rotate": [[0, -50], [0, 50]]}]}}]})");
	const rcg::Opening &o = lv.openings[0];
	CHECK(o.steps[0].dx == 0);
	CHECK(o.steps[0].dy == -50);
	CHECK(o.steps[1].angle == QUARTER); // (exactly: atan2 of the axes)
	CHECK(o.steps[1].px == 0);
	CHECK(o.steps[1].py == -50);
	CHECK(o.steps[1].points == rcg::PointPair{{{0, -50}, {0, 50}}}); // (as written)
	const rc::Xf2 open = rcg::opening_pose(o, o.total());
	CHECK(open.xform(rc::V2::of(0, 0)).x == doctest::Approx(0).epsilon(1e-4));
	CHECK(open.xform(rc::V2::of(0, 0)).y == doctest::Approx(-50));
	CHECK(open.xform(rc::V2::of(100, 0)).x == doctest::Approx(0).epsilon(1e-4));
	CHECK(open.xform(rc::V2::of(100, 0)).y == doctest::Approx(50));
	// the shortest way: aimed up-left, it turns counter-clockwise; aimed straight back, half a turn clockwise; an aim
	// sets the direction from the step's pose (after a quarter turn, back to the axis is a quarter turn back)
	auto angles = [&](const char *steps) {
		const rcg::Level l = level_of((std::string(R"({"elements": [{"kind": "wall", "name": "g", "start": [0, 0], "end": [100, 0], "motion_mode": "opening", "motion": {"steps": )")
				+ steps + "}}]}").c_str());
		std::vector<double> out;
		for (const rcg::Opening::Step &st : l.openings[0].steps) {
			out.push_back(st.angle);
		}
		return out;
	};
	CHECK(angles(R"([{"rotate": [[0, 0], [-10, -10]]}])")[0] == doctest::Approx(-3 * QUARTER / 2));
	CHECK(angles(R"([{"rotate": [[0, 0], [-10, 0]]}])")[0] == doctest::Approx(2 * QUARTER));
	const std::vector<double> two = angles(R"([{"rotate": [[0, 0], [0, 10]]}, {"rotate": [[0, 0], [10, 0]]}])");
	CHECK(two[0] == QUARTER);
	CHECK(two[1] == doctest::Approx(-QUARTER));
	// a circle: the pivot to its center sets its direction
	const rcg::Level c = level_of(R"({"elements": [{"kind": "wall", "name": "c", "circle": {"center": [100, 0], "radius": 20},
		"motion_mode": "opening", "motion": {"steps": [{"rotate": [[0, 0], [0, 100]]}]}}]})");
	CHECK(c.openings[0].steps[0].angle == QUARTER);
	CHECK(rcg::opening_pose(c.openings[0], 0.5).xform(rc::V2::of(100, 0)).y == doctest::Approx(100));
}

TEST_CASE("opening: the demo level -- the plate slides the shield away, the button swings the gate open") {
	const std::filesystem::path root = RC_REPO_ROOT;
	rcg::Session s(rcg::load_level((root / "data/levels/objectives/swing_gate.json").string()));
	REQUIRE(s.level().openings.size() == 2);
	s.set_cursor(rc::V2::of(1250, 540));
	s.settle();
	s.fire(-1, 1.0 / 60.0);
	CHECK_FALSE(s.flags().is_true("door:gate")); // the shield took the arrow
	s.hold({"move_down"}, 30); // onto the plate: the shield slides up
	CHECK(s.flags().is_true("det:plate"));
	s.hold({}, 40);
	CHECK_FALSE(s.swinging());
	s.set_cursor(rc::V2::of(1250, 540));
	s.settle();
	s.fire(-1, 1.0 / 60.0);
	CHECK(s.flags().is_true("door:gate"));
	s.hold({}, 60);
	CHECK_FALSE(s.swinging());
	s.hold({"move_up"}, 40);
	s.hold({"move_right"}, 180);
	s.hold({"move_down"}, 120);
	INFO("player ", s.player().x, ", ", s.player().y);
	CHECK(s.won());
}

TEST_CASE("opening: the preview draws the pose a consequence sends it to, dashed amber") {
	rcg::Session s(level_of(GATE));
	auto amber = [&]() {
		std::vector<rc::V2> at;
		for (const auto &node : s.draw_frame()) {
			if (node.key.kind != rcg::render::NodeKind::Glow) {
				continue;
			}
			for (const rcg::render::Call &c : *node.calls) {
				if (!c.colors.empty() && c.colors[0][0] == 1.0f && c.colors[0][1] == 0.72f && c.colors[0][2] == 0.25f) {
					at.insert(at.end(), c.pts.begin(), c.pts.end());
				}
			}
		}
		return at;
	};
	s.set_cursor(rc::V2::of(660, 300)); // the shot misses the button: nothing to show
	s.settle();
	CHECK(amber().empty());
	s.set_cursor(rc::V2::of(1250, 540)); // through the button: the gate will swing open
	s.settle();
	const std::vector<rc::V2> at = amber();
	REQUIRE_FALSE(at.empty());
	for (const rc::V2 &p : at) { // (dashes along its open pose: y 400, x from 800 to 1000)
		CHECK(p.y == doctest::Approx(400).epsilon(1e-4));
		CHECK(p.x >= 799.9f);
		CHECK(p.x <= 1000.1f);
	}
}

TEST_CASE("opening: the schema") {
	auto err = [](const char *extra) {
		try {
			level_of(extra);
		} catch (const rcg::LevelError &e) {
			return std::string(e.what());
		}
		return std::string("no error");
	};
	auto has = [&](const char *extra, const char *what) { return err(extra).find(what) != std::string::npos; };
	CHECK(has(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion_mode": "opening", "motion": {"steps": []}}]})", "an array of {rotate"));
	CHECK(has(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion_mode": "opening", "motion": {"steps": [{"spin": 1}]}}]})", "{translate, seconds} expected"));
	CHECK(has(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion_mode": "opening", "motion": {"steps": [{"translate": [1, 0], "seconds": 0}]}}]})",
			"a positive number of seconds"));
	CHECK(has(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion_mode": "opening", "motion": {"steps": [{"translate": [1, 0], "pivot": [0, 0]}]}}]})",
			"pivot"));
	CHECK(has(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion_mode": "opening", "motion": {"steps": [{"rotate": [[1, 1], [1, 1]]}]}}]})",
			"two different points expected"));
	CHECK(has(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion_mode": "opening", "motion": {"steps": [{"rotate": [[1, 1], [2, 1]], "pivot": [0, 0]}]}}]})",
			"pivot"));
	CHECK(has(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion_mode": "opening", "motion": {"steps": [{"translate": [[1, 1], [2]]}]}}]})",
			"translate[1]"));
	CHECK(has(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion_mode": "opening", "motion": {"flag": "a &&", "steps": [{"translate": [1, 0]}]}}]})",
			"motion.flag"));
	CHECK(has(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion_mode": "opening", "motion": {"keys": [[0, 0, 0, 0]]}}]})",
			"an opening plays steps"));
	CHECK(has(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion_mode": "door", "motion": {"steps": [{"translate": [1, 0]}]}}]})",
			"cycle or opening expected"));
	CHECK(has(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion": {"keys": [[0, 0, 0, 0]], "steps": [{"translate": [1, 0]}]}}]})",
			"steps or keys, not both"));
}

TEST_CASE("motion: steps played by the cycle, there and back; the same steps an opening plays (Stage 204)") {
	const char *const STEPS = R"("motion": {"steps": [{"translate": [[0, 0], [100, 0]]}, {"rotate": [[1100, 400], [1100, 500]]}], "flag": "det:x"})";
	rcg::Session s(level_of((std::string(R"({"cycle": {"period": 4}, "elements": [{"kind": "wall", "name": "w", "start": [1000, 400], "end": [1100, 400], )")
			+ STEPS + "}]}").c_str()));
	REQUIRE(s.level().motions.size() == 1);
	REQUIRE(s.level().motions[0].played);
	CHECK(s.level().openings.empty()); // (the cycle plays it: no flag, no swing)
	const rcg::Opening &pl = *s.level().motions[0].played;
	CHECK(rcg::played_time(pl, 0.25) == 0.5); // the steps' second forward over half the cycle
	CHECK(rcg::played_time(pl, 0.5) == 1.0);
	CHECK(rcg::played_time(pl, 0.75) == 0.5); // ... and back
	const int id = s.level().surfaces[s.level().motions[0].surfaces.front()].ids.front();
	auto seg = [&]() { return s.world().by_id(id)->seg; };
	s.hold({}, 60); // phase 0.25: the move done
	CHECK(seg().s.x == doctest::Approx(1100).epsilon(1e-5));
	CHECK(seg().e.x == doctest::Approx(1200).epsilon(1e-5));
	s.hold({}, 60); // phase 0.5: the turn done (about the moved start, pointing down)
	CHECK(seg().e.x == doctest::Approx(1100).epsilon(1e-5));
	CHECK(seg().e.y == doctest::Approx(500).epsilon(1e-5));
	CHECK(s.can_fire()); // (a cycle's motion never holds the shot)
	s.hold({}, 120); // and back
	CHECK(seg().s.x == doctest::Approx(1000).epsilon(1e-4));
	CHECK(seg().e.y == doctest::Approx(400).epsilon(1e-4));
	// the same element with motion_mode opening: its flag (kept in the cycle's mode) plays them
	const rcg::Level op = level_of((std::string(R"({"elements": [{"kind": "wall", "name": "w", "start": [1000, 400], "end": [1100, 400], "motion_mode": "opening", )")
			+ STEPS + "}]}").c_str());
	REQUIRE(op.openings.size() == 1);
	CHECK(op.openings[0].flag == "det:x");
	CHECK(op.motions.empty());
}
