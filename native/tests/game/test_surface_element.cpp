// Stage 207: the surface element (rcg/surface_element.hpp, GAME_SPEC 9.13).
// The 15 kinds it replaced (native/tests/game/retired_templates, their
// templates as Stage 206 left them) expand exactly as their presets do over
// every value of their parameters; a surface's parameters read back from it;
// the features combine (a blocking button, a door that is a button); migrate
// picks the preset of the run's label.
#include "rcg/build.hpp"
#include "rcg/canon.hpp"
#include "rcg/elements.hpp"
#include "rcg/level.hpp"
#include "rcg/level2.hpp"
#include "rcg/surface_element.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;
namespace se = rcg::surface_element;

const fs::path ROOT = RC_REPO_ROOT;

json read_json(const fs::path &p) {
	std::ifstream in(p, std::ios::binary);
	REQUIRE_MESSAGE(in.good(), p.string());
	std::ostringstream ss;
	ss << in.rdbuf();
	return json::parse(ss.str());
}

const json SEG = {{"start", {700.0, 300.0}}, {"end", {700.0, 700.0}}};

// A level of one element (push_back: brace-initializing an array from one json copies it).
json level_of(const json &element) {
	json lv = {{"schema", "rc-level/2"}, {"elements", json::array()}};
	lv["elements"].push_back(element);
	return lv;
}

std::vector<json> expand(const json &tmpl, json element) {
	element.update(SEG);
	return rcg::elements::expand(tmpl, element, "e", "test");
}

// An old parameter: its values, and its new spelling (name, value; a null name: dropped).
struct Old {
	std::string name;
	std::vector<json> values;
	std::function<std::pair<std::string, json>(const json &)> spell;
};

std::pair<std::string, json> same(const std::string &n, const json &v) { return {n, v}; }

const std::map<std::string, std::string> FACE = {{"terminal", "stop"}, {"reflection", "mirror"}, {"wall", "stop"}, {"pass", "pass"}, {"mirror", "mirror"}};

Old solid() {
	return {"solid", {true, false}, [](const json &v) { return same("body", v.get<bool>() ? "block" : "pass"); }};
}
Old face(std::vector<json> values) {
	return {"face", std::move(values), [](const json &v) { return same("face", FACE.at(v.get<std::string>())); }};
}
Old keep(const std::string &n, std::vector<json> values) {
	return {n, std::move(values), [n](const json &v) { return same(n, v); }};
}

// The retired kinds' parameters (each value, or a sample for strings) and their new spelling.
std::map<std::string, std::vector<Old>> retired() {
	const json local = {{"start", {-20.0, 0.0}}, {"end", {20.0, 0.0}}, {"via", {0.0, 0.0}},
			{"left", {{"effect", {{"type", "reflection"}}}, {"interactive", true}}}, {"right", nullptr}};
	return {
			{"wall", {}}, {"shield", {}}, {"fence", {}}, {"passthrough", {}}, {"platform", {}}, {"ghost_gate", {}},
			{"mirror", {solid()}},
			{"one_sided_mirror", {{"back", {"pass", "block"}, [](const json &v) { return same("back", v == "block" ? "stop" : "pass"); }}, solid()}},
			{"button", {face({"pass", "terminal", "reflection"}), keep("flag", {"lamp"}), keep("op", {"toggle", "set"}), keep("value", {true, 2.0}),
							   keep("deferred", {false, true})}},
			{"target", {face({"pass", "terminal", "reflection"}), {"interactive", {true, false}, [](const json &v) { return same("plannable", v); }}, solid()}},
			{"door", {{"closed", {"wall", "mirror", "fence"}, [](const json &v) { return same("face", v == "fence" ? "pass" : FACE.at(v.get<std::string>())); }},
							 {"flag", {"door:e", "det:plate", "!a && b"}, [](const json &v) { return same("open", v); }}, solid()}},
			{"grapple", {solid()}},
			{"breakable", {face({"wall", "mirror", "pass"}), solid()}},
			{"spawner", {{"surface", {local}, [](const json &v) { return same("spawn", v); }}, face({"pass", "terminal", "reflection"}), keep("deferred", {false, true}),
								keep("orientation", {"BEAM_TANGENT", "BEAM_NORMAL", "CARRIER_TANGENT", "CARRIER_NORMAL", "BEAM_REFLECTED", "WORLD"}),
								keep("mirror_with_side", {false, true}), keep("trim", {true, false}), keep("channel", {"e", "other"}), keep("offset", {json::array({0.0, 0.0}), json::array({3.0, -4.0})})}},
			{"resetter", {keep("channels", {json::array({"a", "b"})}), face({"pass", "terminal", "reflection"}), keep("deferred", {false, true})}},
	};
}

} // namespace

TEST_CASE("surface element: every retired kind expands as its preset does, over every value of its parameters (Stage 207)") {
	const auto kinds = retired();
	CHECK(kinds.size() == 15);
	size_t n = 0;
	for (const auto &[kind, olds] : kinds) {
		CAPTURE(kind);
		const json old_t = rcg::elements::prepare(read_json(ROOT / "native" / "tests" / "game" / "retired_templates" / (kind + ".json")), kind);
		const json &new_t = rcg::elements::builtin().at(kind);
		CHECK(new_t.value("preset", false));
		// every combination of the parameters' values (and flip)
		std::function<void(size_t, json, json)> each = [&](size_t i, json old_e, json new_e) {
			if (i == olds.size()) {
				for (const bool flip : {false, true}) {
					json a = old_e, b = new_e;
					if (flip) {
						a["flip"] = b["flip"] = true;
					}
					CAPTURE(a);
					const std::vector<json> want = expand(old_t, a);
					const std::vector<json> got = expand(new_t, b);
					REQUIRE(want.size() == got.size());
					for (size_t k = 0; k < want.size(); k++) {
						CHECK(rcg::canonical(got[k]) == rcg::canonical(want[k]));
					}
					n++;
				}
				return;
			}
			for (const json &v : olds[i].values) {
				json a = old_e, b = new_e;
				a[olds[i].name] = v;
				const auto [p, w] = olds[i].spell(v);
				b[p] = w;
				each(i + 1, a, b);
			}
		};
		each(0, json{{"kind", kind}}, json{{"kind", kind}});
		// and every parameter left at its default but the required ones
		json a = {{"kind", kind}}, b = {{"kind", kind}};
		for (const Old &o : olds) {
			if (!old_t["params"][o.name].contains("default") || (kind == "button" && o.name == "flag")) { // (a button's flag: door:<opens>, else required)
				a[o.name] = o.values.front();
				const auto [p, w] = o.spell(o.values.front());
				b[p] = w;
			}
		}
		CAPTURE(a);
		CHECK(rcg::canonical(expand(new_t, b)) == rcg::canonical(expand(old_t, a)));
	}
	CHECK(n > 1000);
}

TEST_CASE("surface element: a surface's parameters read back from it (Stage 207)") {
	const json &ps = se::params();
	const json local = {{"start", {-20.0, 0.0}}, {"end", {20.0, 0.0}}, {"left", nullptr}, {"right", nullptr}};
	size_t n = 0;
	for (const char *face : {"stop", "pass", "mirror"}) {
		for (const json &back : {json(), json("stop"), json("pass"), json("mirror")}) {
			for (const char *body : {"pass", "block", "platform"}) {
				for (const json &plannable : {json(), json(true), json(false)}) {
					for (int features = 0; features < 1 << 8; features += 7) { // (a sample of the 256 sets)
						json env = {{"name", "e"}};
						for (auto it = ps.begin(); it != ps.end(); ++it) {
							env[it.key()] = it.value()["default"];
						}
						env["channel"] = "e";
						env["face"] = face;
						env["back"] = back;
						env["body"] = body;
						env["plannable"] = plannable;
						if (features & 1) env["flag"] = "lamp";
						if (features & 2) env["target"] = true;
						if (features & 4) env["grapple"] = true;
						if (features & 8) env["spawn"] = local;
						if (features & 16) env["channels"] = json::array({"a"});
						if (features & 32) env["breaks"] = true;
						if (features & 64) env["ghost"] = true;
						if (features & 128) env["open"] = "!det:plate";
						const json s = se::compose(env, "test");
						const auto d = se::decompose(s);
						CAPTURE(env);
						REQUIRE(d.has_value());
						// what it reads composes the same surface again
						json again = env;
						for (auto it = d->begin(); it != d->end(); ++it) {
							again[it.key()] = it.value();
						}
						CHECK(rcg::canonical(se::compose(again, "test")) == rcg::canonical(s));
						n++;
					}
				}
			}
		}
	}
	CHECK(n > 1000);
	// what no surface element composes
	CHECK_FALSE(se::decompose(json::parse(R"({"left": {"effect": {"type": "inversion"}, "interactive": true}, "right": null, "player_solid": true})")));
	CHECK_FALSE(se::decompose(json::parse(R"({"left": null, "right": null, "player_solid": false, "resolver": {"key": "f", "table": []}})")));
	CHECK_FALSE(se::decompose(json::parse(
			R"({"left": {"effect": {"type": "pass"}, "hits": [{"kind": "target"}], "interactive": true}, "right": {"effect": {"type": "pass"}, "interactive": false}, "player_solid": false})")));
}

TEST_CASE("surface element: the features combine -- a blocking button, a door that is a button, a breakable target (Stage 207)") {
	auto surfaces = [](const json &element) {
		std::vector<json> out;
		const json low = rcg::lower_level(level_of(element)).level; // (held: a temporary's member dies before the loop)
		for (const json &s : low["surfaces"]) {
			if (s.contains("label") && s["label"] != "Surface" && !s["label"].is_null()) {
				out.push_back(s);
			}
		}
		return out;
	};
	SUBCASE("a button the player and objects collide with (the user's request)") {
		const auto ss = surfaces(json{{"kind", "button"}, {"name", "b"}, {"opens", "gate"}, {"body", "block"}, {"start", {700.0, 300.0}}, {"end", {800.0, 300.0}}});
		REQUIRE(ss.size() == 1);
		CHECK(ss[0]["label"] == "Button");
		CHECK(ss[0]["player_solid"] == true);
		CHECK(ss[0]["left"]["hits"] == json::parse(R"([{"key": "door:gate", "kind": "state", "op": "toggle", "value": true}])"));
		CHECK(ss[0]["left"]["effect"]["type"] == "pass");
		CHECK(ss[0]["left"]["interactive"] == true);
		const std::vector<rcg::BuiltSurface> built =
				rcg::build_level(rcg::parse_level_json(level_of(json{{"kind", "button"}, {"name", "b"}, {"flag", "f"}, {"body", "block"}, {"start", {700.0, 300.0}}, {"end", {800.0, 300.0}}})));
		bool found = false;
		for (const rcg::BuiltSurface &s : built) {
			if (s.label && *s.label == "Button") {
				found = true;
				CHECK(s.player_solid);
			}
		}
		CHECK(found);
	}
	SUBCASE("a door that is also a button; a breakable target; a platform mirror") {
		auto ss = surfaces(json{{"kind", "door"}, {"name", "d"}, {"flag", "lamp"}, {"start", {700.0, 300.0}}, {"end", {800.0, 300.0}}});
		REQUIRE(ss.size() == 1);
		CHECK(ss[0]["presence"] == json::parse(R"([{"key": "door:d", "values": ["DEFAULT", "false"]}])"));
		CHECK(ss[0]["left"]["hits"] == json::parse(R"([{"key": "lamp", "kind": "state", "op": "toggle", "value": true}])"));
		CHECK(ss[0]["player_solid"] == true);
		ss = surfaces(json{{"kind", "target"}, {"name", "t"}, {"breaks", true}, {"open", "det:p"}, {"start", {700.0, 300.0}}, {"end", {800.0, 300.0}}});
		REQUIRE(ss.size() == 1);
		CHECK(ss[0]["left"]["hits"] == json::parse(R"([{"kind": "target"}, {"key": "shot:t:broken", "kind": "state", "op": "set", "value": true}])"));
		CHECK(ss[0]["presence"] == json::parse(R"([{"key": "det:p", "values": ["DEFAULT", "false"]}, {"key": "shot:t:broken", "values": ["DEFAULT"]}])"));
		ss = surfaces(json{{"kind", "mirror"}, {"name", "m"}, {"body", "platform"}, {"start", {700.0, 300.0}}, {"end", {800.0, 300.0}}});
		REQUIRE(ss.size() == 1);
		CHECK(ss[0]["platform"] == true);
		CHECK_FALSE(ss[0].contains("player_solid"));
		CHECK(ss[0]["left"]["effect"]["type"] == "reflection");
	}
	SUBCASE("a preset's own feature is required; a feature's parameters are any surface's") {
		auto err = [](const json &element) {
			try {
				rcg::lower_level(level_of(element));
			} catch (const rcg::LevelError &e) {
				return std::string(e.what());
			}
			return std::string("no error");
		};
		CHECK(err(json{{"kind", "button"}, {"start", {0.0, 0.0}}, {"end", {0.0, 10.0}}}).find("'flag' required (or opens)") != std::string::npos);
		CHECK(err(json{{"kind", "spawner"}, {"start", {0.0, 0.0}}, {"end", {0.0, 10.0}}}).find("'spawn' required") != std::string::npos);
		CHECK(err(json{{"kind", "wall"}, {"body", "solid"}, {"start", {0.0, 0.0}}, {"end", {0.0, 10.0}}}).find("body: one of pass, block, platform expected")
				!= std::string::npos);
		CHECK(err(json{{"kind", "wall"}, {"solid", true}, {"start", {0.0, 0.0}}, {"end", {0.0, 10.0}}}).find("unknown key 'solid'") != std::string::npos);
		CHECK(err(json{{"kind", "fence"}, {"grapple", true}, {"start", {0.0, 0.0}}, {"end", {0.0, 10.0}}}) == "no error");
	}
}

TEST_CASE("surface element: migrate picks the preset of the run's label, writing what differs from it (Stage 207)") {
	const json lv = {{"schema", "rc-level/2"},
			{"elements", json::array({ // (named as migrate names them: the lowering keeps no names)
								 json{{"kind", "fence"}, {"name", "fence_1"}, {"start", {700.0, 300.0}}, {"end", {700.0, 700.0}}, {"via", {700.0, 500.0}}},
								 json{{"kind", "button"}, {"name", "button_1"}, {"flag", "lamp"}, {"body", "block"}, {"start", {800.0, 300.0}}, {"end", {800.0, 700.0}},
										 {"via", {800.0, 500.0}}},
								 json{{"kind", "one_sided_mirror"}, {"name", "one_sided_mirror_1"}, {"flip", true}, {"start", {900.0, 300.0}}, {"end", {900.0, 700.0}},
										 {"via", {900.0, 500.0}}},
								 json{{"kind", "shield"}, {"name", "shield_1"}, {"label", "Lens"}, {"start", {1000.0, 300.0}}, {"end", {1000.0, 700.0}},
										 {"via", {1000.0, 500.0}}},
								 json{{"kind", "door"}, {"name", "door_1"}, {"face", "mirror"}, {"start", {1100.0, 300.0}}, {"end", {1100.0, 700.0}},
										 {"via", {1100.0, 500.0}}}})}};
	const json back = rcg::migrate_level(rcg::lower_level(lv).level);
	json got = json::array();
	for (const json &e : back["elements"]) {
		if (e.value("kind", "") != "edges") {
			got.push_back(e);
		}
	}
	CHECK(rcg::canonical(got) == rcg::canonical(lv["elements"]));
}
