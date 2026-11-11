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
			{"switch", "when", {"when", "then_face", "then_back"}}, // Stage 214
			{"armed", "armed", {"armed"}},
	};
	return fs;
}

bool on(const Feature &f, const json &element, const json &params) {
	auto set_or_default = [&](const std::string &p) {
		const json v = element.contains(p) ? element[p] : params.value(p, json::object()).value("default", json());
		return !v.is_null() && v != json(false);
	};
	return set_or_default(f.key) || (f.name == "button" && set_or_default("opens"));
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
			// Stage 214: a face by flag; hits by flag
			{"when", {{"default", nullptr}, {"type", "string"}, {"doc", "a flag expression: while it holds the sides are then_face and then_back (a switchable)"}}},
			{"then_face", {{"default", "pass"}, {"enum", FACES}, {"doc", "the face while when holds"}}},
			{"then_back", {{"default", nullptr}, {"enum", FACES}, {"doc", "the right side's face while when holds (unset: then_face)"}}},
			{"armed", {{"default", nullptr}, {"type", "string"}, {"doc", "a flag expression: the hits apply only while it holds (a button only a charged arrow presses)"}}},
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
	auto side = [&](const std::string &f, bool with_hits) {
		json s = {{"effect", {{"type", effect_of(f)}}}};
		if (with_hits && !hits.empty()) {
			s["hits"] = hits;
		}
		s["interactive"] = set(env, "plannable") ? env["plannable"].get<bool>() : f == "mirror" || (with_hits && !hits.empty());
		return s;
	};
	// Stage 214: a face by flag (switch: when) and hits by flag (armed) -- one resolver on the flag, its `true` row the
	// then_ faces and the hits, its DEFAULT row the faces, with the hits unless armed (switchable's row order)
	const bool switched = set(env, "when"), armed = set(env, "armed");
	if (switched && armed && env["when"] != env["armed"]) {
		fail(path + ".armed", "one flag per surface: when and armed differ");
	}
	json out;
	if (switched || armed) {
		const std::string then_face = switched ? env.value("then_face", "pass") : face;
		const std::string then_back = switched ? (set(env, "then_back") ? env["then_back"].get<std::string>() : then_face) : back;
		json on = json::object(), otherwise = json::object();
		on["left"] = side(then_face, true);
		on["right"] = side(then_back, true);
		otherwise["left"] = side(face, !armed);
		otherwise["right"] = side(back, !armed);
		json table = json::array();
		table.push_back(json::array({json("true"), on}));
		table.push_back(json::array({json("DEFAULT"), otherwise}));
		out = json::object();
		out["left"] = nullptr;
		out["right"] = nullptr;
		out["resolver"] = json::object({{"key", switched ? env["when"] : env["armed"]}, {"table", table}});
	} else {
		out = {{"left", side(face, true)}, {"right", side(back, true)}};
	}
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
		static const std::set<std::string> keys = {"left", "right", "player_solid", "platform", "presence", "resolver"};
		if (!keys.count(it.key())) {
			return std::nullopt;
		}
	}
	// a pair of sides: their faces, their hits (one list for both), their interactivity
	struct Pair {
		std::string face, back;
		json hits, li, ri;
	};
	auto read_pair = [](const json &l, const json &r) -> std::optional<Pair> {
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
		return Pair{*face, *back, hits, l.value("interactive", json()), r.value("interactive", json())};
	};
	json env = json::object();
	std::vector<std::pair<Pair, bool>> pairs; // (each pair, whether its hits are the surface's)
	json hits;
	if (s.contains("resolver")) { // Stage 214: a switch (when) or armed hits -- two rows, true and DEFAULT
		const json &rv = s["resolver"];
		if (!s.value("left", json()).is_null() || !s.value("right", json()).is_null() || !rv.is_object() || rv.size() != 2
				|| !rv.value("key", json()).is_string() || !rv.value("table", json()).is_array() || rv["table"].size() != 2) {
			return std::nullopt;
		}
		std::optional<Pair> on, otherwise;
		for (const json &row : rv["table"]) {
			if (!row.is_array() || row.size() != 2 || !row[1].is_object() || row[1].size() != 2) {
				return std::nullopt;
			}
			std::optional<Pair> &slot = row[0] == "true" ? on : row[0] == "DEFAULT" ? otherwise : on;
			if (row[0] != "true" && row[0] != "DEFAULT") {
				return std::nullopt;
			}
			slot = read_pair(row[1].value("left", json()), row[1].value("right", json()));
			if (!slot) {
				return std::nullopt;
			}
		}
		if (!on || !otherwise) {
			return std::nullopt;
		}
		const bool armed = !on->hits.empty() && otherwise->hits.empty();
		if (!armed && canonical(on->hits) != canonical(otherwise->hits)) {
			return std::nullopt;
		}
		const bool switched = on->face != otherwise->face || on->back != otherwise->back;
		if (!switched && !armed) {
			return std::nullopt; // (two rows alike: no surface element writes it)
		}
		if (switched) {
			env["when"] = rv["key"];
			env["then_face"] = on->face;
			if (on->back != on->face) {
				env["then_back"] = on->back;
			}
		}
		if (armed) {
			env["armed"] = rv["key"];
		}
		env["face"] = otherwise->face;
		if (otherwise->back != otherwise->face) {
			env["back"] = otherwise->back;
		}
		hits = on->hits;
		pairs = {{*on, true}, {*otherwise, !armed}};
	} else {
		const auto p = read_pair(s.value("left", json()), s.value("right", json()));
		if (!p) {
			return std::nullopt;
		}
		env["face"] = p->face;
		if (p->back != p->face) {
			env["back"] = p->back;
		}
		hits = p->hits;
		pairs = {{*p, true}};
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
	// the interactivity: the rule's on every side, or one value for all of them
	std::vector<std::pair<json, bool>> sides; // (its value, the rule's)
	for (const auto &[p, own] : pairs) {
		const bool with = own && !hits.empty();
		sides.push_back({p.li, p.face == "mirror" || with});
		sides.push_back({p.ri, p.back == "mirror" || with});
	}
	bool by_rule = true;
	for (const auto &[v, rule] : sides) {
		if (!v.is_boolean()) {
			return std::nullopt;
		}
		by_rule = by_rule && v.get<bool>() == rule;
	}
	if (!by_rule) {
		for (const auto &[v, rule] : sides) {
			if (v != sides.front().first) {
				return std::nullopt;
			}
		}
		env["plannable"] = sides.front().first;
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
