// Stage 115.2: the level data (rc-level/1). Every level under data/levels,
// built natively and described as scene defs, equals its level dump
// (bench/scenes/<dir>__<name>.surfaces.json), bit for bit. The dumps were the
// GDScript game's until 115.3 (they matched then, F3 aside); rc_tool writes
// them now, so the game's side of the parity is the GUT tests
// (test_stage81_scene_switch, test_stage107_level_parity: the live game's defs).
#include "rcg/build.hpp"
#include "rcg/canon.hpp"
#include "json_defs.hpp"
#include "rcg/level.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

const fs::path ROOT = RC_REPO_ROOT;

json read_json(const fs::path &p) {
	std::ifstream in(p);
	REQUIRE_MESSAGE(in.good(), p.string());
	return json::parse(in);
}

// The first place two JSON values differ ("" when equal): numbers by their
// float64 bits (an int equals the float of the same value; a zero keeps its
// sign: the dumps are rc_tool's since Stage 115, Godot's JSON dropped it, F3),
// objects by keys.
std::string first_difference(const json &a, const json &b, const std::string &path) {
	if (a.is_number() && b.is_number()) {
		const double x = a.get<double>(), y = b.get<double>();
		if (std::bit_cast<std::uint64_t>(x) != std::bit_cast<std::uint64_t>(y)) {
			std::ostringstream ss;
			ss.precision(17);
			ss << path << ": " << x << " vs " << y;
			return ss.str();
		}
		return "";
	}
	if (a.type() != b.type()) {
		return path + ": " + a.dump() + " vs " + b.dump();
	}
	if (a.is_object()) {
		for (auto it = a.begin(); it != a.end(); ++it) {
			if (!b.contains(it.key())) {
				return path + "." + it.key() + ": only in the native defs";
			}
			std::string d = first_difference(it.value(), b[it.key()], path + "." + it.key());
			if (!d.empty()) {
				return d;
			}
		}
		for (auto it = b.begin(); it != b.end(); ++it) {
			if (!a.contains(it.key())) {
				return path + "." + it.key() + ": only in the dump";
			}
		}
		return "";
	}
	if (a.is_array()) {
		if (a.size() != b.size()) {
			return path + ": " + std::to_string(a.size()) + " vs " + std::to_string(b.size()) + " elements";
		}
		for (size_t i = 0; i < a.size(); i++) {
			std::string d = first_difference(a[i], b[i], path + "[" + std::to_string(i) + "]");
			if (!d.empty()) {
				return d;
			}
		}
		return "";
	}
	return a == b ? "" : path + ": " + a.dump() + " vs " + b.dump();
}

} // namespace

TEST_CASE("level data: every level built natively equals the game's level dump") {
	int n = 0;
	for (const auto &e : fs::recursive_directory_iterator(ROOT / "data" / "levels")) {
		if (e.path().extension() != ".json") {
			continue;
		}
		const std::string rel = fs::relative(e.path(), ROOT / "data" / "levels").replace_extension("").generic_string();
		std::string dump_name;
		for (const char c : rel) {
			dump_name += c == '/' ? std::string("__") : std::string(1, c);
		}
		const fs::path dump = ROOT / "bench" / "scenes" / (dump_name + ".surfaces.json");
		CAPTURE(rel);
		const rcg::Level level = rcg::load_level(e.path().string());
		CHECK(level.name == rel);
		const json defs = rcg::describe_level(rcg::build_level(level));
		const std::string diff = first_difference(defs, read_json(dump)["surfaces"], "surfaces");
		CHECK_MESSAGE(diff.empty(), diff);
		n++;
	}
	CHECK(n == 37);
}

TEST_CASE("level data: the schema is strict") {
	const std::string head = R"({"schema": "rc-level/1", "surfaces": [)";
	auto parse = [&](const std::string &surfaces) { return rcg::parse_level(head + surfaces + "]}"); };
	const std::string wall = R"({"ids": [1], "start": [0, 0], "end": [10, 0], "via": [5, 0], "left": null, "right": null})";
	CHECK(parse(wall).surfaces.size() == 1);
	CHECK_THROWS_WITH_AS(rcg::parse_level(R"({"schema": "rc-level/3", "surfaces": []})"), doctest::Contains("rc-level/1 or rc-level/2"), rcg::LevelError);
	CHECK_THROWS_WITH_AS(parse(R"({"ids": [1], "start": [0, 0], "end": [10, 0], "via": [5, 0], "colour": 1})"),
			doctest::Contains("unknown key 'colour'"), rcg::LevelError);
	CHECK_THROWS_WITH_AS(parse(R"({"ids": [1], "end": [10, 0], "via": [5, 0]})"), doctest::Contains("'start' required"), rcg::LevelError);
	const std::string portal_side = R"({"effect": {"type": "portal", "theta": 0.0, "d": [100.0, 0.0]}, "interactive": true})";
	CHECK_THROWS_WITH_AS(parse(R"({"ids": [1], "start": [0, 0], "end": [10, 0], "via": [5, 0], "left": )" + portal_side + "}"),
			doctest::Contains("a portal takes two ids"), rcg::LevelError);
	CHECK_THROWS_WITH_AS(rcg::parse_level("{"), doctest::Contains("not JSON"), rcg::LevelError);
	// a spec that does not validate is refused by the builder
	const rcg::Level bad = parse(R"({"ids": [1], "start": [0, 0], "end": [10, 0], "via": [5, 0], "left": {"effect": {"type": "inversion"}}})");
	CHECK_THROWS_WITH_AS(rcg::build_level(bad), doctest::Contains("inversion needs a circle carrier"), rcg::LevelError);
}

TEST_CASE("level data: the portal image (RigidMotionEffect.apply_motion)") {
	// float64 arithmetic on the float32 point, rounded to float32
	const rc::V2 z = rc::V2::of(550.0, 600.0);
	CHECK(rcg::apply_motion(z, 0.0, rc::V2::of(800.0, 0.0)) == rc::V2::of(1350.0, 600.0));
	// the inverse's displacement: -T_{-theta}(d) adds +0.0 (a -0.0 component becomes +0.0, then negated)
	const rc::V2 back = -rcg::apply_motion(rc::V2::of(800.0, 0.0), -0.0, rc::V2{});
	CHECK(std::signbit(back.y));
}

TEST_CASE("level data: a directional normal is normalized (float32), as the game's effect holds it") {
	const rcg::Level lv = rcg::parse_level(R"({"schema": "rc-level/1", "surfaces": [
		{"ids": [1], "start": [0, 0], "end": [100, 0], "via": [50, 0],
		 "left": {"effect": {"type": "line_directional_projection", "normal": [3, 4]}, "interactive": true}, "right": null}]})");
	const nlohmann::json d = rcg::describe_level(rcg::build_level(lv));
	CHECK(d[0]["left"]["normal"][0].get<double>() == double(0.6f));
	CHECK(d[0]["left"]["normal"][1].get<double>() == double(0.8f));
}

TEST_CASE("canonical spelling: floats as Python's repr") {
	CHECK(rcg::float_repr(1920.0) == "1920.0");
	CHECK(rcg::float_repr(-0.0) == "-0.0");
	CHECK(rcg::float_repr(0.0) == "0.0");
	CHECK(rcg::float_repr(double(0.1f)) == "0.10000000149011612");
	CHECK(rcg::float_repr(366.79998779296875) == "366.79998779296875");
	CHECK(rcg::float_repr(0.0001) == "0.0001");
	CHECK(rcg::float_repr(0.00001) == "1e-05");
	CHECK(rcg::float_repr(1.5e-7) == "1.5e-07");
	CHECK(rcg::float_repr(1e16) == "1e+16");
	CHECK(rcg::float_repr(123456789012345678.0) == "1.2345678901234568e+17");
	CHECK(rcg::float_repr(9999999999999998.0) == "9999999999999998.0");
	CHECK(rcg::float_repr(-2.5) == "-2.5");
	const nlohmann::json doc = nlohmann::json::parse(R"({"b": [1, 2.0, "x"], "a": {"c": [{"d": null}], "e": {}}, "s": "∞\n"})");
	CHECK(rcg::canonical(doc) == "{\n \"a\": {\n  \"c\": [\n   {\n    \"d\": null\n   }\n  ],\n  \"e\": {}\n },\n \"b\": [1, 2.0, \"x\"],\n \"s\": \"∞\\n\"\n}\n");
}

TEST_CASE("level data: the solver's defs built directly equal the dumps' (the game's SceneDefs path)") {
	for (const auto &e : fs::recursive_directory_iterator(ROOT / "data" / "levels")) {
		if (e.path().extension() != ".json") {
			continue;
		}
		const std::string rel = fs::relative(e.path(), ROOT / "data" / "levels").replace_extension("").generic_string();
		std::string dump_name;
		for (const char c : rel) {
			dump_name += c == '/' ? std::string("__") : std::string(1, c);
		}
		CAPTURE(rel);
		std::ifstream in(ROOT / "bench" / "scenes" / (dump_name + ".surfaces.json"));
		const ojson dump = ojson::parse(in);
		std::vector<rc::SurfaceDef> want = rc::defs_from_json(dump["surfaces"]);
		std::vector<rc::SurfaceDef> got = rcg::defs_of(rcg::build_level(rcg::load_level(e.path().string())));
		// the dumps' JSON objects sort a resolver's table; the defs keep the level's order
		for (auto *defs : {&want, &got}) {
			for (rc::SurfaceDef &d : *defs) {
				if (d.resolver) {
					std::sort(d.resolver->table.begin(), d.resolver->table.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
				}
			}
		}
		REQUIRE(got.size() == want.size());
		for (size_t i = 0; i < got.size(); i++) {
			CAPTURE(got[i].id);
			CHECK(got[i] == want[i]);
		}
	}
}

TEST_CASE("level data: the builder validates as SideSpec / SpawnTemplate do") {
	auto build = [](const std::string &surface) {
		return rcg::build_level(rcg::parse_level(R"({"schema": "rc-level/1", "surfaces": [)" + surface + "]}"));
	};
	const std::string head = R"({"ids": [1], "start": [0, 0], "end": [100, 0], "via": [50, 0], "right": null, "left": )";
	CHECK_THROWS_WITH_AS(build(head + R"({"effect": {"type": "pass"}, "interactive": true}})"), doctest::Contains("nothing to plan"), rcg::LevelError);
	CHECK_THROWS_WITH_AS(build(head + R"({"effect": {"type": "pass"}, "interactive": true, "hits": [{"kind": "spawn", "channel": "a"}]}})"),
			doctest::Contains("a spawn needs a template"), rcg::LevelError);
	const std::string nested = R"({"kind": "spawn", "channel": "a", "template": {"surface": {"start": [0, 0], "end": [10, 0], "via": [5, 0],
		"left": {"effect": {"type": "pass"}, "interactive": true, "hits": [{"kind": "spawn", "channel": "b", "template": {"surface":
		{"start": [0, 0], "end": [1, 0], "via": [0.5, 0], "left": null, "right": null}}}]}, "right": null}}})";
	CHECK_THROWS_WITH_AS(build(head + R"({"effect": {"type": "pass"}, "interactive": true, "hits": [)" + nested + "]}}"),
			doctest::Contains("cannot spawn"), rcg::LevelError);
	CHECK(build(head + R"({"effect": {"type": "pass"}, "interactive": true, "hits": [{"kind": "target"}]}})").size() == 1);
}
