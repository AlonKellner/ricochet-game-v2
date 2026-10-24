// rc-level/2 (docs/LEVEL_FORMAT.md, docs/ELEMENTS.md): levels as elements.
// Every level lowers to its committed rc-level/1 lowering (bench/lowered, what
// the oracle's builder reads) and migrates back from it byte for byte; the
// builtin templates expand and build; the schema is strict, naming the element.
#include "rcg/build.hpp"
#include "rcg/canon.hpp"
#include "rcg/elements.hpp"
#include "rcg/level.hpp"
#include "rcg/level2.hpp"
#include "rcg/model.hpp"
#include "rcg/session.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

const fs::path ROOT = RC_REPO_ROOT;

std::string read_text(const fs::path &p) {
	std::ifstream in(p, std::ios::binary);
	REQUIRE_MESSAGE(in.good(), p.string());
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

std::string flat(const fs::path &rel) {
	std::string out;
	for (const char c : rel.generic_string()) {
		out += c == '/' ? std::string("__") : std::string(1, c);
	}
	return out;
}

std::vector<fs::path> levels() {
	std::vector<fs::path> out;
	for (const auto &e : fs::recursive_directory_iterator(ROOT / "data" / "levels")) {
		if (e.path().extension() == ".json") {
			out.push_back(e.path());
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

// A level of the given elements in a built room (the default one) on the default bounds.
// (a vector: brace-initializing a json from one json copies it instead of making an array)
json level_of(const std::vector<json> &elements, const json &extra = json::object()) {
	json lv = {{"schema", "rc-level/2"}, {"elements", json::array()}};
	for (const json &e : elements) {
		lv["elements"].push_back(e);
	}
	for (auto it = extra.begin(); it != extra.end(); ++it) {
		lv[it.key()] = it.value();
	}
	return lv;
}

json seg(json e, double x0 = 700, double y0 = 300, double x1 = 700, double y1 = 700) {
	e["start"] = {x0, y0};
	e["end"] = {x1, y1};
	return e;
}

// The lowered surfaces of a level, without the room's walls and the edge walls.
std::vector<json> element_surfaces(const json &lv) {
	std::vector<json> out;
	const json low = rcg::lower_level(lv).level;
	for (const json &s : low["surfaces"]) {
		const int id = s["ids"][0];
		const bool wall = id <= 4 && s.value("label", json()) == "Surface";
		const bool edge = s.value("label", json("x")).is_null();
		if (!wall && !edge) {
			out.push_back(s);
		}
	}
	return out;
}

} // namespace

TEST_CASE("rc-level/2: every level lowers to its committed lowering, and migrates back from it") {
	int n = 0;
	for (const fs::path &p : levels()) {
		const fs::path rel = fs::relative(p, ROOT / "data" / "levels").replace_extension("");
		CAPTURE(rel.generic_string());
		const std::string text = read_text(p);
		const json doc = json::parse(text);
		CHECK(doc["schema"] == "rc-level/2");
		const std::string lowered = read_text(ROOT / "bench" / "lowered" / (flat(rel) + ".json"));
		CHECK(rcg::canonical(rcg::lower_level(doc).level) == lowered);
		// every level: migrating its lowering gives elements that lower to the same surfaces
		const json migrated = rcg::migrate_level(json::parse(lowered));
		CHECK(rcg::canonical(rcg::lower_level(migrated).level) == lowered);
		// a level Stage 142 migrated: exactly itself (a level written in rc-level/2 has names and
		// parameters of its author's, which surfaces do not tell; the lowering leaves out the
		// goal, the detectors, the flags and the templates)
		if (rel.generic_string().rfind("objectives/", 0) != 0) {
			CHECK(rcg::canonical(migrated) == text);
		}
		// the level read from either form builds the same surfaces
		const json a = rcg::describe_level(rcg::build_level(rcg::parse_level(text)));
		const json b = rcg::describe_level(rcg::build_level(rcg::parse_level(lowered)));
		CHECK(rcg::canonical(a) == rcg::canonical(b));
		n++;
	}
	CHECK(n == 41);
}

TEST_CASE("rc-level/2: every builtin template expands to surfaces that build") {
	const auto &kinds = rcg::elements::builtin();
	CHECK(kinds.size() == 27);
	const json local = {{"start", {-20.0, 0.0}}, {"end", {20.0, 0.0}}, {"via", {0.0, 0.0}},
			{"left", {{"effect", {{"type", "reflection"}}}, {"interactive", true}}}, {"right", nullptr}};
	const json required = {{"portal", {{"d", {300.0, 0.0}}}}, {"switchable", {{"flag", "open"}}}, {"button", {{"flag", "open"}}},
			{"spawner", {{"surface", local}}}, {"resetter", {{"channels", {"spawner_1"}}}},
			{"conditional_button", {{"flag", "switch"}}}};
	std::vector<json> elements;
	double x = 300;
	for (const auto &[kind, tmpl] : kinds) {
		CAPTURE(kind);
		json e = {{"kind", kind}};
		if (required.contains(kind)) {
			for (auto it = required[kind].begin(); it != required[kind].end(); ++it) {
				e[it.key()] = it.value();
			}
		}
		if (tmpl.value("geometry", "any") == "circle") {
			e["circle"] = {{"center", {x, 500.0}}, {"radius", 20.0}};
		} else if (kind == "projection") {
			e = seg(e, x, 300, x, 340); // a line normal projection
		} else {
			e = seg(e, x, 300, x, 340);
		}
		elements.push_back(e);
		x += 60;
	}
	const rcg::Level lv = rcg::parse_level_json(level_of(elements));
	const std::vector<rcg::BuiltSurface> built = rcg::build_level(lv);
	CHECK(built.size() == 4 + 27 + 1 + 1 + 4); // (a toggle surface stands two surfaces) // the room, the elements (a portal's image), the edges
	// docs/ELEMENTS.md lists every builtin kind
	const std::string doc = read_text(ROOT / "docs" / "ELEMENTS.md");
	for (const auto &[kind, tmpl] : kinds) {
		CHECK_MESSAGE(doc.find("| `" + kind + "` |") != std::string::npos, kind);
	}
}

TEST_CASE("rc-level/2: the expansions") {
	SUBCASE("a mirror: its left reflects, its back passes or blocks; flip faces it right") {
		auto ss = element_surfaces(level_of({seg({{"kind", "mirror"}, {"name", "m"}})}));
		REQUIRE(ss.size() == 1);
		CHECK(ss[0]["label"] == "Mirror");
		CHECK(ss[0]["left"]["effect"]["type"] == "reflection");
		CHECK(ss[0]["left"]["interactive"] == true);
		CHECK(ss[0]["right"]["effect"]["type"] == "pass");
		CHECK(ss[0]["via"] == json::array({700.0, 500.0})); // via omitted: the middle point (a line)
		ss = element_surfaces(level_of({seg({{"kind", "mirror"}, {"back", "block"}, {"flip", true}})}));
		CHECK(ss[0]["left"]["effect"]["type"] == "terminal");
		CHECK(ss[0]["right"]["effect"]["type"] == "reflection");
	}
	SUBCASE("a door: present while its flag is unset or false; a button opens it by name") {
		auto ss = element_surfaces(level_of({seg({{"kind", "door"}, {"name", "d1"}}), seg({{"kind", "button"}, {"opens", "d1"}}, 900, 300, 900, 400)}));
		REQUIRE(ss.size() == 2);
		CHECK(ss[0]["presence"] == json::parse(R"([{"key": "door:d1", "values": ["DEFAULT", "false"]}])"));
		CHECK(ss[0]["player_solid"] == true);
		CHECK(ss[0]["left"]["effect"]["type"] == "terminal");
		CHECK(ss[1]["left"]["hits"] == json::parse(R"([{"key": "door:d1", "kind": "state", "op": "toggle", "value": true}])"));
		ss = element_surfaces(level_of({seg({{"kind", "button"}, {"flag", "lamp"}, {"op", "set"}, {"deferred", true}})}));
		CHECK(ss[0]["right"]["hits"][0]["deferred"] == true);
		CHECK(ss[0]["right"]["hits"][0]["op"] == "set");
	}
	SUBCASE("labels: the template's, the element's own, none (not drawn)") {
		const json inv = {{"kind", "inversion"}, {"sides", "left"}, {"circle", {{"center", {900.0, 500.0}}, {"radius", 50.0}}}};
		CHECK(element_surfaces(level_of({inv}))[0]["label"] == "Inversion");
		json named = inv;
		named["label"] = "Lens";
		CHECK(element_surfaces(level_of({named}))[0]["label"] == "Lens");
		named["label"] = nullptr;
		const json low = rcg::lower_level(level_of({named})).level;
		CHECK(low["surfaces"][4]["label"].is_null());
	}
	SUBCASE("ids: the room's walls, the elements (a portal two), the edge walls, the pre-transform circles last") {
		const json pre = {{"kind", "pre_inversion"}, {"circle", {{"center", {960.0, 540.0}}, {"radius", 300.0}}}};
		const json portal = seg({{"kind", "portal"}, {"d", {200.0, 0.0}}});
		const json low = rcg::lower_level(level_of({pre, portal, seg({{"kind", "wall"}}, 900, 300, 900, 400)})).level;
		const json &ss = low["surfaces"];
		REQUIRE(ss.size() == 4 + 1 + 1 + 4 + 1);
		CHECK(ss[4]["ids"] == json::array({5, 6})); // the portal and its image
		CHECK(ss[5]["ids"] == json::array({7}));
		CHECK(ss[6]["label"].is_null());            // the edge walls
		CHECK(ss[10]["pre_transform"] == true);
		CHECK(ss[10]["ids"] == json::array({12}));
		// an edges element places the edge walls where it stands
		const json low2 = rcg::lower_level(level_of({json{{"kind", "edges"}}, seg({{"kind", "wall"}}, 900, 300, 900, 400)})).level;
		CHECK(low2["surfaces"][4]["label"].is_null());
		CHECK(low2["surfaces"][8]["label"] == "Surface");
		// no room
		const json low3 = rcg::lower_level(level_of({}, {{"room", {{"rect", {0.0, 0.0, 10.0, 10.0}}, {"built", false}}}})).level;
		CHECK(low3["surfaces"].size() == 4);
	}
	SUBCASE("a level's own template: a new kind, and an override of a builtin") {
		const json glass = json::parse(R"({"label": "Glass", "params": {"tint": {"default": "clear", "enum": ["clear", "dark"]}},
			"surfaces": [{"left": {"$switch": "tint", "cases": {"clear": {"effect": {"type": "pass"}, "interactive": false},
			                                                     "dark": {"effect": {"type": "terminal"}, "interactive": false}}},
			              "player_solid": true, "right": {"effect": {"type": "pass"}, "interactive": false}}]})");
		const json wall = json::parse(R"({"label": "Brick", "surfaces": [{"left": {"effect": {"type": "terminal"}, "interactive": false},
			"player_solid": true, "right": {"effect": {"type": "terminal"}, "interactive": false}}]})");
		const json lv = level_of({seg({{"kind", "glass"}, {"tint", "dark"}}), seg({{"kind", "wall"}}, 900, 300, 900, 400)},
				{{"templates", {{"glass", glass}, {"wall", wall}}}});
		auto ss = element_surfaces(lv);
		REQUIRE(ss.size() == 2);
		CHECK(ss[0]["label"] == "Glass");
		CHECK(ss[0]["left"]["effect"]["type"] == "terminal");
		CHECK(ss[1]["label"] == "Brick");
		rcg::build_level(rcg::parse_level_json(lv));
	}
	SUBCASE("migration names what it recognises and keeps the rest raw") {
		json v1 = rcg::lower_level(level_of({seg({{"kind", "mirror"}, {"name", "m"}}), seg({{"kind", "wall"}}, 900, 300, 900, 400)})).level;
		json odd = v1["surfaces"][5];
		odd["left"]["interactive"] = true; // a wall whose side is plannable: no template
		v1["surfaces"][5] = odd;
		const json v2 = rcg::migrate_level(v1);
		REQUIRE(v2["elements"].size() == 2);
		CHECK(v2["elements"][0]["kind"] == "mirror");
		CHECK(v2["elements"][0]["name"] == "mirror_1");
		CHECK(v2["elements"][1]["kind"] == "raw");
		CHECK(rcg::canonical(rcg::lower_level(v2).level) == rcg::canonical(v1));
		json gap = v1;
		gap["surfaces"][5]["ids"] = json::array({9});
		CHECK_THROWS_WITH_AS(rcg::migrate_level(gap), doctest::Contains("not consecutive"), rcg::LevelError);
	}
}

TEST_CASE("rc-level/2: the schema is strict, and names the element") {
	auto err = [](const json &lv) {
		try {
			rcg::build_level(rcg::parse_level_json(lv));
		} catch (const rcg::LevelError &e) {
			return std::string(e.what());
		}
		return std::string("no error");
	};
	CHECK(err(level_of({}, {{"colour", 1}})).find("unknown key 'colour'") != std::string::npos);
	CHECK(err(level_of({}, {{"carriers", nullptr}})).find("an array of carriers expected") != std::string::npos);
	CHECK(err(level_of({seg({{"kind", "crystal"}})})).find("level.elements[0].kind: unknown element kind 'crystal'") != std::string::npos);
	CHECK(err(level_of({seg({{"kind", "mirror"}, {"shiny", true}})})).find("unknown key 'shiny' (parameters: back, solid)") != std::string::npos);
	CHECK(err(level_of({seg({{"kind", "mirror"}, {"back", "maybe"}})})).find("level.elements[0] (mirror_1).back: one of pass, block expected")
			!= std::string::npos);
	CHECK(err(level_of({seg({{"kind", "portal"}})})).find("'d' required") != std::string::npos);
	CHECK(err(level_of({seg({{"kind", "button"}})})).find("'flag' required") != std::string::npos);
	CHECK(err(level_of({seg({{"kind", "wall"}, {"name", "a"}}), seg({{"kind", "wall"}, {"name", "a"}})})).find("'a' names another element")
			!= std::string::npos);
	CHECK(err(level_of({json{{"kind", "wall"}}})).find("a geometry expected") != std::string::npos);
	CHECK(err(level_of({json{{"kind", "edges"}}, json{{"kind", "edges"}}})).find("placed once") != std::string::npos);
	// a raw surface's errors name the element
	const json raw = {{"kind", "raw"}, {"name", "r"},
			{"surface", {{"start", {0.0, 0.0}}, {"end", {9.0, 0.0}}, {"via", {4.5, 0.0}}, {"left", {{"effect", {{"type", "glow"}}}, {"oops", 1}}}}}};
	CHECK(err(level_of({raw})).find("level.elements[0] (r).surface.left: unknown key 'oops'") != std::string::npos);
	// a template's own mistakes
	const json bad = json::parse(R"({"surfaces": [{"left": "$nope", "right": null}]})");
	CHECK(err(level_of({}, {{"templates", {{"thing", bad}}}})).find("undeclared parameter 'nope'") != std::string::npos);
	const json geo = json::parse(R"({"surfaces": [{"start": [0, 0], "left": null, "right": null}]})");
	CHECK(err(level_of({}, {{"templates", {{"thing", geo}}}})).find("the geometry is the element's") != std::string::npos);
}

TEST_CASE("rc-level/2: the initial flags start the session, and a reset restores them") {
	const json lv = level_of({seg({{"kind", "door"}, {"name", "d1"}})}, {{"flags", {{"door:d1", true}, {"count", 2}}}, {"player", {{"start", {600.0, 500.0}}}}});
	rcg::Session s(rcg::parse_level_json(lv));
	const rc::FlagValue *open = s.flags().get("door:d1");
	REQUIRE(open != nullptr);
	CHECK(std::get<bool>(*open) == true);
	CHECK(std::get<double>(*s.flags().get("count")) == 2.0);
	const rcg::BuiltSurface *door = s.world().by_id(5);
	REQUIRE(door != nullptr);
	CHECK_FALSE(rcg::present(*door, s.flags())); // open: gone
	s.key(rcg::Action::Reset);
	CHECK(std::get<bool>(*s.flags().get("door:d1")) == true);
}

TEST_CASE("rc-level/2: carriers -- elements on a line or a circle, projected onto it (Stage 149)") {
	const json lv = json::parse(R"({"schema": "rc-level/2", "carriers": [
		{"name": "floor", "line": {"a": [600, 500], "b": [1200, 503]}},
		{"name": "ring", "circle": {"center": [900, 400], "radius": 100}}],
		"elements": [
			{"kind": "wall", "name": "w1", "carrier": "floor", "from": [600, 480], "to": [800, 520]},
			{"kind": "door", "name": "d", "carrier": "floor", "from": [800, 520], "to": [900, 490]},
			{"kind": "mirror", "name": "arc1", "carrier": "ring", "from": [1000, 400], "to": [900, 300]},
			{"kind": "two_sided_mirror", "name": "arc2", "carrier": "ring", "from": [900, 300], "to": [800, 400], "via": [820, 330]},
			{"kind": "inversion", "name": "whole", "carrier": "ring"}]})");
	const json low = rcg::lower_level(lv).level;
	std::map<std::string, json> by_label_id;
	const json &ss = low["surfaces"];
	// the room's walls first (ids 1-4), then the elements
	const json &w1 = ss[4], &door = ss[5], &arc1 = ss[6], &arc2 = ss[7], &whole = ss[8];
	auto on_line = [](const json &p) { // (600, 500) + t (600, 3)
		const double t = (p[0].get<double>() - 600) / 600;
		return std::abs(p[1].get<double>() - (500 + 3 * t)) < 1e-9;
	};
	CHECK(on_line(w1["start"]));
	CHECK(on_line(w1["end"]));
	CHECK(w1["straight"] == true);
	CHECK(rcg::canonical(w1["end"]) == rcg::canonical(door["start"])); // one point, by provenance: the same value
	auto on_ring = [](const json &p) { return std::abs(std::hypot(p[0].get<double>() - 900, p[1].get<double>() - 400) - 100) < 1e-9; };
	CHECK(on_ring(arc1["start"]));
	CHECK(on_ring(arc1["via"]));
	// the default via: the middle of the shorter arc from from to to (here a quarter, at -45 degrees)
	CHECK(arc1["via"][0].get<double>() == doctest::Approx(900 + 100 * std::sqrt(0.5)).epsilon(1e-9));
	CHECK(arc1["via"][1].get<double>() == doctest::Approx(400 - 100 * std::sqrt(0.5)).epsilon(1e-9));
	CHECK(rcg::canonical(arc1["end"]) == rcg::canonical(arc2["start"]));
	CHECK(on_ring(arc2["via"]));
	CHECK(whole["circle"] == json::parse(R"({"center": [900, 400], "radius": 100})"));
	CHECK_NOTHROW(rcg::build_level(rcg::parse_level_json(lv)));
	// the errors
	auto err = [](const char *j) {
		try {
			rcg::lower_level(json::parse(j));
		} catch (const rcg::LevelError &e) {
			return std::string(e.what());
		}
		return std::string("no error");
	};
	CHECK(err(R"({"schema": "rc-level/2", "elements": [{"kind": "wall", "carrier": "x", "from": [0, 0], "to": [9, 0]}]})").find("no carrier named 'x'")
			!= std::string::npos);
	CHECK(err(R"({"schema": "rc-level/2", "carriers": [{"name": "l", "line": {"a": [0, 0], "b": [9, 0]}}],
		"elements": [{"kind": "wall", "carrier": "l", "start": [0, 0], "end": [9, 0]}]})").find("an element on a carrier takes from and to") != std::string::npos);
	CHECK(err(R"({"schema": "rc-level/2", "carriers": [{"name": "l", "line": {"a": [0, 0], "b": [9, 0]}}],
		"elements": [{"kind": "wall", "carrier": "l", "from": [3, 5], "to": [3, -5]}]})").find("land on one point") != std::string::npos);
	CHECK(err(R"({"schema": "rc-level/2", "carriers": [{"name": "l", "line": {"a": [0, 0], "b": [0, 0]}}], "elements": []})").find("two distinct points")
			!= std::string::npos);
	CHECK(err(R"({"schema": "rc-level/2", "carriers": [{"name": "c", "circle": {"center": [0, 0], "radius": 5}},
		{"name": "c", "circle": {"center": [0, 0], "radius": 6}}], "elements": []})").find("names another carrier") != std::string::npos);
}
