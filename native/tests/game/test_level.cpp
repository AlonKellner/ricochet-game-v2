// Stage 115.2: the level data (rc-level/1). Every level under data/levels,
// built natively and described as scene defs, equals the level dump the
// GDScript game wrote (bench/scenes/<dir>__<name>.surfaces.json), bit for bit.
#include "rcg/build.hpp"
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
// float64 bits (an int equals the float of the same value), objects by keys.
// The dumps are Godot's JSON, which writes -0.0 as 0.0 (TDD_20 F3): a zero
// there stands for either sign.
std::string first_difference(const json &a, const json &b, const std::string &path) {
	if (a.is_number() && b.is_number()) {
		const double x = a.get<double>(), y = b.get<double>();
		if (std::bit_cast<std::uint64_t>(x) != std::bit_cast<std::uint64_t>(y) && !(x == 0.0 && y == 0.0)) {
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
		std::string dump_name = rel;
		for (char &c : dump_name) {
			if (c == '/') {
				c = '_';
				dump_name.insert(size_t(&c - dump_name.data()), "_");
			}
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
	CHECK_THROWS_WITH_AS(rcg::parse_level(R"({"schema": "rc-level/2", "surfaces": []})"), doctest::Contains("rc-level/1"), rcg::LevelError);
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
