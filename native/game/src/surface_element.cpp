// Stage 207: the surface element (rcg/surface_element.hpp, GAME_SPEC 9.13).
#include "rcg/surface_element.hpp"

#include "rcg/canon.hpp"
#include "rcg/level.hpp"

#include <algorithm>
#include <set>

namespace rcg::surface_element {

namespace {

[[noreturn]] void fail(const std::string &path, const std::string &what) { throw LevelError(path + ": " + what); }

const json FACES = {"stop", "pass", "mirror"};

std::string effect_of(const std::string &face) { return face == "stop" ? "terminal" : face == "mirror" ? "reflection" : "pass"; }

std::optional<std::string> face_of(const json &effect) {
	if (!effect.is_object() || effect.size() != 1 || !effect.contains("type") || !effect["type"].is_string()) {
		return std::nullopt;
	}
	const std::string t = effect["type"].get<std::string>();
	if (t == "terminal") return "stop";
	if (t == "reflection") return "mirror";
	if (t == "pass") return "pass";
	return std::nullopt;
}

bool set(const json &env, const char *p) { return env.contains(p) && !env[p].is_null(); }

std::string broken(const std::string &name) { return "shot:" + name + ":broken"; }

// "shot:<name>:broken" -> name
std::optional<std::string> broken_name(const std::string &key) {
	const std::string head = "shot:", tail = ":broken";
	if (key.size() <= head.size() + tail.size() || key.compare(0, head.size(), head) != 0
			|| key.compare(key.size() - tail.size(), tail.size(), tail) != 0) {
		return std::nullopt;
	}
	return key.substr(head.size(), key.size() - head.size() - tail.size());
}

} // namespace

const std::vector<Feature> &features() {
	static const std::vector<Feature> fs = {
			{"button", "flag", {"flag", "opens", "op", "value", "deferred"}},
			{"target", "target", {"target"}},
			{"grapple", "grapple", {"grapple"}},
			{"spawn", "spawn", {"spawn", "channel", "orientation", "mirror_with_side", "offset", "trim", "deferred"}},
			{"reset", "channels", {"channels", "deferred"}},
			{"break", "breaks", {"breaks"}},
			{"ghost", "ghost", {"ghost"}},
			{"door", "open", {"open"}},
	};
	return fs;
}

std::string feature_of(const std::string &param) {
	for (const Feature &f : features()) { // (deferred: the button's, the first that reads it)
		if (std::find(f.params.begin(), f.params.end(), param) != f.params.end()) {
			return f.name;
		}
	}
	return "";
}

const json &params() {
	static const json ps = {
			{"face", {{"default", "stop"}, {"enum", FACES}, {"doc", "what the arrow does: stop, pass or mirror (the left side; both unless back)"}}},
			{"back", {{"default", nullptr}, {"enum", FACES}, {"doc", "the right side's face (unset: the face)"}}},
			{"body", {{"default", "block"}, {"enum", {"pass", "block", "platform"}}, {"doc", "what the player and objects do: pass, block, or stand on it from above"}}},
			{"plannable", {{"default", nullptr}, {"type", "bool"}, {"doc", "the sides can be planned (unset: a side that reflects or has hits)"}}},
			{"flag", {{"default", nullptr}, {"type", "string"}, {"doc", "a flag the hit writes (a button)"}}},
			{"opens", {{"default", nullptr}, {"type", "string"}, {"doc", "the door the hit opens (writes its flag door:<name>)"}}},
			{"op", {{"default", "toggle"}, {"enum", {"toggle", "set"}}, {"doc", "toggle the flag, or set it to value (a one-shot button)"}}},
			{"value", {{"default", true}, {"type", "any"}, {"doc", "the value set"}}},
			{"deferred", {{"default", false}, {"doc", "the hit's writes applied when the arrow lands instead of in the shot"}}},
			{"target", {{"default", false}, {"doc", "hitting it registers the target (GAME_SPEC 24)"}}},
			{"grapple", {{"default", false}, {"doc", "the hit pulls the player to it when the shot lands"}}},
			{"spawn", {{"default", nullptr}, {"type", "object"}, {"doc", "a surface (local frame) the hit places at the hit"}}},
			{"channel", {{"default", "${name}"}, {"type", "string"}, {"doc", "the spawn's channel (a resetter re-arms it)"}}},
			{"orientation", {{"default", "CARRIER_TANGENT"},
					{"enum", {"BEAM_TANGENT", "BEAM_NORMAL", "CARRIER_TANGENT", "CARRIER_NORMAL", "BEAM_REFLECTED", "WORLD"}},
					{"doc", "how the spawned surface is turned"}}},
			{"mirror_with_side", {{"default", false}, {"doc", "the spawned surface mirrored by the side hit"}}},
			{"offset", {{"default", {0.0, 0.0}}, {"type", "point"}, {"doc", "the spawned surface's offset"}}},
			{"trim", {{"default", true}, {"doc", "the spawned surface trimmed where it crosses others"}}},
			{"channels", {{"default", nullptr}, {"type", "array"}, {"doc", "the spawn channels the hit re-arms (a resetter)"}}},
			{"breaks", {{"default", false}, {"doc", "gone for the rest of the shot once hit, back when the arrow lands"}}},
			{"ghost", {{"default", false}, {"doc", "the hit toggles the arrow's ghost mode"}}},
			{"open", {{"default", nullptr}, {"type", "string"}, {"doc", "a flag expression: the surface is gone while it holds (a door)"}}},
	};
	return ps;
}

json compose(const json &env, const std::string &path) {
	const std::string face = env.value("face", "stop");
	const std::string back = set(env, "back") ? env["back"].get<std::string>() : face;
	const bool deferred = env.value("deferred", false);
	json hits = json::array();
	if (set(env, "flag") || set(env, "opens")) {
		const std::string key = set(env, "flag") ? env["flag"].get<std::string>() : "door:" + env["opens"].get<std::string>();
		if (key.empty()) {
			fail(path + ".flag", "a flag name expected");
		}
		json h = {{"key", key}, {"kind", "state"}, {"op", env.value("op", "toggle")}, {"value", env.value("value", json(true))}};
		if (deferred) {
			h["deferred"] = true;
		}
		hits.push_back(std::move(h));
	}
	if (env.value("target", false)) {
		hits.push_back({{"kind", "target"}});
	}
	if (env.value("grapple", false)) {
		hits.push_back({{"kind", "grapple"}});
	}
	if (set(env, "spawn")) {
		hits.push_back({{"channel", env["channel"]}, {"deferred", deferred}, {"kind", "spawn"},
				{"template", {{"mirror_with_side", env["mirror_with_side"]}, {"offset", env["offset"]}, {"orientation", env["orientation"]},
									 {"surface", env["spawn"]}, {"trim", env["trim"]}}}});
	}
	if (set(env, "channels")) {
		hits.push_back({{"channels", env["channels"]}, {"deferred", deferred}, {"kind", "reset"}});
	}
	const std::string name = env.value("name", "");
	if (env.value("breaks", false)) {
		hits.push_back({{"key", broken(name)}, {"kind", "state"}, {"op", "set"}, {"value", true}});
	}
	if (env.value("ghost", false)) {
		hits.push_back({{"key", "ghost"}, {"kind", "state"}, {"op", "toggle"}, {"value", true}});
	}
	auto side = [&](const std::string &f) {
		json s = {{"effect", {{"type", effect_of(f)}}}};
		if (!hits.empty()) {
			s["hits"] = hits;
		}
		s["interactive"] = set(env, "plannable") ? env["plannable"].get<bool>() : f == "mirror" || !hits.empty();
		return s;
	};
	json out = {{"left", side(face)}, {"right", side(back)}};
	const std::string body = env.value("body", "block");
	if (body == "platform") {
		out["platform"] = true;
	} else {
		out["player_solid"] = body == "block";
	}
	json presence = json::array();
	if (set(env, "open")) {
		presence.push_back({{"key", env["open"]}, {"values", {"DEFAULT", "false"}}});
	}
	if (env.value("breaks", false)) {
		presence.push_back({{"key", broken(name)}, {"values", {"DEFAULT"}}});
	}
	if (!presence.empty()) {
		out["presence"] = std::move(presence);
	}
	return out;
}

std::optional<json> decompose(const json &s) {
	if (!s.is_object()) {
		return std::nullopt;
	}
	for (auto it = s.begin(); it != s.end(); ++it) {
		static const std::set<std::string> keys = {"left", "right", "player_solid", "platform", "presence"};
		if (!keys.count(it.key())) {
			return std::nullopt;
		}
	}
	const json &l = s.value("left", json()), &r = s.value("right", json());
	if (!l.is_object() || !r.is_object()) {
		return std::nullopt;
	}
	for (const json *side : {&l, &r}) {
		for (auto it = side->begin(); it != side->end(); ++it) {
			if (it.key() != "effect" && it.key() != "hits" && it.key() != "interactive") {
				return std::nullopt;
			}
		}
	}
	const auto face = face_of(l.value("effect", json())), back = face_of(r.value("effect", json()));
	const json hits = l.value("hits", json::array());
	if (!face || !back || !hits.is_array() || canonical(hits) != canonical(r.value("hits", json::array()))) {
		return std::nullopt;
	}
	json env = json::object();
	env["face"] = *face;
	if (*back != *face) {
		env["back"] = *back;
	}
	std::optional<bool> deferred;
	auto agree = [&](bool d) {
		if (deferred && *deferred != d) {
			return false;
		}
		deferred = d;
		return true;
	};
	for (const json &h : hits) {
		const std::string kind = h.value("kind", "");
		if (kind == "target" || kind == "grapple") {
			if (h.size() != 1 || env.contains(kind)) {
				return std::nullopt;
			}
			env[kind] = true;
		} else if (kind == "spawn") {
			const json &t = h.value("template", json());
			if (env.contains("spawn") || !h.contains("channel") || !h.value("deferred", json()).is_boolean() || !t.is_object()
					|| !agree(h["deferred"].get<bool>())) {
				return std::nullopt;
			}
			env["spawn"] = t.value("surface", json());
			env["channel"] = h["channel"];
			for (const char *k : {"mirror_with_side", "offset", "orientation", "trim"}) {
				env[k] = t.value(k, json());
			}
		} else if (kind == "reset") {
			if (env.contains("channels") || !h.value("deferred", json()).is_boolean() || !agree(h["deferred"].get<bool>())) {
				return std::nullopt;
			}
			env["channels"] = h.value("channels", json());
		} else if (kind == "state" && h.value("key", json()).is_string()) {
			const std::string key = h["key"].get<std::string>();
			const bool plain = h.size() == 4 && h.value("value", json()) == json(true);
			if (plain && key == "ghost" && h.value("op", "") == "toggle" && !env.contains("ghost")) {
				env["ghost"] = true;
			} else if (const auto n = broken_name(key); plain && n && h.value("op", "") == "set" && !env.contains("breaks")) {
				env["breaks"] = true;
				env["name"] = *n;
			} else {
				if (env.contains("flag") || (h.contains("deferred") && h["deferred"] != json(true)) || !agree(h.contains("deferred"))) {
					return std::nullopt;
				}
				env["flag"] = key;
				env["op"] = h.value("op", json());
				env["value"] = h.value("value", json());
			}
		} else {
			return std::nullopt;
		}
	}
	if (deferred && *deferred) {
		env["deferred"] = true;
	}
	// the interactivity: the rule's, or one value for both sides
	const json li = l.value("interactive", json()), ri = r.value("interactive", json());
	if (!li.is_boolean() || !ri.is_boolean()) {
		return std::nullopt;
	}
	const bool lauto = *face == "mirror" || !hits.empty(), rauto = *back == "mirror" || !hits.empty();
	if (li.get<bool>() != lauto || ri.get<bool>() != rauto) {
		if (li != ri) {
			return std::nullopt;
		}
		env["plannable"] = li;
	}
	if (s.value("platform", false)) {
		if (s.contains("player_solid")) {
			return std::nullopt;
		}
		env["body"] = "platform";
	} else if (s.value("player_solid", json()).is_boolean()) {
		env["body"] = s["player_solid"].get<bool>() ? "block" : "pass";
	} else {
		return std::nullopt;
	}
	for (const json &c : s.value("presence", json::array())) {
		if (!c.is_object() || !c.value("key", json()).is_string()) {
			return std::nullopt;
		}
		const std::string key = c["key"].get<std::string>();
		if (c.value("values", json()) == json::array({"DEFAULT", "false"}) && !env.contains("open")) {
			env["open"] = key;
		} else if (!(env.contains("name") && key == broken(env["name"].get<std::string>()) && c.value("values", json()) == json::array({"DEFAULT"}))) {
			return std::nullopt;
		}
	}
	return env;
}

} // namespace rcg::surface_element
