// Stage 156 (GAME_SPEC 10.6, docs/TDD_61): the arrow's states in the game. Tags
// give elements and objects their part: flammable (fire passes between the
// arrow and it), conductive (they swap charge), frosted (the arrow takes
// frost); burning and charged start them so. Water puts the arrow's fire out;
// a frosted arrow that would be stopped is reflected or inverted, once.
#include "rcg/level.hpp"
#include "rcg/session.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

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

// the x of every fired hit of the current preview's physical walk
std::vector<float> hits_x(rcg::Session &s, double x, double y) {
	s.set_cursor(rc::V2::of(x, y));
	s.settle();
	std::vector<float> out;
	for (const rc::HitRecord &h : s.physical_trace()->hits) {
		if (h.fired) {
			out.push_back(h.point.x);
		}
	}
	return out;
}

} // namespace

TEST_CASE("arrow states: fire -- a burning surface lights the arrow, a burning arrow lights what is flammable; water puts it out") {
	rcg::Session s(level_of(R"({"elements": [
		{"kind": "torch", "name": "t", "start": [800, 300], "end": [800, 780]},
		{"kind": "wall", "name": "w", "tags": ["flammable"], "start": [1000, 300], "end": [1000, 780]}]})"));
	CHECK(s.flags().is_true("el:t:fire")); // burning from the start
	shoot(s, 1000, 540);
	CHECK(s.flags().is_true("el:w:fire"));
	CHECK_FALSE(s.flags().has("shot:fire")); // the arrow's fire ends with the shot
	// an unlit arrow touching a burning surface catches fire, and lights the next flammable one
	rcg::Session u(level_of(R"({"flags": {"el:a:fire": true}, "elements": [
		{"kind": "passthrough", "name": "a", "tags": ["flammable"], "start": [800, 300], "end": [800, 780]},
		{"kind": "passthrough", "name": "b", "tags": ["flammable"], "start": [900, 300], "end": [900, 780]}]})"));
	shoot(u, 1000, 540);
	CHECK(u.flags().is_true("el:b:fire"));
	// water between them puts the arrow out
	rcg::Session v(level_of(R"({"elements": [
		{"kind": "torch", "start": [800, 300], "end": [800, 780]},
		{"kind": "water", "start": [900, 300], "end": [900, 780]},
		{"kind": "wall", "name": "w", "tags": ["flammable"], "start": [1000, 300], "end": [1000, 780]}]})"));
	shoot(v, 1000, 540);
	CHECK_FALSE(v.flags().has("el:w:fire"));
}

TEST_CASE("arrow states: charge -- the arrow and a conductor swap it; a battery starts charged") {
	rcg::Session s(level_of(R"({"elements": [
		{"kind": "charger", "start": [750, 300], "end": [750, 780]},
		{"kind": "conductor", "name": "c", "start": [900, 300], "end": [900, 780]},
		{"kind": "battery", "name": "cell", "start": [1100, 300], "end": [1100, 780]}]})"));
	CHECK(s.flags().is_true("el:cell:charge"));
	shoot(s, 1100, 540); // charged by the charger, it gives the charge to c; uncharged, it takes the battery's
	CHECK(s.flags().is_true("el:c:charge"));
	CHECK(s.flags().get("el:cell:charge") != nullptr);
	CHECK_FALSE(s.flags().is_true("el:cell:charge"));
	// a conductive object that stops the arrow takes its charge (one passed through is touched twice: given, then taken back)
	rcg::Session o(level_of(R"({"objects": [{"name": "box", "kind": "static", "face": "wall", "circle": {"center": [1000, 540], "radius": 20},
		"tags": ["conductive"]}], "elements": [{"kind": "charger", "start": [750, 300], "end": [750, 780]}]})"));
	shoot(o, 1200, 540);
	CHECK(o.flags().is_true("el:box:charge"));
}

TEST_CASE("arrow states: frost -- a frosted arrow reflects off what would stop it, once; mirrors keep it; a shadow's edge still stops it") {
	{
		rcg::Session s(level_of(R"({"elements": [
			{"kind": "frost_barrier", "start": [800, 300], "end": [800, 780]},
			{"kind": "wall", "start": [1000, 300], "end": [1000, 780]}]})"));
		const std::vector<float> xs = hits_x(s, 1000, 540);
		REQUIRE(xs.size() >= 3);
		CHECK(xs[1] == 1000); // reflected off the wall
		CHECK(xs[2] == 800);  // back through the barrier: frosted again
	}
	{
		// a frosted circle object: the arrow is frosted by it and keeps going (a pass), then the wall reflects it
		rcg::Session s(level_of(R"({"objects": [{"name": "ice", "kind": "static", "face": "pass", "circle": {"center": [900, 540], "radius": 20},
			"tags": ["frosted"]}], "elements": [{"kind": "wall", "start": [1100, 300], "end": [1100, 780]}]})"));
		const std::vector<float> xs = hits_x(s, 1100, 540);
		REQUIRE(xs.size() >= 3);
		CHECK(xs[2] == 1100);
	}
	{
		// a shadow's edge is no wall frost can turn: the frosted arrow stops at it
		rcg::Session s(level_of(R"({"elements": [
			{"kind": "frost_barrier", "start": [800, 300], "end": [800, 780]},
			{"kind": "dark", "name": "night", "circle": {"center": [1150, 540], "radius": 120}}]})"));
		const std::vector<float> xs = hits_x(s, 1150, 540);
		REQUIRE(xs.size() == 2);
		CHECK(xs[1] == doctest::Approx(1030).epsilon(1e-4));
	}
}

TEST_CASE("arrow states: the tags' errors") {
	auto err = [](const char *extra) {
		try {
			level_of(extra);
		} catch (const rcg::LevelError &e) {
			return std::string(e.what());
		}
		return std::string("no error");
	};
	CHECK(err(R"({"elements": [{"kind": "wall", "tags": ["frosted"], "start": [800, 300], "end": [800, 700]}]})").find("cannot bear frost") != std::string::npos);
	CHECK(err(R"({"objects": [{"name": "o", "circle": {"center": [800, 500], "radius": 20}, "tags": ["frosted"]}]})").find("cannot bear frost")
			!= std::string::npos);
	CHECK(err(R"({"elements": [{"kind": "raw", "surface": {"start": [800, 300], "end": [800, 700], "via": [800, 500], "left": {"effect": {"type": "pass"},
		"hits": [{"kind": "target", "when": [{"key": "a", "values": ["true"]}]}]}}}]})").find("only a state write") != std::string::npos);
}
