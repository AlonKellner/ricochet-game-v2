// rc-level/2's element templates (rcg/elements.hpp, docs/ELEMENTS.md).
#include "rcg/elements.hpp"

#include "rcg/canon.hpp"
#include "rcg/level.hpp"

#include <algorithm>
#include <functional>

namespace rcg::elements {

// The builtin templates, data/elements/*.json, embedded at build time (native/cmake/embed_elements.cmake).
namespace embedded {
struct File {
	const char *kind;
	const char *text;
};
extern const File FILES[];
extern const unsigned COUNT;
} // namespace embedded

namespace {

[[noreturn]] void fail(const std::string &path, const std::string &what) { throw LevelError(path + ": " + what); }

const std::set<std::string> COMMON = {"kind", "name", "start", "end", "via", "circle", "straight", "label", "flip"};
const std::set<std::string> GEOMETRY = {"start", "end", "via", "circle", "straight"};
const std::set<std::string> TYPES = {"string", "number", "bool", "point", "array", "object", "any"};

bool ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool ident_char(char c) { return ident_start(c) || (c >= '0' && c <= '9'); }

// "$p" exactly: p
std::optional<std::string> whole_ref(const std::string &s) {
	if (s.size() < 2 || s[0] != '$' || !ident_start(s[1])) {
		return std::nullopt;
	}
	for (size_t i = 2; i < s.size(); i++) {
		if (!ident_char(s[i])) {
			return std::nullopt;
		}
	}
	return s.substr(1);
}

// A string split at its "${p}" references.
struct Piece {
	bool param = false;
	std::string text;
};

std::vector<Piece> pieces(const std::string &s) {
	std::vector<Piece> out;
	std::string lit;
	for (size_t i = 0; i < s.size();) {
		if (s.compare(i, 2, "${") == 0) {
			size_t j = i + 2;
			while (j < s.size() && ident_char(s[j])) {
				j++;
			}
			if (j > i + 2 && j < s.size() && s[j] == '}' && ident_start(s[i + 2])) {
				if (!lit.empty()) {
					out.push_back({false, lit});
					lit.clear();
				}
				out.push_back({true, s.substr(i + 2, j - i - 2)});
				i = j + 1;
				continue;
			}
		}
		lit += s[i++];
	}
	if (!lit.empty()) {
		out.push_back({false, lit});
	}
	return out;
}

bool interpolated(const std::vector<Piece> &ps) {
	return std::any_of(ps.begin(), ps.end(), [](const Piece &p) { return p.param; });
}

// A value's spelling as a $switch case name (and in messages).
std::string spelling(const json &v) {
	if (v.is_string()) {
		return v.get<std::string>();
	}
	if (v.is_number_float()) {
		return float_repr(v.get<double>());
	}
	return v.dump();
}

std::string param_type(const json &spec) {
	if (spec.contains("type")) {
		return spec["type"].get<std::string>();
	}
	const json d = spec.value("default", json());
	if (d.is_boolean()) {
		return "bool";
	}
	if (d.is_number()) {
		return "number";
	}
	if (d.is_string()) {
		return "string";
	}
	if (d.is_array()) {
		return d.size() == 2 && d[0].is_number() && d[1].is_number() ? "point" : "array";
	}
	if (d.is_object()) {
		return "object";
	}
	return "any";
}

// Why a value does not fit a parameter ("" when it does).
std::string misfit(const json &spec, const json &v) {
	if (v.is_null() && spec.contains("default") && spec["default"].is_null()) {
		return ""; // an optional parameter left unset
	}
	if (spec.contains("enum")) {
		for (const json &e : spec["enum"]) {
			if (e == v) {
				return "";
			}
		}
		std::string opts;
		for (const json &e : spec["enum"]) {
			opts += (opts.empty() ? "" : ", ") + spelling(e);
		}
		return "one of " + opts + " expected";
	}
	const std::string t = param_type(spec);
	const bool ok = t == "any" || (t == "string" && v.is_string()) || (t == "number" && v.is_number()) || (t == "bool" && v.is_boolean())
			|| (t == "point" && v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number()) || (t == "array" && v.is_array())
			|| (t == "object" && v.is_object());
	return ok ? "" : "a" + std::string(t == "array" || t == "object" ? "n " : " ") + t + " expected";
}

json eval(const json &node, const json &env, const std::string &path) {
	if (node.is_string()) {
		const std::string s = node.get<std::string>();
		if (const auto p = whole_ref(s)) {
			if (!env.contains(*p)) {
				fail(path, "unknown parameter '" + *p + "'");
			}
			return env[*p];
		}
		const std::vector<Piece> ps = pieces(s);
		if (!interpolated(ps)) {
			return node;
		}
		std::string out;
		for (const Piece &p : ps) {
			if (!p.param) {
				out += p.text;
				continue;
			}
			if (!env.contains(p.text)) {
				fail(path, "unknown parameter '" + p.text + "'");
			}
			const json &v = env[p.text];
			if (v.is_null()) {
				fail(path, "parameter '" + p.text + "' is unset");
			}
			out += spelling(v);
		}
		return out;
	}
	if (node.is_object()) {
		if (node.contains("$switch")) {
			const std::string p = node["$switch"].get<std::string>();
			if (!env.contains(p)) {
				fail(path, "unknown parameter '" + p + "'");
			}
			const std::string key = spelling(env[p]);
			if (!node["cases"].contains(key)) {
				fail(path, "no case '" + key + "' for parameter '" + p + "'");
			}
			return eval(node["cases"][key], env, path);
		}
		json out = json::object();
		for (auto it = node.begin(); it != node.end(); ++it) {
			out[it.key()] = eval(it.value(), env, path);
		}
		return out;
	}
	if (node.is_array()) {
		json out = json::array();
		for (const json &v : node) {
			out.push_back(eval(v, env, path));
		}
		return out;
	}
	return node;
}

// The parameters a template expression refers to (the checks).
void refs(const json &node, std::set<std::string> &out) {
	if (node.is_string()) {
		const std::string s = node.get<std::string>();
		if (const auto p = whole_ref(s)) {
			out.insert(*p);
		}
		for (const Piece &p : pieces(s)) {
			if (p.param) {
				out.insert(p.text);
			}
		}
	} else if (node.is_object()) {
		if (node.contains("$switch") && node["$switch"].is_string()) {
			out.insert(node["$switch"].get<std::string>());
		}
		for (auto it = node.begin(); it != node.end(); ++it) {
			refs(it.value(), out);
		}
	} else if (node.is_array()) {
		for (const json &v : node) {
			refs(v, out);
		}
	}
}

void check_switches(const json &node, const std::string &path) {
	if (node.is_object()) {
		if (node.contains("$switch")) {
			for (auto it = node.begin(); it != node.end(); ++it) {
				if (it.key() != "$switch" && it.key() != "cases") {
					fail(path, "a $switch has only '$switch' and 'cases'");
				}
			}
			if (!node["$switch"].is_string() || !node.contains("cases") || !node["cases"].is_object()) {
				fail(path, "{\"$switch\": <parameter>, \"cases\": {...}} expected");
			}
		}
		for (auto it = node.begin(); it != node.end(); ++it) {
			check_switches(it.value(), path + "." + it.key());
		}
	} else if (node.is_array()) {
		for (size_t i = 0; i < node.size(); i++) {
			check_switches(node[i], path + "[" + std::to_string(i) + "]");
		}
	}
}

void swap_sides(json &s) {
	if (s.contains("left") || s.contains("right")) {
		json l = s.value("left", json()), r = s.value("right", json());
		s["left"] = r;
		s["right"] = l;
	}
	if (s.contains("resolver") && s["resolver"].is_object() && s["resolver"].contains("table")) {
		for (json &row : s["resolver"]["table"]) {
			if (row.is_array() && row.size() == 2 && row[1].is_object()) {
				json l = row[1].value("left", json()), r = row[1].value("right", json());
				row[1]["left"] = r;
				row[1]["right"] = l;
			}
		}
	}
}

// ---------------------------------------------------------------- matching

using Env = std::map<std::string, json>;

bool fits(const json &tmpl, const std::string &p, const json &v) {
	if (p == "name") {
		return v.is_string() && !v.get<std::string>().empty();
	}
	const json &params = tmpl["params"];
	return params.contains(p) && misfit(params[p], v).empty();
}

bool unify(const json &tmpl, const json &pat, const json &val, Env &env);

bool unify_pieces(const json &tmpl, const std::vector<Piece> &ps, size_t pi, const std::string &s, size_t si, Env &env) {
	if (pi == ps.size()) {
		return si == s.size();
	}
	const Piece &p = ps[pi];
	if (!p.param) {
		return s.compare(si, p.text.size(), p.text) == 0 && unify_pieces(tmpl, ps, pi + 1, s, si + p.text.size(), env);
	}
	if (auto it = env.find(p.text); it != env.end()) {
		if (!it->second.is_string()) {
			return false;
		}
		const std::string b = it->second.get<std::string>();
		return s.compare(si, b.size(), b) == 0 && unify_pieces(tmpl, ps, pi + 1, s, si + b.size(), env);
	}
	for (size_t len = 1; si + len <= s.size(); len++) {
		const json v = s.substr(si, len);
		if (!fits(tmpl, p.text, v)) {
			continue;
		}
		env[p.text] = v;
		if (unify_pieces(tmpl, ps, pi + 1, s, si + len, env)) {
			return true;
		}
		env.erase(p.text);
	}
	return false;
}

// The parameter value a $switch case names.
std::optional<json> case_value(const json &tmpl, const std::string &p, const std::string &key) {
	const json &params = tmpl["params"];
	if (!params.contains(p)) {
		return std::nullopt;
	}
	const json &spec = params[p];
	if (spec.contains("enum")) {
		for (const json &e : spec["enum"]) {
			if (spelling(e) == key) {
				return e;
			}
		}
		return std::nullopt;
	}
	if (param_type(spec) == "bool") {
		if (key == "true" || key == "false") {
			return json(key == "true");
		}
		return std::nullopt;
	}
	return json(key);
}

bool unify(const json &tmpl, const json &pat, const json &val, Env &env) {
	if (pat.is_string()) {
		const std::string s = pat.get<std::string>();
		if (const auto p = whole_ref(s)) {
			if (auto it = env.find(*p); it != env.end()) {
				return canonical(it->second) == canonical(val);
			}
			if (!fits(tmpl, *p, val)) {
				return false;
			}
			env[*p] = val;
			return true;
		}
		const std::vector<Piece> ps = pieces(s);
		if (interpolated(ps)) {
			return val.is_string() && unify_pieces(tmpl, ps, 0, val.get<std::string>(), 0, env);
		}
		return val.is_string() && val.get<std::string>() == s;
	}
	if (pat.is_object()) {
		if (pat.contains("$switch")) {
			const std::string p = pat["$switch"].get<std::string>();
			const json &cases = pat["cases"];
			if (auto it = env.find(p); it != env.end()) {
				const std::string key = spelling(it->second);
				return cases.contains(key) && unify(tmpl, cases[key], val, env);
			}
			std::vector<std::string> keys;
			const json &spec = tmpl["params"].value(p, json::object());
			if (spec.contains("default") && cases.contains(spelling(spec["default"]))) {
				keys.push_back(spelling(spec["default"])); // the default first: fewer parameters written
			}
			for (auto it = cases.begin(); it != cases.end(); ++it) {
				if (std::find(keys.begin(), keys.end(), it.key()) == keys.end()) {
					keys.push_back(it.key());
				}
			}
			for (const std::string &key : keys) {
				const auto v = case_value(tmpl, p, key);
				if (!v) {
					continue;
				}
				Env saved = env;
				env[p] = *v;
				if (unify(tmpl, cases[key], val, env)) {
					return true;
				}
				env = std::move(saved);
			}
			return false;
		}
		if (!val.is_object() || val.size() != pat.size()) {
			return false;
		}
		for (auto it = pat.begin(); it != pat.end(); ++it) {
			if (!val.contains(it.key()) || !unify(tmpl, it.value(), val[it.key()], env)) {
				return false;
			}
		}
		return true;
	}
	if (pat.is_array()) {
		if (!val.is_array() || val.size() != pat.size()) {
			return false;
		}
		for (size_t i = 0; i < pat.size(); i++) {
			if (!unify(tmpl, pat[i], val[i], env)) {
				return false;
			}
		}
		return true;
	}
	return canonical(pat) == canonical(val);
}

json without(const json &j, const std::set<std::string> &keys) {
	json out = j;
	for (const std::string &k : keys) {
		out.erase(k);
	}
	return out;
}

} // namespace

const std::set<std::string> &common_keys() { return COMMON; }

void check_template(const json &t, const std::string &path) {
	if (!t.is_object()) {
		fail(path, "a template object expected");
	}
	for (auto it = t.begin(); it != t.end(); ++it) {
		static const std::set<std::string> keys = {"doc", "label", "priority", "geometry", "params", "surfaces", "example"};
		if (!keys.count(it.key())) {
			fail(path, "unknown key '" + it.key() + "'");
		}
	}
	if (t.contains("priority") && !t["priority"].is_number()) {
		fail(path + ".priority", "a number expected");
	}
	if (t.contains("doc") && !t["doc"].is_string()) {
		fail(path + ".doc", "a string expected");
	}
	if (t.contains("geometry") && !(t["geometry"] == "any" || t["geometry"] == "segment" || t["geometry"] == "circle")) {
		fail(path + ".geometry", "any, segment or circle expected");
	}
	const json params = t.value("params", json::object());
	if (!params.is_object()) {
		fail(path + ".params", "an object expected");
	}
	for (auto it = params.begin(); it != params.end(); ++it) {
		const std::string pp = path + ".params." + it.key();
		const json &spec = it.value();
		if (COMMON.count(it.key()) || !ident_start(it.key().empty() ? '0' : it.key()[0])) {
			fail(pp, "not a parameter name (reserved or not an identifier)");
		}
		if (!spec.is_object()) {
			fail(pp, "an object expected");
		}
		for (auto k = spec.begin(); k != spec.end(); ++k) {
			if (k.key() != "default" && k.key() != "doc" && k.key() != "enum" && k.key() != "type") {
				fail(pp, "unknown key '" + k.key() + "'");
			}
		}
		if (spec.contains("type") && (!spec["type"].is_string() || !TYPES.count(spec["type"].get<std::string>()))) {
			fail(pp + ".type", "string, number, bool, point, array, object or any expected");
		}
		if (spec.contains("enum") && (!spec["enum"].is_array() || spec["enum"].empty())) {
			fail(pp + ".enum", "a non-empty array expected");
		}
		const json d = spec.value("default", json());
		const bool interp = d.is_string() && (whole_ref(d.get<std::string>()) || interpolated(pieces(d.get<std::string>())));
		if (spec.contains("default") && !interp) {
			const std::string why = misfit(spec, spec["default"]);
			if (!why.empty()) {
				fail(pp + ".default", why);
			}
		}
	}
	if (t.contains("example")) { // the parameters a new element starts with (the editor's palette)
		if (!t["example"].is_object()) {
			fail(path + ".example", "an object of parameter values expected");
		}
		for (auto it = t["example"].begin(); it != t["example"].end(); ++it) {
			if (!params.contains(it.key())) {
				fail(path + ".example", "'" + it.key() + "' is not a parameter");
			}
			const std::string why = misfit(params[it.key()], it.value());
			if (!why.empty()) {
				fail(path + ".example." + it.key(), why);
			}
		}
	}
	const json &ss = t.contains("surfaces") ? t["surfaces"] : json();
	if (!ss.is_array() || ss.empty()) {
		fail(path + ".surfaces", "a non-empty array of surfaces expected");
	}
	for (size_t i = 0; i < ss.size(); i++) {
		const std::string sp = path + ".surfaces[" + std::to_string(i) + "]";
		if (!ss[i].is_object()) {
			fail(sp, "an object expected");
		}
		for (const std::string &g : GEOMETRY) {
			if (ss[i].contains(g)) {
				fail(sp, "'" + g + "': the geometry is the element's");
			}
		}
		if (ss[i].contains("ids")) {
			fail(sp, "'ids': ids are derived from the build order");
		}
		check_switches(ss[i], sp);
	}
	std::set<std::string> used;
	refs(ss, used);
	if (t.contains("label")) {
		check_switches(t["label"], path + ".label");
		refs(t["label"], used);
	}
	for (auto it = params.begin(); it != params.end(); ++it) {
		if (it.value().contains("default")) {
			refs(it.value()["default"], used);
		}
	}
	for (const std::string &p : used) {
		if (p != "name" && !params.contains(p)) {
			fail(path, "refers to an undeclared parameter '" + p + "'");
		}
	}
}

const std::map<std::string, json> &builtin() {
	static const std::map<std::string, json> kinds = [] {
		std::map<std::string, json> out;
		for (unsigned i = 0; i < embedded::COUNT; i++) {
			const std::string kind = embedded::FILES[i].kind;
			json t = json::parse(embedded::FILES[i].text);
			check_template(t, "data/elements/" + kind + ".json");
			if (!t.contains("params")) {
				t["params"] = json::object();
			}
			out[kind] = std::move(t);
		}
		return out;
	}();
	return kinds;
}

json params_of(const json &tmpl, const json &element, const std::string &name, const std::string &path) {
	const json params = tmpl.value("params", json::object());
	for (auto it = element.begin(); it != element.end(); ++it) {
		if (!COMMON.count(it.key()) && !params.contains(it.key())) {
			std::string known;
			for (auto p = params.begin(); p != params.end(); ++p) {
				known += (known.empty() ? "" : ", ") + p.key();
			}
			fail(path, "unknown key '" + it.key() + "' (parameters: " + (known.empty() ? "none" : known) + ")");
		}
	}
	json env = json::object();
	env["name"] = name;
	for (auto it = params.begin(); it != params.end(); ++it) {
		if (element.contains(it.key())) {
			const std::string why = misfit(it.value(), element[it.key()]);
			if (!why.empty()) {
				fail(path + "." + it.key(), why);
			}
			env[it.key()] = element[it.key()];
		} else if (!it.value().contains("default")) {
			fail(path, "'" + it.key() + "' required");
		}
	}
	// the defaults: plain ones first, then the ones that interpolate other parameters
	for (int pass = 0; pass < 2; pass++) {
		for (auto it = params.begin(); it != params.end(); ++it) {
			if (element.contains(it.key()) || env.contains(it.key())) {
				continue;
			}
			const json &d = it.value()["default"];
			const bool interp = d.is_string() && (whole_ref(d.get<std::string>()) || interpolated(pieces(d.get<std::string>())));
			if (interp != (pass == 1)) {
				continue;
			}
			if (!interp) {
				env[it.key()] = d;
				continue;
			}
			try {
				env[it.key()] = eval(d, env, path + "." + it.key());
			} catch (const LevelError &) {
				fail(path, "'" + it.key() + "' required (its default " + d.dump() + " needs a parameter that is unset)");
			}
		}
	}
	return env;
}

std::vector<json> expand(const json &tmpl, const json &element, const std::string &name, const std::string &path) {
	const json env = params_of(tmpl, element, name, path);
	const bool points = element.contains("start") || element.contains("end") || element.contains("via");
	const bool circle = element.contains("circle");
	if (points == circle) {
		fail(path, points ? "start/end/via or circle, not both" : "a geometry expected: start and end (via: the arc's middle point), or circle");
	}
	if (points && (!element.contains("start") || !element.contains("end"))) {
		fail(path, "start and end expected");
	}
	if (circle && element.contains("straight")) {
		fail(path + ".straight", "a circle is not straight");
	}
	bool flip = false;
	if (element.contains("flip")) {
		if (!element["flip"].is_boolean()) {
			fail(path + ".flip", "true or false expected");
		}
		flip = element["flip"].get<bool>();
	}
	json label;
	if (element.contains("label")) {
		if (!element["label"].is_string() && !element["label"].is_null()) {
			fail(path + ".label", "a string or null expected");
		}
		label = element["label"];
	} else if (tmpl.contains("label")) {
		label = eval(tmpl["label"], env, path + ".label");
	}
	std::vector<json> out;
	const json &ts = tmpl["surfaces"];
	for (size_t i = 0; i < ts.size(); i++) {
		json s = eval(ts[i], env, path);
		if (element.contains("label") || !s.contains("label")) {
			s["label"] = label;
		}
		for (const std::string &g : GEOMETRY) {
			if (element.contains(g)) {
				s[g] = element[g];
			}
		}
		if (points && !element.contains("via")) {
			const json &a = element["start"], &b = element["end"];
			if (a.is_array() && b.is_array() && a.size() == 2 && b.size() == 2 && a[0].is_number() && a[1].is_number() && b[0].is_number()
					&& b[1].is_number()) {
				s["via"] = json::array({(a[0].get<double>() + b[0].get<double>()) / 2, (a[1].get<double>() + b[1].get<double>()) / 2});
			}
		}
		if (flip) {
			swap_sides(s);
		}
		out.push_back(std::move(s));
	}
	return out;
}

std::optional<json> match(const std::string &kind, const json &tmpl, const std::vector<json> &surfaces, size_t at,
		const std::string &fallback_name, const std::set<std::string> &taken) {
	const json &ts = tmpl["surfaces"];
	const size_t n = ts.size();
	if (at + n > surfaces.size()) {
		return std::nullopt;
	}
	json geometry = json::object();
	for (const std::string &g : GEOMETRY) {
		if (surfaces[at].contains(g)) {
			geometry[g] = surfaces[at][g];
		}
	}
	std::vector<json> want; // the run as an element expands: no ids
	for (size_t i = 0; i < n; i++) {
		json s = without(surfaces[at + i], {"ids"});
		for (const std::string &g : GEOMETRY) {
			if (canonical(s.value(g, json())) != canonical(geometry.value(g, json()))) {
				return std::nullopt; // an element's surfaces share one geometry
			}
		}
		want.push_back(std::move(s));
	}
	for (const bool flip : {false, true}) {
		Env env;
		bool ok = true;
		for (size_t i = 0; i < n && ok; i++) {
			json val = without(want[i], GEOMETRY);
			val.erase("label");
			if (flip) {
				swap_sides(val);
			}
			ok = unify(tmpl, without(ts[i], {"label"}), val, env);
		}
		if (!ok) {
			continue;
		}
		std::string name = fallback_name;
		if (auto it = env.find("name"); it != env.end()) {
			name = it->second.get<std::string>();
			if (taken.count(name)) {
				continue;
			}
			env.erase(it);
		}
		json element = geometry;
		element["kind"] = kind;
		element["name"] = name;
		if (flip) {
			element["flip"] = true;
		}
		for (const auto &[p, v] : env) {
			element[p] = v;
		}
		auto expands_to = [&](const json &e) -> std::optional<std::vector<json>> {
			try {
				return expand(tmpl, e, name, "");
			} catch (const LevelError &) {
				return std::nullopt;
			}
		};
		std::optional<std::vector<json>> got = expands_to(element);
		if (!got) {
			continue;
		}
		// the label: the template's, else the run's own (one for every surface)
		bool labels_match = true;
		for (size_t i = 0; i < n; i++) {
			labels_match = labels_match && canonical((*got)[i].value("label", json())) == canonical(want[i].value("label", json()));
		}
		if (!labels_match) {
			element["label"] = want[0].value("label", json());
			got = expands_to(element);
		}
		auto same = [&](const std::optional<std::vector<json>> &e) {
			if (!e || e->size() != n) {
				return false;
			}
			for (size_t i = 0; i < n; i++) {
				if (canonical((*e)[i]) != canonical(want[i])) {
					return false;
				}
			}
			return true;
		};
		if (!same(got)) {
			continue;
		}
		// the parameters left at their defaults are not written
		for (auto it = tmpl["params"].begin(); it != tmpl["params"].end(); ++it) {
			if (!element.contains(it.key())) {
				continue;
			}
			json trial = element;
			trial.erase(it.key());
			if (same(expands_to(trial))) {
				element = std::move(trial);
			}
		}
		return element;
	}
	return std::nullopt;
}

} // namespace rcg::elements
