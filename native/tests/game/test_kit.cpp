// Stage 147 (docs/TDD_52, docs/ELEMENTS.md "The kit"): the novel elements as
// templates alone -- breakable, toggle, frost, fire, charge -- on one primitive,
// the per-shot flags (shot:*, erased when the arrow lands).
#include "rcg/level.hpp"
#include "rcg/model.hpp"
#include "rcg/session.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;

rcg::Level level_of(const std::string &elements) {
	json lv = json::parse(R"({"schema": "rc-level/2", "player": {"start": [660, 540]}, "room": {"rect": [560, 240, 800, 600], "built": true}})");
	lv["elements"] = json::parse(elements);
	return rcg::parse_level_json(lv);
}

// a shot from the player at a point
void shoot(rcg::Session &s, double x, double y) {
	s.set_cursor(rc::V2::of(x, y));
	s.settle();
	s.fire(-1, 1.0 / 60.0);
}

// the fired hits' x (vertical surfaces) of the shot the session would fire now
std::vector<float> fired_xs(rcg::Session &s, double x, double y) {
	s.set_cursor(rc::V2::of(x, y));
	s.settle();
	std::vector<float> out;
	const rc::TracePath *p = s.physical_trace();
	REQUIRE(p != nullptr);
	for (const rc::HitRecord &h : p->hits) {
		if (h.fired) {
			out.push_back(h.point.x);
		}
	}
	return out;
}

bool has(const rcg::Session &s, const std::string &k) { return s.flags().get(k) != nullptr; }
bool is_true(const rcg::Session &s, const std::string &k) { return s.flags().is_true(k); }

// the surface built for an element (by its label and position), present now?
bool present_at(const rcg::Session &s, float x) {
	for (const rcg::BuiltSurface &b : s.world().surfaces()) {
		if (b.label && *b.label != "Surface" && b.seg.s.x == x && b.seg.e.x == x) {
			return rcg::present(b, s.flags());
		}
	}
	FAIL("no surface at x = " << x);
	return false;
}


} // namespace

TEST_CASE("kit: the per-shot flags are gone when the arrow lands") {
	rcg::Session s(level_of(R"([{"kind": "torch", "start": [800, 300], "end": [800, 780]}])"));
	shoot(s, 1000, 540);
	CHECK_FALSE(has(s, "shot:fire"));
}

TEST_CASE("kit: a breakable mirror reflects once, then is gone for the shot, back after it") {
	// the arrow reflects off it, off a mirror behind the player, then passes where it was
	rcg::Session s(level_of(R"([{"kind": "breakable", "name": "glass", "face": "mirror", "start": [900, 300], "end": [900, 780]},
		{"kind": "two_sided_mirror", "start": [620, 300], "end": [620, 780]}])"));
	const std::vector<float> xs = fired_xs(s, 900, 540);
	REQUIRE(xs.size() >= 3);
	CHECK(xs[0] == 900); // the breakable mirror
	CHECK(xs[1] == 620); // the mirror behind
	CHECK(xs[2] == 1360); // the room's right wall: the glass is gone
	s.fire(-1, 1.0 / 60.0);
	CHECK(present_at(s, 900)); // back after the shot
}

TEST_CASE("kit: frost freezes a wall into a mirror for one hit") {
	rcg::Session s(level_of(R"([{"kind": "frost_barrier", "start": [800, 300], "end": [800, 780]},
		{"kind": "freezable", "start": [1000, 300], "end": [1000, 780]}])"));
	const std::vector<float> xs = fired_xs(s, 1000, 540);
	REQUIRE(xs.size() >= 3);
	CHECK(xs[0] == 800);  // frosted
	CHECK(xs[1] == 1000); // the frozen wall reflects
	CHECK(xs[2] == 800);  // back through the barrier
	// unfrosted, the wall is a wall
	rcg::Session t(level_of(R"([{"kind": "freezable", "start": [1000, 300], "end": [1000, 780]}])"));
	const std::vector<float> plain = fired_xs(t, 1000, 540);
	REQUIRE(plain.size() == 1);
	CHECK(plain[0] == 1000); // it stops the arrow
}

TEST_CASE("kit: fire burns a burnable barrier for good; water puts it out first") {
	rcg::Session s(level_of(R"([{"kind": "torch", "start": [800, 300], "end": [800, 780]},
		{"kind": "burnable", "name": "crate", "start": [1000, 300], "end": [1000, 780]}])"));
	shoot(s, 1000, 540);
	CHECK(is_true(s, "el:crate:fire")); // (Stage 156: on fire, burnt away)
	CHECK_FALSE(present_at(s, 1000)); // gone for good
	rcg::Session t(level_of(R"([{"kind": "torch", "start": [800, 300], "end": [800, 780]},
		{"kind": "water", "start": [900, 300], "end": [900, 780]},
		{"kind": "burnable", "name": "crate", "start": [1000, 300], "end": [1000, 780]}])"));
	shoot(t, 1000, 540);
	CHECK_FALSE(has(t, "el:crate:fire"));
	CHECK(present_at(t, 1000));
}

TEST_CASE("kit: a toggle surface goes when hit and comes back when passed") {
	rcg::Session s(level_of(R"([{"kind": "toggle_surface", "name": "t", "face": "mirror", "start": [900, 300], "end": [900, 780]}])"));
	shoot(s, 900, 540); // the hit: gone
	CHECK(is_true(s, "el:t:off"));
	shoot(s, 900, 540); // the arrow passes where it was: back
	REQUIRE(has(s, "el:t:off"));
	CHECK_FALSE(is_true(s, "el:t:off"));
}

TEST_CASE("kit: a conditional button presses only for a charged arrow") {
	const std::string elements = R"([{"kind": "conditional_button", "flag": "door:gate", "start": [1000, 300], "end": [1000, 780]},
		{"kind": "door", "name": "gate", "start": [1200, 300], "end": [1200, 780]}%s])";
	char buf[1024];
	std::snprintf(buf, sizeof buf, elements.c_str(), "");
	rcg::Session plain(level_of(buf));
	shoot(plain, 1000, 540);
	CHECK_FALSE(has(plain, "door:gate"));
	std::snprintf(buf, sizeof buf, elements.c_str(), R"(, {"kind": "charger", "start": [800, 300], "end": [800, 780]})");
	rcg::Session charged(level_of(buf));
	shoot(charged, 1000, 540);
	CHECK(is_true(charged, "door:gate"));
}
