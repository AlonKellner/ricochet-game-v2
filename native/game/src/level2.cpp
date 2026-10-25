// rc-level/2: lowering to rc-level/1, and the migration back (rcg/level2.hpp).
#include "rcg/lattice.hpp"
#include "rcg/level2.hpp"

#include "rcg/canon.hpp"
#include "rcg/elements.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

namespace rcg {
namespace {

using json = nlohmann::json;

[[noreturn]] void fail(const std::string &path, const std::string &what) { throw LevelError(path + ": " + what); }

// The level keys rc-level/2 shares with rc-level/1 (copied to the lowering as they are).
const std::vector<std::string> META = {"name", "bounds", "gravity", "room", "player", "camera", "background", "colliders", "features"};
// Keys of the format's later stages (docs/LEVEL_FORMAT.md), refused until they exist.
const std::vector<std::string> RESERVED = {};

const json DEFAULT_BOUNDS = json::array({0.0, 0.0, 1920.0, 1080.0});
const json DEFAULT_ROOM = json::array({560.0, 240.0, 800.0, 600.0});

bool rect4(const json &r) {
	return r.is_array() && r.size() == 4 && std::all_of(r.begin(), r.end(), [](const json &v) { return v.is_number(); });
}

json side_of(const char *type) { return {{"effect", {{"type", type}}}, {"interactive", false}}; }

bool is_portal(const json &s) {
	if (s.value("pre_transform", false) || (s.contains("resolver") && !s["resolver"].is_null())) {
		return false;
	}
	const json l = s.value("left", json());
	return l.is_object() && l.contains("effect") && l["effect"].is_object() && l["effect"].value("type", "") == "portal";
}

json bounds_of(const json &lv) { return lv.contains("bounds") && rect4(lv["bounds"]) ? lv["bounds"] : DEFAULT_BOUNDS; }

// The room: its rect when it is built (and well formed), else null.
json built_room(const json &lv) {
	if (!lv.contains("room") || lv["room"].is_null()) {
		return DEFAULT_ROOM;
	}
	const json &r = lv["room"];
	if (!r.is_object() || (r.contains("built") && r["built"] != true) || !r.contains("rect") || !rect4(r["rect"])) {
		return json();
	}
	return r["rect"];
}

std::vector<json> room_walls(const json &rect) { return rect_walls(rect, side_of("terminal"), "Surface", true); }
std::vector<json> edge_walls(const json &lv) { return rect_walls(bounds_of(lv), side_of("pass"), json(), false); }

std::string text(const json &j) { return canonical(j); }

// Stage 149: the declared carriers (name -> {"line": {a, b}} | {"circle": {center, radius}}), checked
std::map<std::string, json> carriers_of(const json &v2) {
	std::map<std::string, json> out;
	if (!v2.contains("carriers")) {
		return out;
	}
	const json &cs = v2["carriers"];
	if (!cs.is_array()) {
		fail("level.carriers", "an array of carriers expected");
	}
	auto point = [](const json &p) { return p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number(); };
	for (size_t i = 0; i < cs.size(); i++) {
		const std::string cp = "level.carriers[" + std::to_string(i) + "]";
		const json &c = cs[i];
		if (!c.is_object() || !c.contains("name") || !c["name"].is_string() || c["name"].get<std::string>().empty()) {
			fail(cp, "{\"name\", \"line\": {\"a\", \"b\"}} or {\"name\", \"circle\": {\"center\", \"radius\"}} expected");
		}
		for (auto it = c.begin(); it != c.end(); ++it) {
			if (it.key() != "name" && it.key() != "line" && it.key() != "circle" && it.key() != "exact") {
				fail(cp, "unknown key '" + it.key() + "'");
			}
		}
		const bool line = c.contains("line"), circle = c.contains("circle");
		if (line == circle) {
			fail(cp, "a line or a circle expected");
		}
		if (line) {
			const json &l = c["line"];
			if (!l.is_object() || !point(l.value("a", json())) || !point(l.value("b", json())) || l["a"] == l["b"]) {
				fail(cp + ".line", "{\"a\": [x, y], \"b\": [x, y]}, two distinct points expected");
			}
		} else {
			const json &k = c["circle"];
			if (!k.is_object() || !point(k.value("center", json())) || !k.value("radius", json()).is_number() || !(k["radius"].get<double>() > 0)) {
				fail(cp + ".circle", "{\"center\": [x, y], \"radius\": r > 0} expected");
			}
		}
		if (c.contains("exact") && !c["exact"].is_boolean()) {
			fail(cp + ".exact", "true or false expected");
		}
		if (!out.emplace(c["name"].get<std::string>(), c).second) {
			fail(cp + ".name", "'" + c["name"].get<std::string>() + "' names another carrier");
		}
	}
	return out;
}

bool exact(const json &carrier) { return carrier.value("exact", false); }

// Stage 154: an exact carrier's lattice (rcg/lattice.hpp)
lattice::Line line_lattice(const json &c) {
	return lattice::line_of(c["line"]["a"][0].get<double>(), c["line"]["a"][1].get<double>(), c["line"]["b"][0].get<double>(), c["line"]["b"][1].get<double>());
}
const lattice::Circle &circle_lattice(const json &c) {
	return lattice::circle_of(c["circle"]["center"][0].get<double>(), c["circle"]["center"][1].get<double>(), c["circle"]["radius"].get<double>());
}

// A point onto a carrier (float64): the line's nearest point, the circle's point in its direction (an exact
// carrier: its nearest lattice point).
std::array<double, 2> project(const json &carrier, const json &p) {
	const double px = p[0].get<double>(), py = p[1].get<double>();
	if (exact(carrier)) {
		if (carrier.contains("line")) {
			return lattice::nearest(line_lattice(carrier), px, py);
		}
		const lattice::Circle &c = circle_lattice(carrier);
		return c.point(lattice::nearest(c, px, py));
	}
	if (carrier.contains("line")) {
		const double ax = carrier["line"]["a"][0].get<double>(), ay = carrier["line"]["a"][1].get<double>();
		const double dx = carrier["line"]["b"][0].get<double>() - ax, dy = carrier["line"]["b"][1].get<double>() - ay;
		const double t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);
		return {ax + t * dx, ay + t * dy};
	}
	const double cx = carrier["circle"]["center"][0].get<double>(), cy = carrier["circle"]["center"][1].get<double>();
	const double r = carrier["circle"]["radius"].get<double>();
	const double vx = px - cx, vy = py - cy, n = std::sqrt(vx * vx + vy * vy);
	return n > 0 ? std::array<double, 2>{cx + r * vx / n, cy + r * vy / n} : std::array<double, 2>{cx + r, cy};
}

// An element on a carrier ("carrier": its name) as one with its own geometry: from, to (and an arc's via) projected onto it -- a
// line straight between its ends, an arc through its via (default: the middle of the shorter arc from -> to);
// neither from nor to on a circle: the full circle.
json on_carrier(const json &e, const std::map<std::string, json> &carriers, const std::string &path) {
	const json &on = e["carrier"];
	if (!on.is_string() || !carriers.count(on.get<std::string>())) {
		fail(path + ".carrier", on.is_string() ? "no carrier named '" + on.get<std::string>() + "'" : "a carrier's name expected");
	}
	for (const char *k : {"start", "end", "circle", "straight"}) {
		if (e.contains(k)) {
			fail(path, std::string("'") + k + "': an element on a carrier takes from and to");
		}
	}
	const json &c = carriers.at(on.get<std::string>());
	json out = e;
	out.erase("carrier");
	out.erase("from");
	out.erase("to");
	out.erase("via");
	auto point = [](const json &p) { return p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number(); };
	if (!e.contains("from") && !e.contains("to")) {
		if (!c.contains("circle")) {
			fail(path, "from and to expected (a line carrier has no whole)");
		}
		if (exact(c)) {
			fail(path, "from and to expected (an exact circle carrier holds arcs: its whole is no float circle)");
		}
		out["circle"] = c["circle"];
		return out;
	}
	if (!point(e.value("from", json())) || !point(e.value("to", json()))) {
		fail(path, "from and to expected, [x, y] each");
	}
	const auto s = project(c, e["from"]), en = project(c, e["to"]);
	if (s == en) {
		fail(path, "from and to land on one point of the carrier");
	}
	out["start"] = json::array({s[0], s[1]});
	out["end"] = json::array({en[0], en[1]});
	if (c.contains("line")) {
		out["straight"] = true;
		out["via"] = json::array({(s[0] + en[0]) / 2, (s[1] + en[1]) / 2});
		return out;
	}
	if (e.contains("via")) {
		if (!point(e["via"])) {
			fail(path + ".via", "[x, y] expected");
		}
		const auto v = project(c, e["via"]);
		if (exact(c) && (v == s || v == en)) {
			fail(path + ".via", "the via lands on an end (an exact carrier's points are its lattice's)");
		}
		out["via"] = json::array({v[0], v[1]});
		return out;
	}
	// no via: the shorter arc (a half circle: the one of increasing angle)
	const double PI = 3.141592653589793;
	const double cx = c["circle"]["center"][0].get<double>(), cy = c["circle"]["center"][1].get<double>(), r = c["circle"]["radius"].get<double>();
	const double a0 = std::atan2(s[1] - cy, s[0] - cx), a1 = std::atan2(en[1] - cy, en[0] - cx);
	double span = std::fmod(a1 - a0, 2 * PI);
	if (span <= -PI) {
		span += 2 * PI;
	} else if (span > PI) {
		span -= 2 * PI;
	}
	if (span == -PI) {
		span = PI;
	}
	if (exact(c)) { // the lattice point nearest the middle, strictly between the ends
		const lattice::Circle &lc = circle_lattice(c);
		const size_t i = lattice::nearest_angle(lc, std::atan2(std::sin(a0 + span / 2), std::cos(a0 + span / 2)));
		const auto v = lc.point(i);
		if (v == s || v == en) {
			fail(path, "from and to are neighbours on the exact carrier: no point between them for the arc");
		}
		out["via"] = json::array({v[0], v[1]});
		return out;
	}
	out["via"] = json::array({cx + r * std::cos(a0 + span / 2), cy + r * std::sin(a0 + span / 2)});
	return out;
}

// The first line where two documents' canonical spellings differ (messages).
std::string first_difference(const json &a, const json &b) {
	const std::string x = canonical(a), y = canonical(b);
	size_t i = 0, line = 1;
	while (i < x.size() && i < y.size() && x[i] == y[i]) {
		line += x[i] == '\n';
		i++;
	}
	auto line_at = [](const std::string &s, size_t at) {
		const size_t from = s.rfind('\n', at == 0 ? 0 : at - 1);
		const size_t to = s.find('\n', at);
		return s.substr(from == std::string::npos ? 0 : from + 1, to == std::string::npos ? std::string::npos : to - (from == std::string::npos ? 0 : from + 1));
	};
	return "line " + std::to_string(line) + ": '" + line_at(x, i) + "' vs '" + line_at(y, i) + "'";
}

} // namespace

json off_carrier(const json &element, const json &doc) { return on_carrier(element, carriers_of(doc), "element"); }

namespace {
bool has_tag(const std::vector<std::string> &tags, const char *t) { return std::find(tags.begin(), tags.end(), t) != tags.end(); }
json cond(const std::string &key, bool on) { return {{"key", key}, {"values", on ? json::array({"true"}) : json::array({"DEFAULT", "false"})}}; }
json write(const std::string &key, bool value, json when) { return {{"kind", "state"}, {"key", key}, {"op", "set"}, {"value", value}, {"when", std::move(when)}}; }
} // namespace

json tag_hits(const std::string &name, const std::vector<std::string> &tags) {
	json out = json::array();
	if (has_tag(tags, "flammable") || has_tag(tags, "burning")) {
		const std::string mine = "el:" + name + ":fire";
		out.push_back(write(mine, true, json::array({cond("shot:fire", true)})));  // a burning arrow lights it
		out.push_back(write("shot:fire", true, json::array({cond(mine, true)}))); // it lights the arrow
	}
	if (has_tag(tags, "conductive") || has_tag(tags, "charged")) {
		const std::string mine = "el:" + name + ":charge";
		const json give = json::array({cond("shot:charge", true), cond(mine, false)});
		const json take = json::array({cond("shot:charge", false), cond(mine, true)});
		out.push_back(write("shot:charge", false, give)); // the arrow's charge to it
		out.push_back(write(mine, true, give));
		out.push_back(write("shot:charge", true, take)); // its charge to the arrow
		out.push_back(write(mine, false, take));
	}
	if (has_tag(tags, "frosted")) {
		out.push_back({{"kind", "state"}, {"key", "shot:frost"}, {"op", "set"}, {"value", true}});
	}
	return out;
}

std::vector<std::string> tag_initial_flags(const std::string &name, const std::vector<std::string> &tags) {
	std::vector<std::string> out;
	if (has_tag(tags, "burning")) {
		out.push_back("el:" + name + ":fire");
	}
	if (has_tag(tags, "charged")) {
		out.push_back("el:" + name + ":charge");
	}
	return out;
}

bool frost_bearing(const std::string &t) { return t == "pass" || t == "reflection" || t == "inversion"; }

std::vector<json> rect_walls(const json &rect, const json &side, const json &label, bool solid) {
	const double x = rect[0].get<double>(), y = rect[1].get<double>(), w = rect[2].get<double>(), h = rect[3].get<double>();
	const double pts[4][3][2] = {{{x, y}, {x + w, y}, {x + w / 2, y}}, {{x + w, y}, {x + w, y + h}, {x + w, y + h / 2}},
			{{x + w, y + h}, {x, y + h}, {x + w / 2, y + h}}, {{x, y + h}, {x, y}, {x, y + h / 2}}};
	std::vector<json> out;
	for (const auto &p : pts) {
		out.push_back({{"start", {p[0][0], p[0][1]}}, {"end", {p[1][0], p[1][1]}}, {"via", {p[2][0], p[2][1]}}, {"label", label}, {"left", side},
				{"right", side}, {"player_solid", solid}});
	}
	return out;
}

Lowered lower_level(const json &v2) {
	const std::string P = "level";
	if (!v2.is_object()) {
		fail(P, "an object expected");
	}
	for (auto it = v2.begin(); it != v2.end(); ++it) {
		const std::string &k = it.key();
		if (std::find(RESERVED.begin(), RESERVED.end(), k) != RESERVED.end()) {
			fail(P, "'" + k + "' is reserved for a later stage of rc-level/2");
		}
		const bool known = k == "schema" || k == "flags" || k == "templates" || k == "elements" || k == "objective" || k == "detectors"
				|| k == "objects" || k == "carriers" || k == "cycle" || k == "spread" || k == "edges" || std::find(META.begin(), META.end(), k) != META.end();
		if (!known) {
			fail(P, "unknown key '" + k + "'");
		}
	}
	if (v2.value("schema", json()) != "rc-level/2") {
		fail(P + ".schema", "rc-level/2 expected");
	}
	Lowered out;
	out.level = json::object();
	out.level["schema"] = "rc-level/1";
	for (const std::string &k : META) {
		if (v2.contains(k)) {
			out.level[k] = v2[k];
		}
	}
	if (v2.contains("flags")) {
		const json &f = v2["flags"];
		if (!f.is_object()) {
			fail(P + ".flags", "an object of flag values expected");
		}
		for (auto it = f.begin(); it != f.end(); ++it) {
			if (!it.value().is_boolean() && !it.value().is_number() && !it.value().is_string()) {
				fail(P + ".flags." + it.key(), "a bool, number or string expected");
			}
		}
		out.flags = f;
	}
	std::map<std::string, json> kinds = elements::builtin();
	if (v2.contains("templates")) {
		const json &t = v2["templates"];
		if (!t.is_object()) {
			fail(P + ".templates", "an object of element templates expected");
		}
		for (auto it = t.begin(); it != t.end(); ++it) {
			if (it.key() == "raw" || it.key() == "edges") {
				fail(P + ".templates." + it.key(), "a builtin kind of the format, not a template");
			}
			elements::check_template(it.value(), P + ".templates." + it.key());
			json tmpl = it.value();
			if (!tmpl.contains("params")) {
				tmpl["params"] = json::object();
			}
			kinds[it.key()] = std::move(tmpl);
		}
	}
	if (!v2.contains("elements") || !v2["elements"].is_array()) {
		fail(P + ".elements", "an array of elements expected");
	}
	const json &es = v2["elements"];
	// the names: the explicit ones, then kind_<n> for the others
	std::set<std::string> taken;
	for (size_t i = 0; i < es.size(); i++) {
		const std::string ep = P + ".elements[" + std::to_string(i) + "]";
		if (!es[i].is_object() || !es[i].contains("kind") || !es[i]["kind"].is_string()) {
			fail(ep, "an element {\"kind\": ..., ...} expected");
		}
		if (es[i].contains("name")) {
			if (!es[i]["name"].is_string() || es[i]["name"].get<std::string>().empty()) {
				fail(ep + ".name", "a non-empty string expected");
			}
			if (!taken.insert(es[i]["name"].get<std::string>()).second) {
				fail(ep + ".name", "'" + es[i]["name"].get<std::string>() + "' names another element");
			}
		}
	}
	std::map<std::string, int> counters;
	auto auto_name = [&](const std::string &kind) {
		std::string n;
		do {
			n = kind + "_" + std::to_string(++counters[kind]);
		} while (taken.count(n));
		taken.insert(n);
		return n;
	};
	const std::map<std::string, json> carriers = carriers_of(v2);
	std::vector<json> surfaces;
	auto push = [&](std::vector<json> ss, const std::string &origin) {
		for (size_t i = 0; i < ss.size(); i++) {
			out.origins.push_back(ss.size() > 1 ? origin + " surface " + std::to_string(i) : origin);
			surfaces.push_back(std::move(ss[i]));
		}
	};
	const json room = built_room(v2);
	if (!room.is_null()) {
		push(room_walls(room), P + ".room (its walls)");
	}
	// an element of a template kind: its surfaces, tags and region (`at`: where it stands, its origins' prefix)
	auto place = [&](const json &e, const std::string &kind, const std::string &name, size_t index, const std::string &at, const std::string &origin) {
		auto it = kinds.find(kind);
		if (it == kinds.end()) {
			fail(at + ".kind", "unknown element kind '" + kind + "'");
		}
		const json g = e.contains("carrier") ? on_carrier(e, carriers, origin) : e; // Stage 149: on a carrier
		std::vector<json> expanded = elements::expand(it->second, g, name, origin);
		// Stage 152: its tags
		std::vector<std::string> tags;
		for (const json *src : {static_cast<const json *>(&it->second), &e}) {
			if (src->contains("tags")) {
				if (!(*src)["tags"].is_array()) {
					fail(origin + ".tags", "an array of strings expected");
				}
				for (const json &tg : (*src)["tags"]) {
					if (!tg.is_string()) {
						fail(origin + ".tags", "an array of strings expected");
					}
					if (std::find(tags.begin(), tags.end(), tg.get<std::string>()) == tags.end()) {
						tags.push_back(tg.get<std::string>());
					}
				}
			}
		}
		if (!tags.empty()) {
			out.tagged.push_back({index, name, tags, at});
		}
		// Stage 156: what the tags give every side (a missing side: a pass that carries them), the flags they start
		const json extra = tag_hits(name, tags);
		if (!extra.empty()) {
			const bool frost = std::find(tags.begin(), tags.end(), "frosted") != tags.end();
			auto give = [&](json &sides) {
				for (const char *k : {"left", "right"}) {
					json &sd = sides[k];
					if (sd.is_null()) {
						sd = {{"effect", {{"type", "pass"}}}, {"interactive", false}};
					}
					const std::string t = sd.contains("effect") ? sd["effect"].value("type", "pass") : "pass";
					if (frost && !frost_bearing(t)) {
						fail(origin + ".tags", "frosted: its sides must pass, reflect or invert (a " + t + " side cannot bear frost)");
					}
					if (!sd.contains("hits")) {
						sd["hits"] = json::array();
					}
					for (const json &h : extra) {
						sd["hits"].push_back(h);
					}
				}
			};
			for (json &s : expanded) {
				if (s.value("pre_transform", false)) {
					continue;
				}
				give(s);
				if (s.contains("resolver") && s["resolver"].is_object() && s["resolver"].value("table", json()).is_array()) {
					for (json &row : s["resolver"]["table"]) {
						if (row.is_array() && row.size() == 2 && row[1].is_object()) {
							give(row[1]);
						}
					}
				}
			}
		}
		for (const std::string &k : tag_initial_flags(name, tags)) {
			if (!out.flags.contains(k)) {
				out.flags[k] = true;
			}
		}
		push(std::move(expanded), origin);
		// Stage 153: the region it bounds
		const json shade = elements::evaluate(it->second, g, name, "shade", origin);
		if (!shade.is_null()) {
			out.shaded.push_back({index, name, shade, at});
		}
	};
	// Stage 169: the edge walls -- passes (not plannable, not drawn) unless `edges` makes them elements of a
	// kind, their faces (left sides) inward, or "wrap": the opposite edges a portal pair (docs/LEVEL_FORMAT.md)
	auto place_edges = [&](size_t index, const std::string &origin) {
		if (!v2.contains("edges") || v2["edges"].is_null()) {
			push(edge_walls(v2), origin);
			return;
		}
		const std::string EP = P + ".edges";
		static const char *NAMES[4] = {"top", "right", "bottom", "left"};
		json per[4];
		const json &cfg = v2["edges"];
		if (cfg.is_string() || (cfg.is_object() && cfg.contains("kind"))) {
			for (json &e : per) {
				e = cfg;
			}
		} else if (cfg.is_object()) {
			for (auto it = cfg.begin(); it != cfg.end(); ++it) {
				const auto at = std::find_if(std::begin(NAMES), std::end(NAMES), [&](const char *n) { return it.key() == n; });
				if (at == std::end(NAMES)) {
					fail(EP, "unknown key '" + it.key() + "' (top, right, bottom, left; or a kind for all four)");
				}
				per[at - std::begin(NAMES)] = it.value();
			}
		} else {
			fail(EP, "a kind, {\"kind\", ...params}, \"wrap\", or {top, right, bottom, left} of those expected");
		}
		auto wraps = [&](int k) { return per[k] == "wrap"; };
		for (int k = 0; k < 4; k++) {
			if (wraps(k) != wraps((k + 2) % 4)) {
				fail(EP + "." + NAMES[k], "wrap joins opposite edges: both wrap or neither");
			}
		}
		const std::vector<json> pass = edge_walls(v2);
		const json b = bounds_of(v2);
		const double w = b[2].get<double>(), h = b[3].get<double>();
		for (int k = 0; k < 4; k++) {
			const std::string ek = EP + "." + NAMES[k];
			const json &c = per[k];
			const json &wall = pass[size_t(k)];
			if (c.is_null() || c == "pass") {
				push({wall}, ek);
				continue;
			}
			if (c == "wrap") {
				if (k == 1 || k == 2) { // the right and bottom edges, their images the left and top
					json e = {{"kind", "portal"}, {"start", wall["start"]}, {"end", wall["end"]}, {"via", wall["via"]},
							{"d", k == 1 ? json::array({-w, 0.0}) : json::array({0.0, -h})}};
					const std::string name = std::string("edge_wrap_") + (k == 1 ? "x" : "y");
					place(e, "portal", name, index, ek, ek + " (" + name + ")");
				}
				continue;
			}
			json e = c.is_string() ? json{{"kind", c}} : c;
			if (!e.is_object() || !e.contains("kind") || !e["kind"].is_string()) {
				fail(ek, "a kind, {\"kind\", ...params}, \"pass\" or \"wrap\" expected");
			}
			for (const char *g : {"start", "end", "via", "circle", "carrier", "from", "to", "name", "motion"}) {
				if (e.contains(g)) {
					fail(ek + "." + g, "an edge's geometry and name follow the bounds");
				}
			}
			e["start"] = wall["end"]; // (reversed: the left side, a face, inward)
			e["end"] = wall["start"];
			e["via"] = wall["via"];
			const std::string name = std::string("edge_") + NAMES[k];
			if (taken.count(name)) {
				fail(ek, "'" + name + "' names an element (the edge's name)");
			}
			place(e, e["kind"].get<std::string>(), name, index, ek, ek + " (" + name + ")");
		}
	};
	bool edges = false;
	for (size_t i = 0; i < es.size(); i++) {
		const json &e = es[i];
		const std::string kind = e["kind"].get<std::string>();
		const std::string name = e.contains("name") ? e["name"].get<std::string>() : auto_name(kind);
		const std::string ep = P + ".elements[" + std::to_string(i) + "]";
		const std::string origin = ep + " (" + name + ")";
		if (kind == "edges") {
			for (auto it = e.begin(); it != e.end(); ++it) {
				if (it.key() != "kind" && it.key() != "name") {
					fail(origin, "unknown key '" + it.key() + "' (the edge walls follow the bounds)");
				}
			}
			if (edges) {
				fail(origin, "the edge walls are placed once");
			}
			edges = true;
			place_edges(i, origin);
		} else if (kind == "raw") {
			for (auto it = e.begin(); it != e.end(); ++it) {
				if (it.key() != "kind" && it.key() != "name" && it.key() != "surface") {
					fail(origin, "unknown key '" + it.key() + "' (a raw element is {kind, name, surface})");
				}
			}
			if (!e.contains("surface") || !e["surface"].is_object()) {
				fail(origin + ".surface", "an rc-level/1 surface (without ids) expected");
			}
			if (e["surface"].contains("ids")) {
				fail(origin + ".surface.ids", "ids are derived from the build order");
			}
			push({e["surface"]}, origin + ".surface");
		} else {
			place(e, kind, name, i, ep, origin);
		}
	}
	if (!edges) {
		place_edges(es.size(), P + ".bounds (the edge walls)");
	}
	// the pre-transform circles last, in order
	std::vector<size_t> order(surfaces.size());
	for (size_t i = 0; i < order.size(); i++) {
		order[i] = i;
	}
	std::stable_partition(order.begin(), order.end(), [&](size_t i) { return !surfaces[i].value("pre_transform", false); });
	json list = json::array();
	std::vector<std::string> origins;
	int next = 1;
	for (const size_t i : order) {
		json s = surfaces[i];
		json ids = json::array({next});
		if (is_portal(s)) {
			ids.push_back(next + 1);
		}
		next += int(ids.size());
		s["ids"] = ids;
		list.push_back(std::move(s));
		origins.push_back(out.origins[i]);
	}
	out.origins = std::move(origins);
	out.level["surfaces"] = std::move(list);
	return out;
}

json migrate_level(const json &v1) {
	if (!v1.is_object() || v1.value("schema", json()) != "rc-level/1" || !v1.contains("surfaces") || !v1["surfaces"].is_array()) {
		throw LevelError("migrate: an rc-level/1 document expected");
	}
	json out = json::object();
	out["schema"] = "rc-level/2";
	for (const std::string &k : META) {
		if (v1.contains(k)) {
			out[k] = v1[k];
		}
	}
	std::vector<json> rest;
	int next = 1;
	for (const json &s : v1["surfaces"]) {
		const json ids = s.value("ids", json());
		if (!ids.is_array() || ids.empty() || ids[0] != next) {
			throw LevelError("migrate: the ids are not consecutive in build order (surface " + std::to_string(rest.size()) + ")");
		}
		next += int(ids.size());
		json t = s;
		t.erase("ids");
		rest.push_back(std::move(t));
	}
	const json room = built_room(v1);
	if (!room.is_null()) {
		const std::vector<json> walls = room_walls(room);
		for (size_t i = 0; i < 4; i++) {
			if (i >= rest.size() || text(rest[i]) != text(walls[i])) {
				throw LevelError("migrate: the room is built, but its first four surfaces are not its walls");
			}
		}
		rest.erase(rest.begin(), rest.begin() + 4);
	}
	const std::vector<json> edges = edge_walls(v1);
	std::optional<size_t> edges_at;
	for (size_t i = 0; i + 4 <= rest.size(); i++) {
		bool same = true;
		for (size_t k = 0; k < 4 && same; k++) {
			same = text(rest[i + k]) == text(edges[k]);
		}
		if (same) {
			if (edges_at) {
				throw LevelError("migrate: the edge walls are there twice");
			}
			edges_at = i;
		}
	}
	std::vector<std::pair<std::string, const json *>> kinds;
	for (const auto &[kind, tmpl] : elements::builtin()) {
		kinds.emplace_back(kind, &tmpl);
	}
	std::stable_sort(kinds.begin(), kinds.end(), [](const auto &a, const auto &b) {
		return a.second->value("priority", 100.0) < b.second->value("priority", 100.0);
	});
	// Stage 169: configured edges -- per edge (top, right, bottom, left) its pass wall, an element of a kind on
	// the reversed wall (its face inward), or a wrap's portal (the right and bottom edges; their images the left
	// and top): the `edges` value and how many surfaces it takes
	size_t edges_take = 4;
	auto configured = [&](size_t at) -> std::optional<std::pair<json, size_t>> {
		const json b = bounds_of(v1);
		const double w = b[2].get<double>(), h = b[3].get<double>();
		json per[4];
		size_t j = at;
		for (int k = 0; k < 4; k++) {
			const json &wall = edges[size_t(k)];
			if (j < rest.size() && text(rest[j]) == text(wall)) {
				per[k] = "pass";
				j++;
				continue;
			}
			if (k == 1 || k == 2) {
				const json portal = {{"kind", "portal"}, {"start", wall["start"]}, {"end", wall["end"]}, {"via", wall["via"]},
						{"d", k == 1 ? json::array({-w, 0.0}) : json::array({0.0, -h})}};
				const std::vector<json> want = elements::expand(elements::builtin().at("portal"), portal, "edge", "migrate");
				if (j < rest.size() && want.size() == 1 && text(rest[j]) == text(want[0])) {
					per[k] = "wrap";
					j++;
					continue;
				}
			}
			for (const auto &[kind, tmpl] : kinds) {
				std::set<std::string> scratch;
				const auto e = j < rest.size() ? elements::match(kind, *tmpl, rest, j, "edge", scratch) : std::nullopt;
				if (!e || (*e).value("start", json()) != wall["end"] || (*e).value("end", json()) != wall["start"]) {
					continue;
				}
				json spec = *e;
				for (const char *g : {"start", "end", "via", "name"}) {
					spec.erase(g);
				}
				per[k] = spec.size() == 1 ? spec["kind"] : spec;
				j += (*tmpl)["surfaces"].size();
				break;
			}
			if (per[k].is_null() && (k == 0 || (k == 3 && per[1] == "wrap"))) {
				per[k] = "wrap"; // (an image edge: nothing of its own; checked against its opposite below)
			}
			if (per[k].is_null()) {
				return std::nullopt;
			}
		}
		for (int k = 0; k < 4; k++) {
			if ((per[k] == "wrap") != (per[(k + 2) % 4] == "wrap")) {
				return std::nullopt;
			}
		}
		json cfg;
		if (per[0] == per[1] && per[1] == per[2] && per[2] == per[3]) {
			cfg = per[0];
		} else {
			static const char *NAMES[4] = {"top", "right", "bottom", "left"};
			cfg = json::object();
			for (int k = 0; k < 4; k++) {
				if (per[k] != "pass") {
					cfg[NAMES[k]] = per[k];
				}
			}
		}
		return std::make_pair(cfg, j - at);
	};
	if (!edges_at) {
		for (size_t i = rest.size(); i-- > 0;) { // (the last place they fit)
			if (auto c = configured(i)) {
				out["edges"] = c->first;
				edges_take = c->second;
				edges_at = i;
				break;
			}
		}
	}
	if (!edges_at) {
		throw LevelError("migrate: the four edge walls of the bounds are not there");
	}
	bool edges_implicit = true;
	for (size_t i = *edges_at + edges_take; i < rest.size(); i++) {
		edges_implicit = edges_implicit && rest[i].value("pre_transform", false);
	}
	std::set<std::string> taken;
	std::map<std::string, int> counters;
	auto fallback = [&](const std::string &kind) {
		int n = counters[kind];
		std::string name;
		do {
			name = kind + "_" + std::to_string(++n);
		} while (taken.count(name));
		return name;
	};
	auto use = [&](const std::string &kind, const std::string &name) {
		taken.insert(name);
		counters[kind]++;
	};
	json elems = json::array();
	for (size_t i = 0; i < rest.size();) {
		if (i == *edges_at) {
			if (!edges_implicit) {
				elems.push_back({{"kind", "edges"}});
			}
			i += edges_take;
			continue;
		}
		bool matched = false;
		for (const auto &[kind, tmpl] : kinds) {
			if (i < *edges_at && i + (*tmpl)["surfaces"].size() > *edges_at) {
				continue; // a run never takes in the edge walls
			}
			const std::string name = fallback(kind);
			if (auto e = elements::match(kind, *tmpl, rest, i, name, taken)) {
				use(kind, (*e)["name"].get<std::string>());
				elems.push_back(*e);
				i += (*tmpl)["surfaces"].size();
				matched = true;
				break;
			}
		}
		if (!matched) {
			const std::string name = fallback("raw");
			use("raw", name);
			elems.push_back({{"kind", "raw"}, {"name", name}, {"surface", rest[i]}});
			i++;
		}
	}
	out["elements"] = elems;
	const json lowered = lower_level(out).level;
	if (text(lowered) != text(v1)) {
		throw LevelError("migrate: the migrated level does not lower to the original (" + first_difference(lowered, v1) + ")");
	}
	return out;
}

} // namespace rcg
