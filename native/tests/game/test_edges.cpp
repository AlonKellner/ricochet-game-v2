// Stage 169 (GAME_SPEC 4.2, 9.10; docs/TDD_73): the level's edges configured. By default the four edge walls
// pass the arrow (it wraps through infinity there); `edges` makes them elements of a kind -- their faces inward
// -- or "wrap": the opposite edges a portal pair. The ∞ band lies only along the edges that pass.
#include "rcg/build.hpp"
#include "rcg/canon.hpp"
#include "rcg/level2.hpp"
#include "rcg/level.hpp"
#include "rcg/session.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <string>

namespace {

using json = nlohmann::json;

// no room; a wall at x = 50 and a button (opening "d") at x = 100, left of the player at (200, 540)
rcg::Level level_of(const json &edges, const std::string &button = R"([100, 480], "end": [100, 600])") {
	json lv = json::parse(R"({"schema": "rc-level/2", "player": {"start": [200, 540]}, "room": {"rect": [560, 240, 800, 600], "built": false},
		"elements": [{"kind": "button", "name": "b", "opens": "d", "start": )" + button + R"(},
			{"kind": "door", "name": "d", "start": [1300, 700], "end": [1300, 800]}]})");
	if (!edges.is_null()) {
		lv["edges"] = edges;
	}
	return rcg::parse_level_json(lv);
}

void shoot(rcg::Session &s, double x, double y) {
	s.set_cursor(rc::V2::of(x, y));
	s.settle();
	s.fire(-1, 1.0 / 60.0);
}

bool pressed(const json &edges, double x = 1000, double y = 540, bool wall = true, const std::string &button = R"([100, 480], "end": [100, 600])") {
	rcg::Level lv = level_of(edges, button);
	if (wall) { // (a wall behind the button: an arrow wrapping in from the left stops there)
		json j = json::parse(R"({"schema": "rc-level/2", "player": {"start": [200, 540]}, "room": {"rect": [560, 240, 800, 600], "built": false},
			"elements": [{"kind": "wall", "name": "w", "start": [50, 400], "end": [50, 700]},
				{"kind": "button", "name": "b", "opens": "d", "start": )" + button + R"(},
				{"kind": "door", "name": "d", "start": [1300, 700], "end": [1300, 800]}]})");
		if (!edges.is_null()) {
			j["edges"] = edges;
		}
		lv = rcg::parse_level_json(j);
	}
	rcg::Session s(lv);
	shoot(s, x, y);
	return s.flags().is_true("door:d");
}

bool infinity_hovered(const json &edges, double x, double y) {
	rcg::Session s(level_of(edges));
	s.set_cursor(rc::V2::of(x, y));
	s.settle();
	return s.record(json::object())["edge"]["hovered"] == true;
}

} // namespace

TEST_CASE("edges: by default the arrow passes the edges -- an arrow wrapping in from the left stops at the wall") {
	CHECK_FALSE(pressed(nullptr));
	CHECK_FALSE(pressed("pass"));
}

TEST_CASE("edges: a kind for all four -- mirrors turn the arrow back onto the button, walls stop it") {
	CHECK(pressed("mirror"));
	CHECK(pressed("two_sided_mirror"));
	CHECK(pressed(json{{"kind", "mirror"}, {"back", "pass"}})); // (with its params)
	CHECK_FALSE(pressed("wall"));
	CHECK_FALSE(pressed("arrow_block"));
	CHECK(pressed(json{{"right", "mirror"}})); // one edge; the others pass
	CHECK_FALSE(pressed(json{{"left", "mirror"}}));
}

TEST_CASE("edges: the faces are inward, drawn and plannable as their kind's") {
	const rcg::Level lv = level_of("mirror");
	const std::vector<rcg::BuiltSurface> ss = rcg::build_level(lv);
	int edges = 0;
	for (const rcg::BuiltSurface &b : ss) {
		if (b.label != std::optional<std::string>("Mirror")) {
			continue;
		}
		edges++;
		CHECK(b.seg.determine_side(rc::V2::of(960, 540)) == rc::Side::Left); // the reflective side faces the level
		REQUIRE(b.left.has_value());
		CHECK(b.left->effect.type == "reflection");
		CHECK(b.left->interactive);
	}
	CHECK(edges == 4);
}

TEST_CASE("edges: wrap -- opposite edges a portal pair: the arrow leaves the right edge and enters at the left, the same height") {
	// aimed up and right: at x = 1920 it is at y = 325; through infinity it would come back along its line (y = 552.5 at x = 100)
	const std::string high = R"([100, 290], "end": [100, 330])";
	CHECK(pressed("wrap", 1000, 440, false, high));
	CHECK_FALSE(pressed(nullptr, 1000, 440, false, high));
	CHECK(pressed(json{{"left", "wrap"}, {"right", "wrap"}}, 1000, 440, false, high));
	// the pair's surfaces: a portal at the right edge whose image is the left one
	const rcg::Level lv = level_of("wrap");
	int portals = 0;
	for (const rcg::BuiltSurface &b : rcg::build_level(lv)) {
		portals += b.portal_pair != 0 ? 1 : 0;
	}
	CHECK(portals == 4);
}

TEST_CASE("edges: the ∞ band lies only along the edges that pass") {
	CHECK(infinity_hovered(nullptr, 1915, 540));
	CHECK_FALSE(infinity_hovered("arrow_block", 1915, 540));
	CHECK_FALSE(infinity_hovered("mirror", 1915, 540)); // (the mirror is hovered)
	CHECK_FALSE(infinity_hovered("wrap", 1915, 540));
	CHECK(infinity_hovered(json{{"right", "arrow_block"}}, 960, 5)); // the top edge still passes
	CHECK_FALSE(infinity_hovered(json{{"right", "arrow_block"}}, 1915, 540));
}

TEST_CASE("edges: the errors") {
	auto err = [](const json &edges) {
		try {
			level_of(edges);
		} catch (const std::exception &e) {
			return std::string(e.what());
		}
		return std::string();
	};
	CHECK(err(json{{"middle", "wall"}}).find("unknown key 'middle'") != std::string::npos);
	CHECK(err(json{{"left", "wrap"}}).find("both wrap or neither") != std::string::npos);
	CHECK(err(json{{"kind", "mirror"}, {"start", {0, 0}}}).find("follow the bounds") != std::string::npos);
	CHECK(err("lava").find("unknown element kind 'lava'") != std::string::npos);
	CHECK(err(3).find("expected") != std::string::npos);
}

TEST_CASE("edges: migrate reads them back from the lowering") {
	for (const json &edges : {json("mirror"), json("wrap"), json{{"top", "mirror"}, {"right", "arrow_block"}}, json{{"left", "wrap"}, {"right", "wrap"}, {"top", "wall"}},
			 json{{"kind", "mirror"}, {"back", "pass"}}}) {
		CAPTURE(edges.dump());
		json lv = json::parse(R"({"schema": "rc-level/2", "room": {"rect": [560, 240, 800, 600], "built": false},
			"elements": [{"kind": "wall", "name": "w", "start": [50, 400], "end": [50, 700]}]})");
		lv["edges"] = edges;
		const json lowered = rcg::lower_level(lv).level;
		const json back = rcg::migrate_level(lowered);
		CHECK(rcg::canonical(rcg::lower_level(back).level) == rcg::canonical(lowered));
		if (!edges.contains("kind")) {
			CHECK(back["edges"] == edges);
		}
	}
}
