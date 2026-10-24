// Stage 152 (GAME_SPEC 24.7, docs/TDD_57): spreading. Fire flows through the
// flammable elements that touch, for good; charge flows from the powered ones
// through what conducts, recomputed as things open, close and move.
#include "rcg/level.hpp"
#include "rcg/model.hpp"
#include "rcg/session.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

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

const char *CIRCUIT = R"([
	{"kind": "battery", "name": "cell", "start": [600, 300], "end": [600, 400]},
	{"kind": "door", "name": "breaker", "tags": ["conductive"], "closed": "fence", "start": [600, 400], "end": [700, 400]},
	{"kind": "conductor", "name": "w1", "start": [700, 400], "end": [800, 400]},
	{"kind": "conductor", "name": "w2", "start": [840, 400], "end": [1000, 400]},
	{"kind": "charged_door", "name": "gate", "start": [1000, 240], "end": [1000, 840]},
	{"kind": "button", "opens": "breaker", "op": "set", "start": [800, 600], "end": [800, 700]}])";

} // namespace

TEST_CASE("spread: fire burns through the flammable elements that touch, for good") {
	rcg::Session s(level_of(R"({"elements": [
		{"kind": "torch", "start": [800, 300], "end": [800, 780]},
		{"kind": "burnable", "name": "a", "start": [1000, 300], "end": [1000, 700]},
		{"kind": "burnable", "name": "b", "start": [1000, 700], "end": [1200, 700]},
		{"kind": "burnable", "name": "c", "start": [1300, 300], "end": [1300, 400]}]})"));
	shoot(s, 1000, 540); // a fire arrow burns a, and the fire spreads to b, which touches it
	CHECK(s.flags().is_true("el:a:burnt"));
	CHECK(s.flags().is_true("el:b:burnt"));
	CHECK_FALSE(s.flags().has("el:c:burnt"));
}

TEST_CASE("spread: charge flows from a battery through conductors; a gap, a breaker, an object") {
	{
		rcg::Session s(level_of(std::string(R"({"elements": )") + CIRCUIT + "}"));
		CHECK(s.flags().is_true("el:w1:charge")); // through the closed breaker
		CHECK_FALSE(s.flags().has("el:w2:charge")); // the gap
		CHECK(rcg::present(*s.world().by_id(9), s.flags())); // the charged door: closed
	}
	{
		// a conductive object in the gap closes the circuit: the door opens; the breaker opened breaks it again
		rcg::Session s(level_of(std::string(R"({"objects": [{"name": "box", "kind": "static", "circle": {"center": [820, 400], "radius": 21},
			"tags": ["conductive"]}], "elements": )") +
				CIRCUIT + "}"));
		CHECK(s.flags().is_true("el:box:charge"));
		CHECK(s.flags().is_true("el:gate:charge"));
		CHECK_FALSE(rcg::present(*s.world().by_id(9), s.flags())); // open
		shoot(s, 800, 650); // the button: the breaker opens (gone), the circuit breaks
		CHECK(s.flags().is_true("door:breaker"));
		CHECK_FALSE(s.flags().has("el:gate:charge"));
		CHECK(rcg::present(*s.world().by_id(9), s.flags())); // closed again
		s.key(rcg::Action::Undo);
		CHECK(s.flags().is_true("el:gate:charge")); // undone with the checkpoint's flags
	}
}

TEST_CASE("spread: the demo level -- a popped bubble drops a box into the gap, the door opens") {
	const std::filesystem::path root = RC_REPO_ROOT;
	rcg::Session s(rcg::load_level((root / "data/levels/objectives/circuit.json").string()));
	s.hold({}, 60);
	CHECK_FALSE(s.flags().has("el:gate:charge"));
	shoot(s, 898, 262);
	s.hold({}, 60); // the box falls onto the wires' ends
	CHECK(s.flags().is_true("el:box:charge"));
	CHECK(s.flags().is_true("el:gate:charge"));
	s.hold({"move_right"}, 180);
	CHECK(s.won());
}

TEST_CASE("spread: the level's rules") {
	auto err = [](const char *extra) {
		try {
			level_of(extra);
		} catch (const rcg::LevelError &e) {
			return std::string(e.what());
		}
		return std::string("no error");
	};
	CHECK(err(R"({"spread": [{"flag": "glow", "tag": "lit", "speed": 2}]})").find("unknown key 'speed'") != std::string::npos);
	const rcg::Level lv = level_of(R"({"spread": [{"flag": "glow", "tag": "lit", "persist": false, "sources": ["lamp"]}],
		"elements": [{"kind": "passthrough", "name": "l", "tags": ["lit", "lamp"], "start": [700, 300], "end": [700, 400]},
		             {"kind": "passthrough", "name": "m", "tags": ["lit"], "start": [700, 400], "end": [800, 400]}]})");
	rcg::Session s(lv);
	CHECK(s.flags().is_true("el:m:glow"));
	CHECK_FALSE(s.flags().has("el:m:charge")); // (the level's rules replace the defaults)
}
