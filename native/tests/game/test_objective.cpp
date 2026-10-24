// Stage 145 (GAME_SPEC 3.2, 24.5-24.6, docs/TDD_50): the goal and the
// detectors. A level with a goal is won when the player stands in it while its
// conditions hold; a level without one when every target is hit. Detectors
// write flags as the player moves: a door opens as the player steps on a plate.
#include "rcg/level.hpp"
#include "rcg/model.hpp"
#include "rcg/session.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

rcg::Level level_of(json extra) {
	json lv = {{"schema", "rc-level/2"}, {"player", {{"start", {660.0, 540.0}}}}, {"room", {{"rect", {560.0, 240.0, 800.0, 600.0}}, {"built", true}}},
		{"elements", json::array()}};
	for (auto it = extra.begin(); it != extra.end(); ++it) {
		lv[it.key()] = it.value();
	}
	return rcg::parse_level_json(lv);
}

const rc::FlagValue *flag(const rcg::Session &s, const std::string &k) { return s.flags().get(k); }

std::string err(const json &extra) {
	try {
		level_of(extra);
	} catch (const rcg::LevelError &e) {
		return e.what();
	}
	return "no error";
}

} // namespace

TEST_CASE("objective: the goal wins, latched, until undo or reset") {
	rcg::Session s(level_of(json::parse(R"({"objective": {"goal": {"rect": [1160, 440, 140, 200]}}})")));
	CHECK_FALSE(s.won());
	CHECK(s.hud() == "Goal: open");
	s.teleport(rc::V2::of(1200, 540));
	CHECK_FALSE(s.won()); // (a teleport is no move)
	s.hold({}, 1);
	CHECK(s.won());
	CHECK(s.hud() == "Goal: reached!");
	CHECK(s.record({})["won"] == true);
	s.teleport(rc::V2::of(700, 540));
	s.hold({}, 1);
	CHECK(s.won()); // latched
	s.key(rcg::Action::Reset);
	CHECK_FALSE(s.won());
	CHECK(s.record({})["won"] == false);
}

TEST_CASE("objective: a goal's conditions lock it; a detector unlocks it") {
	rcg::Session s(level_of(json::parse(R"({
		"objective": {"goal": {"circle": {"center": [1200, 540], "radius": 70}}, "requires": [{"key": "det:plate", "values": ["true"]}]},
		"detectors": [{"name": "plate", "region": {"rect": [800, 640, 120, 120]}, "mode": "enter"}]})")));
	CHECK(s.hud() == "Goal: locked");
	s.teleport(rc::V2::of(1200, 540));
	s.hold({}, 1);
	CHECK_FALSE(s.won()); // inside, locked
	s.teleport(rc::V2::of(860, 700));
	s.hold({}, 1);
	REQUIRE(flag(s, "det:plate") != nullptr);
	CHECK(std::get<bool>(*flag(s, "det:plate")) == true);
	CHECK(s.hud() == "Goal: open");
	s.teleport(rc::V2::of(1200, 540));
	s.hold({}, 1);
	CHECK(s.won());
}

TEST_CASE("detectors: while, enter, toggle, and a segment crossed") {
	rcg::Session s(level_of(json::parse(R"({"detectors": [
		{"name": "w", "region": {"rect": [700, 500, 40, 80]}},
		{"name": "t", "region": {"rect": [800, 500, 40, 80]}, "mode": "toggle"},
		{"name": "line", "segment": {"a": [900, 300], "b": [900, 800]}, "mode": "toggle", "flag": "crossed"}]})")));
	// while: set inside, unset outside
	s.hold({"move_right"}, 15); // 660 -> 710
	REQUIRE(flag(s, "det:w") != nullptr);
	s.hold({"move_right"}, 15); // 760: out
	CHECK(flag(s, "det:w") == nullptr);
	// toggle: each entry
	s.hold({"move_right"}, 15); // 810: in
	CHECK(std::get<bool>(*flag(s, "det:t")) == true);
	s.hold({"move_right"}, 15); // 860: out (no change)
	CHECK(std::get<bool>(*flag(s, "det:t")) == true);
	s.hold({"move_left"}, 15); // 810: in again
	CHECK(std::get<bool>(*flag(s, "det:t")) == false);
	// a segment: crossed going right, crossed back going left
	CHECK(flag(s, "crossed") == nullptr);
	s.hold({"move_right"}, 40); // 810 -> 943: across x = 900
	CHECK(std::get<bool>(*flag(s, "crossed")) == true);
	s.hold({"move_left"}, 40);
	CHECK(std::get<bool>(*flag(s, "crossed")) == false);
}

TEST_CASE("objective: without a goal, every target hit wins") {
	rcg::Session s(level_of(json::parse(R"({"elements": [{"kind": "target", "name": "t", "start": [900, 400], "end": [900, 700]}]})")));
	CHECK(s.hud().empty()); // (no goal line)
	s.set_cursor(rc::V2::of(900, 540));
	s.settle();
	s.fire(-1, 1.0 / 60.0);
	CHECK(s.won());
	CHECK_FALSE(s.record({}).contains("won")); // (only a level with a goal records it)
}

TEST_CASE("objective: the demo levels play to their goals") {
	const fs::path root = RC_REPO_ROOT;
	{
		rcg::Session s(rcg::load_level((root / "data/levels/objectives/button_door.json").string()));
		s.hold({"move_right"}, 120);
		CHECK(s.player().x < 960 - 11); // the door holds
		s.set_cursor(rc::V2::of(810, 300));
		s.settle();
		s.fire(-1, 1.0 / 60.0);
		CHECK(std::get<bool>(*flag(s, "door:gate")) == true); // the button opened it
		s.hold({"move_right"}, 100);
		CHECK(s.won());
	}
	{
		rcg::Session s(rcg::load_level((root / "data/levels/objectives/plate_door.json").string()));
		s.hold({"move_right"}, 60); // 660 -> 860, the plate's column
		CHECK_FALSE(s.won());
		s.hold({"move_down"}, 45);
		s.hold({"move_up"}, 45);
		s.hold({"move_right"}, 120);
		CHECK(s.won());
	}
}

TEST_CASE("objective: the schema") {
	auto e = [](const char *j) { return err(json::parse(j)); };
	CHECK(e(R"({"objective": {"goal": {"rect": [0, 0, 0, 10]}}})").find("a positive width and height") != std::string::npos);
	CHECK(e(R"({"objective": {"goal": {"circle": {"center": [0, 0], "radius": 5}}, "when": 1}})").find("unknown key 'when'") != std::string::npos);
	CHECK(e(R"({"detectors": [{"name": "s", "segment": {"a": [0, 0], "b": [9, 0]}}]})").find("a segment is crossed") != std::string::npos);
	CHECK(e(R"({"detectors": [{"name": "a", "region": {"rect": [0, 0, 9, 9]}}, {"name": "a", "region": {"rect": [0, 0, 9, 9]}}]})")
					.find("'a' names another detector")
			!= std::string::npos);
	CHECK(e(R"({"detectors": [{"name": "x", "for": "ghosts", "region": {"rect": [0, 0, 9, 9]}}]})").find("player, object or any") != std::string::npos);
	CHECK(e(R"({"detectors": [{"name": "x"}]})").find("a region or a segment") != std::string::npos);
}
