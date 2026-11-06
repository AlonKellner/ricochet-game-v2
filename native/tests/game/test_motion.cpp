// Stage 151 (GAME_SPEC 9.8, docs/TDD_56): moving surfaces. An element's motion
// keys an offset and a rotation over the level's cycle; the cycle runs on its
// own, a scroll pauses and sets it, a shot or the idle seconds run it again.
#include "rcg/level.hpp"
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

const rcg::BuiltSurface &surface(const rcg::Session &s, int id) {
	const rcg::BuiltSurface *b = s.world().by_id(id);
	REQUIRE(b != nullptr);
	return *b;
}

} // namespace

TEST_CASE("motion: a moving wall follows its keys; a scroll pauses the cycle, idle seconds run it again") {
	rcg::Session s(level_of(R"({"cycle": {"period": 4, "resume_after": 1},
		"elements": [{"kind": "wall", "name": "w", "start": [900, 400], "end": [900, 600],
		              "motion": {"keys": [[0, 0, 0, 0], [0.5, 100, 0, 0]]}}]})"));
	CHECK(surface(s, 5).seg.s.x == 900);
	s.hold({}, 60); // a second: a quarter of the cycle, halfway to the second key
	CHECK(s.phase() == doctest::Approx(0.25));
	CHECK(surface(s, 5).seg.s.x == doctest::Approx(950));
	s.hold({}, 60); // at the key
	CHECK(surface(s, 5).seg.s.x == doctest::Approx(1000));
	s.hold({}, 120); // back (the loop)
	CHECK(surface(s, 5).seg.s.x == doctest::Approx(900).epsilon(1e-4));
	s.scroll(5); // a tenth of the cycle, paused
	CHECK_FALSE(s.cycle_auto());
	CHECK(s.phase() == doctest::Approx(0.1).epsilon(1e-6));
	CHECK(s.hud() == "Cycle: paused (a shot runs it again)");
	s.hold({}, 30);
	CHECK(s.phase() == doctest::Approx(0.1).epsilon(1e-6)); // paused
	s.hold({}, 40); // over resume_after (1 s): it runs again
	CHECK(s.cycle_auto());
	CHECK(s.phase() > 0.1);
}

TEST_CASE("motion: a rotation about the pivot; a portal moves with its image; undo restores the phase") {
	rcg::Session s(level_of(R"({"cycle": {"period": 4}, "elements": [
		{"kind": "mirror", "name": "m", "start": [900, 400], "end": [900, 600],
		 "motion": {"keys": [[0, 0, 0, 0], [0.5, 0, 0, 1.5707963267948966]], "pivot": [900, 500]}},
		{"kind": "portal", "name": "p", "d": [200, 0], "start": [700, 300], "end": [700, 400],
		 "motion": {"keys": [[0, 0, 0, 0], [0.5, 0, 100, 0]]}}]})"));
	s.hold({}, 120); // phase 0.5: the mirror a quarter turn about its middle, the portal 100 lower
	const rcg::BuiltSurface &m = surface(s, 5);
	CHECK(m.seg.s.x == doctest::Approx(1000).epsilon(1e-4));
	CHECK(m.seg.s.y == doctest::Approx(500).epsilon(1e-4));
	CHECK(surface(s, 6).seg.s.y == doctest::Approx(400).epsilon(1e-4));
	CHECK(surface(s, 7).seg.s.x == doctest::Approx(900).epsilon(1e-4)); // the image, moved with it
	CHECK(surface(s, 7).seg.s.y == doctest::Approx(400).epsilon(1e-4));
	s.scroll(-5);
	s.set_cursor(rc::V2::of(1200, 540));
	s.settle();
	s.fire(-1, 1.0 / 60.0); // a checkpoint at phase 0.4, paused; the shot runs the cycle again
	CHECK(s.cycle_auto());
	s.hold({}, 30);
	s.key(rcg::Action::Undo);
	CHECK(s.phase() == doctest::Approx(0.4).epsilon(1e-6));
	CHECK_FALSE(s.cycle_auto());
}

TEST_CASE("motion: the demo level -- the slider blocks the shot until the cycle is set") {
	const std::filesystem::path root = RC_REPO_ROOT;
	rcg::Session s(rcg::load_level((root / "data/levels/objectives/timing_gate.json").string()));
	s.set_cursor(rc::V2::of(1250, 540));
	s.settle();
	s.fire(-1, 1.0 / 60.0);
	CHECK_FALSE(s.flags().is_true("door:gate")); // the slider took the arrow
	s.scroll(25); // half a cycle: the slider out of the way
	s.settle();
	s.fire(-1, 1.0 / 60.0);
	CHECK(s.flags().is_true("door:gate"));
	s.hold({"move_right"}, 180);
	s.hold({"move_down"}, 90);
	CHECK(s.won());
}

TEST_CASE("motion: keys by two points (Stage 203) -- from the element as written, lowered about the motion's pivot") {
	// a wall [0, 0]-[100, 0] (its pivot: its middle, [50, 0]); at 0.5 turned about its start to point down
	const rcg::Level lv = level_of(R"({"elements": [{"kind": "wall", "name": "w", "start": [0, 0], "end": [100, 0],
		"motion": {"keys": [{"phase": 0}, {"phase": 0.25, "translate": [[10, 10], [40, 50]]}, {"phase": 0.5, "rotate": [[0, 0], [0, 10]]},
		                    [0.75, 0, -200, 0]]}}]})");
	const rcg::Motion &m = lv.motions[0];
	REQUIRE(m.keys.size() == 4);
	CHECK(m.keys[0] == std::array<double, 4>{0, 0, 0, 0});
	CHECK(m.keys[1] == std::array<double, 4>{0.25, 30, 40, 0});
	CHECK(m.keys[2][3] == QUARTER);
	CHECK(m.keys[2][1] == doctest::Approx(-50)); // R(a) (P - c) + c - P: [0, 50] - [50, 0]
	CHECK(m.keys[2][2] == doctest::Approx(50));
	// the points the editor shows: as written, or derived from numbers (from the pivot); none for an absent part
	CHECK_FALSE(m.points[0].translate);
	CHECK_FALSE(m.points[0].rotate);
	CHECK(*m.points[1].translate == rcg::PointPair{{{10, 10}, {40, 50}}});
	CHECK(*m.points[2].rotate == rcg::PointPair{{{0, 0}, {0, 10}}});
	CHECK(*m.points[3].translate == rcg::PointPair{{{50, 0}, {50, -200}}});
	CHECK_FALSE(m.points[3].rotate);
	// in the game: at 0.5 the wall hangs from its start
	rcg::Session s(level_of(R"({"cycle": {"period": 4}, "elements": [{"kind": "wall", "name": "w", "start": [900, 400], "end": [1000, 400],
		"motion": {"keys": [{"phase": 0}, {"phase": 0.5, "rotate": [[900, 400], [900, 500]]}]}}]})"));
	s.hold({}, 120);
	CHECK(surface(s, 5).seg.s.x == doctest::Approx(900).epsilon(1e-4));
	CHECK(surface(s, 5).seg.s.y == doctest::Approx(400).epsilon(1e-4));
	CHECK(surface(s, 5).seg.e.x == doctest::Approx(900).epsilon(1e-4));
	CHECK(surface(s, 5).seg.e.y == doctest::Approx(500).epsilon(1e-4));
	// a key's turn is the one nearest the previous key's: 3/4 of a half turn, then on past the half turn (not back)
	const rcg::Level u = level_of(R"({"elements": [{"kind": "wall", "name": "w", "start": [0, 0], "end": [100, 0],
		"motion": {"keys": [{"phase": 0, "rotate": [[0, 0], [-10, 10]]}, {"phase": 0.5, "rotate": [[0, 0], [-10, -10]]}]}}]})");
	CHECK(u.motions[0].keys[0][3] == doctest::Approx(3 * QUARTER / 2));
	CHECK(u.motions[0].keys[1][3] == doctest::Approx(5 * QUARTER / 2));
}

TEST_CASE("motion: the schema") {
	auto err = [](const char *extra) {
		try {
			level_of(extra);
		} catch (const rcg::LevelError &e) {
			return std::string(e.what());
		}
		return std::string("no error");
	};
	CHECK(err(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion": {"keys": [[0.5, 0, 0, 0], [0.2, 1, 0, 0]]}}]})")
					.find("phases in [0, 1), increasing")
			!= std::string::npos);
	CHECK(err(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion": {"keys": []}}]})").find("[phase, dx, dy, angle]") != std::string::npos);
	CHECK(err(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion": {"keys": [{"translate": [1, 0]}]}}]})").find("phase") != std::string::npos);
	CHECK(err(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion": {"keys": [{"phase": 0, "spin": 1}]}}]})").find("spin") != std::string::npos);
	CHECK(err(R"({"elements": [{"kind": "wall", "start": [0, 0], "end": [9, 0], "motion": {"keys": [{"phase": 0, "rotate": [[0, 0], [0, 0]]}]}}]})")
					.find("two different points")
			!= std::string::npos);
	CHECK(err(R"({"cycle": {"period": 0}, "elements": []})").find("a positive number of seconds") != std::string::npos);
}
