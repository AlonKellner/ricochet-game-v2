// The level reader (docs/LEVEL_FORMAT.md): rc-level/1, and rc-level/2 through
// its lowering (rcg/level2.hpp). Strict: an unknown key, a missing required key
// or a value of the wrong type is a LevelError naming its path.
#include "rcg/level.hpp"

#include "rcg/level2.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <initializer_list>
#include <sstream>

namespace rcg {
namespace {

using json = nlohmann::json;

[[noreturn]] void fail(const std::string &path, const std::string &what) { throw LevelError(path + ": " + what); }

void only_keys(const json &j, const std::string &path, std::initializer_list<const char *> keys) {
	if (!j.is_object()) {
		fail(path, "an object expected");
	}
	for (auto it = j.begin(); it != j.end(); ++it) {
		bool known = false;
		for (const char *k : keys) {
			known = known || it.key() == k;
		}
		if (!known) {
			fail(path, "unknown key '" + it.key() + "'");
		}
	}
}

const json *opt(const json &j, const char *key) {
	auto it = j.find(key);
	return it == j.end() || it->is_null() ? nullptr : &*it;
}

const json &req(const json &j, const char *key, const std::string &path) {
	const json *v = opt(j, key);
	if (!v) {
		fail(path, std::string("'") + key + "' required");
	}
	return *v;
}

double num(const json &j, const std::string &path) {
	if (!j.is_number()) {
		fail(path, "a number expected");
	}
	return j.get<double>();
}

bool boolean(const json &j, const std::string &path) {
	if (!j.is_boolean()) {
		fail(path, "true or false expected");
	}
	return j.get<bool>();
}

std::string str(const json &j, const std::string &path) {
	if (!j.is_string()) {
		fail(path, "a string expected");
	}
	return j.get<std::string>();
}

std::array<double, 2> pair(const json &j, const std::string &path) {
	if (!j.is_array() || j.size() != 2) {
		fail(path, "[x, y] expected");
	}
	return {num(j[0], path + "[0]"), num(j[1], path + "[1]")};
}

// A Vector2: the authored values rounded to the game's float32.
V2 vec(const json &j, const std::string &path) {
	const auto p = pair(j, path);
	return V2::of(p[0], p[1]);
}

Rect rect(const json &j, const std::string &path) {
	if (!j.is_array() || j.size() != 4) {
		fail(path, "[x, y, w, h] expected");
	}
	return {num(j[0], path), num(j[1], path), num(j[2], path), num(j[3], path)};
}

std::vector<std::string> strings(const json &j, const std::string &path) {
	if (!j.is_array()) {
		fail(path, "an array of strings expected");
	}
	std::vector<std::string> out;
	for (size_t i = 0; i < j.size(); i++) {
		out.push_back(str(j[i], path + "[" + std::to_string(i) + "]"));
	}
	return out;
}

FlagValue flag_value(const json &j, const std::string &path) {
	if (j.is_boolean()) {
		return j.get<bool>();
	}
	if (j.is_number_integer()) {
		return j.get<std::int64_t>();
	}
	if (j.is_number_float()) {
		return j.get<double>();
	}
	if (j.is_string()) {
		return j.get<std::string>();
	}
	fail(path, "a bool, number or string expected");
}

EffectSpec effect(const json &j, const std::string &path) {
	only_keys(j, path, {"type", "back", "normal", "theta", "d"});
	EffectSpec e;
	e.raw = j;
	e.type = str(req(j, "type", path), path + ".type");
	if (const json *v = opt(j, "back")) {
		e.back = boolean(*v, path + ".back");
	}
	if (const json *v = opt(j, "normal")) {
		e.normal = pair(*v, path + ".normal");
	}
	if (const json *v = opt(j, "theta")) {
		e.theta = num(*v, path + ".theta");
	}
	if (const json *v = opt(j, "d")) {
		e.d = pair(*v, path + ".d");
	}
	return e;
}

SurfaceSpec surface_spec(const json &j, const std::string &path, bool in_level);

HitSpec hit(const json &j, const std::string &path) {
	only_keys(j, path, {"kind", "deferred", "key", "op", "value", "channels", "channel", "template"});
	HitSpec h;
	h.raw = j;
	h.kind = str(req(j, "kind", path), path + ".kind");
	if (const json *v = opt(j, "deferred")) {
		h.deferred = boolean(*v, path + ".deferred");
	}
	if (h.kind == "state") {
		h.key = str(req(j, "key", path), path + ".key");
		if (const json *v = opt(j, "op")) {
			h.op = str(*v, path + ".op");
		}
		if (const json *v = opt(j, "value")) {
			h.value = flag_value(*v, path + ".value");
		}
	} else if (h.kind == "reset") {
		if (const json *v = opt(j, "channels")) {
			h.channels = strings(*v, path + ".channels");
		}
	} else if (h.kind == "spawn") {
		if (const json *v = opt(j, "channel")) {
			h.channel = str(*v, path + ".channel");
		}
		if (const json *t = opt(j, "template")) {
			const std::string tp = path + ".template";
			only_keys(*t, tp, {"surface", "orientation", "mirror_with_side", "offset", "trim"});
			auto st = std::make_shared<SpawnTemplateSpec>();
			st->surface = std::make_shared<SurfaceSpec>(surface_spec(req(*t, "surface", tp), tp + ".surface", false));
			if (const json *v = opt(*t, "orientation")) {
				st->orientation = str(*v, tp + ".orientation");
			}
			if (const json *v = opt(*t, "mirror_with_side")) {
				st->mirror_with_side = boolean(*v, tp + ".mirror_with_side");
			}
			if (const json *v = opt(*t, "offset")) {
				st->offset = vec(*v, tp + ".offset");
			}
			if (const json *v = opt(*t, "trim")) {
				st->trim = boolean(*v, tp + ".trim");
			}
			h.spawn_template = st;
		}
	} else if (h.kind != "target" && h.kind != "grapple" && h.kind != "pull") {
		fail(path + ".kind", "unknown hit effect kind '" + h.kind + "'");
	}
	return h;
}

std::optional<SideSpec> side(const json &j, const std::string &path) {
	if (j.is_null()) {
		return std::nullopt;
	}
	only_keys(j, path, {"effect", "interactive", "hits"});
	SideSpec s;
	if (const json *v = opt(j, "effect")) {
		s.effect = effect(*v, path + ".effect");
	}
	if (const json *v = opt(j, "interactive")) {
		s.interactive = boolean(*v, path + ".interactive");
	}
	if (const json *v = opt(j, "hits")) {
		if (!v->is_array()) {
			fail(path + ".hits", "an array expected");
		}
		for (size_t i = 0; i < v->size(); i++) {
			s.hits.push_back(hit((*v)[i], path + ".hits[" + std::to_string(i) + "]"));
		}
	}
	return s;
}

std::optional<SideSpec> side_at(const json &j, const char *key, const std::string &path) {
	auto it = j.find(key);
	return it == j.end() ? std::nullopt : side(*it, path + "." + key);
}

SurfaceSpec surface_spec(const json &j, const std::string &path, bool in_level) {
	if (in_level) {
		only_keys(j, path, {"ids", "label", "start", "end", "via", "circle", "straight", "pre_transform", "left", "right",
				"resolver", "presence", "player_solid"});
	} else {
		only_keys(j, path, {"start", "end", "via", "circle", "straight", "left", "right", "resolver", "presence", "player_solid"});
	}
	SurfaceSpec s;
	if (const json *c = opt(j, "circle")) {
		only_keys(*c, path + ".circle", {"center", "radius"});
		s.full_circle = true;
		s.center = vec(req(*c, "center", path + ".circle"), path + ".circle.center");
		s.radius = num(req(*c, "radius", path + ".circle"), path + ".circle.radius");
	} else {
		s.start = vec(req(j, "start", path), path + ".start");
		s.end = vec(req(j, "end", path), path + ".end");
		s.via = vec(req(j, "via", path), path + ".via");
	}
	if (const json *v = opt(j, "straight")) {
		s.straight = boolean(*v, path + ".straight");
	}
	if (const json *v = opt(j, "pre_transform")) {
		s.pre_transform = boolean(*v, path + ".pre_transform");
		if (s.pre_transform && !s.full_circle) {
			fail(path, "a pre-transform is a full circle");
		}
	}
	s.left = side_at(j, "left", path);
	s.right = side_at(j, "right", path);
	if (const json *r = opt(j, "resolver")) {
		const std::string rp = path + ".resolver";
		only_keys(*r, rp, {"key", "table"});
		s.resolver_key = str(req(*r, "key", rp), rp + ".key");
		const json &t = req(*r, "table", rp);
		if (!t.is_array()) {
			fail(rp + ".table", "an array of [value, {left, right}] expected");
		}
		for (size_t i = 0; i < t.size(); i++) {
			const std::string tp = rp + ".table[" + std::to_string(i) + "]";
			if (!t[i].is_array() || t[i].size() != 2) {
				fail(tp, "[value, {left, right}] expected");
			}
			only_keys(t[i][1], tp + "[1]", {"left", "right"});
			s.resolver.push_back({str(t[i][0], tp + "[0]"), side_at(t[i][1], "left", tp), side_at(t[i][1], "right", tp)});
		}
	}
	if (const json *p = opt(j, "presence")) {
		if (!p->is_array()) {
			fail(path + ".presence", "an array expected");
		}
		for (size_t i = 0; i < p->size(); i++) {
			const std::string pp = path + ".presence[" + std::to_string(i) + "]";
			only_keys((*p)[i], pp, {"key", "values"});
			s.presence.push_back({str(req((*p)[i], "key", pp), pp + ".key"), strings(req((*p)[i], "values", pp), pp + ".values")});
		}
	}
	if (const json *v = opt(j, "player_solid")) {
		s.player_solid = boolean(*v, path + ".player_solid");
	}
	return s;
}

// rc-level/2: a region {"rect": [x, y, w, h]} | {"circle": {"center", "radius"}}
Region region(const json &j, const std::string &path) {
	only_keys(j, path, {"rect", "circle"});
	Region r;
	if (const json *c = opt(j, "circle")) {
		only_keys(*c, path + ".circle", {"center", "radius"});
		r.circle = true;
		r.center = vec(req(*c, "center", path + ".circle"), path + ".circle.center");
		r.radius = num(req(*c, "radius", path + ".circle"), path + ".circle.radius");
		if (!(r.radius > 0)) {
			fail(path + ".circle.radius", "a positive radius expected");
		}
		if (opt(j, "rect")) {
			fail(path, "a rect or a circle, not both");
		}
		return r;
	}
	r.rect = rect(req(j, "rect", path), path + ".rect");
	if (!(r.rect.w > 0) || !(r.rect.h > 0)) {
		fail(path + ".rect", "a positive width and height expected");
	}
	return r;
}

std::vector<PresenceSpec> conditions(const json &j, const std::string &path) {
	if (!j.is_array()) {
		fail(path, "an array of {key, values} expected");
	}
	std::vector<PresenceSpec> out;
	for (size_t i = 0; i < j.size(); i++) {
		const std::string pp = path + "[" + std::to_string(i) + "]";
		only_keys(j[i], pp, {"key", "values"});
		out.push_back({str(req(j[i], "key", pp), pp + ".key"), strings(req(j[i], "values", pp), pp + ".values")});
	}
	return out;
}

// rc-level/2's objective and detectors (not surfaces: the lowering leaves them out)
void parse_goals(const json &j, Level &lv) {
	const std::string P = "level";
	if (const json *o = opt(j, "objective")) {
		const std::string op = P + ".objective";
		only_keys(*o, op, {"goal", "requires"});
		Objective ob;
		ob.goal = region(req(*o, "goal", op), op + ".goal");
		if (const json *r = opt(*o, "requires")) {
			ob.conditions = conditions(*r, op + ".requires");
		}
		lv.objective = ob;
	}
	if (const json *ds = opt(j, "detectors")) {
		if (!ds->is_array()) {
			fail(P + ".detectors", "an array of detectors expected");
		}
		for (size_t i = 0; i < ds->size(); i++) {
			const json &d = (*ds)[i];
			const std::string dp = P + ".detectors[" + std::to_string(i) + "]";
			only_keys(d, dp, {"name", "for", "region", "segment", "flag", "mode", "value"});
			Detector det;
			det.name = str(req(d, "name", dp), dp + ".name");
			for (const Detector &other : lv.detectors) {
				if (other.name == det.name) {
					fail(dp + ".name", "'" + det.name + "' names another detector");
				}
			}
			if (const json *v = opt(d, "for")) {
				det.target = str(*v, dp + ".for");
				if (det.target != "player" && det.target != "object" && det.target != "any") {
					fail(dp + ".for", "player, object or any expected");
				}
			}
			const json *seg = opt(d, "segment");
			const json *reg = opt(d, "region");
			if ((seg == nullptr) == (reg == nullptr)) {
				fail(dp, "a region or a segment expected");
			}
			if (seg != nullptr) {
				only_keys(*seg, dp + ".segment", {"a", "b"});
				det.segment = true;
				det.a = vec(req(*seg, "a", dp + ".segment"), dp + ".segment.a");
				det.b = vec(req(*seg, "b", dp + ".segment"), dp + ".segment.b");
			} else {
				det.region = region(*reg, dp + ".region");
			}
			det.flag = "det:" + det.name;
			if (const json *v = opt(d, "flag")) {
				det.flag = str(*v, dp + ".flag");
			}
			if (const json *v = opt(d, "mode")) {
				det.mode = str(*v, dp + ".mode");
				if (det.mode != "while" && det.mode != "enter" && det.mode != "toggle") {
					fail(dp + ".mode", "while, enter or toggle expected");
				}
			}
			if (det.segment && det.mode == "while") {
				fail(dp + ".mode", "a segment is crossed, never inside: enter or toggle");
			}
			if (const json *v = opt(d, "value")) {
				const FlagValue fv = flag_value(*v, dp + ".value");
				det.value = std::holds_alternative<bool>(fv)		   ? rc::FlagValue(std::get<bool>(fv))
						: std::holds_alternative<std::int64_t>(fv) ? rc::FlagValue(double(std::get<std::int64_t>(fv)))
						: std::holds_alternative<double>(fv)	   ? rc::FlagValue(std::get<double>(fv))
																   : rc::FlagValue(std::get<std::string>(fv));
			}
			lv.detectors.push_back(std::move(det));
		}
	}
	if (const json *os = opt(j, "objects")) {
		if (!os->is_array()) {
			fail(P + ".objects", "an array of objects expected");
		}
		for (size_t i = 0; i < os->size(); i++) {
			const json &o = (*os)[i];
			const std::string op = P + ".objects[" + std::to_string(i) + "]";
			only_keys(o, op, {"name", "circle", "kind", "solid", "face", "label"});
			Object ob;
			ob.name = str(req(o, "name", op), op + ".name");
			for (const Object &other : lv.objects) {
				if (other.name == ob.name) {
					fail(op + ".name", "'" + ob.name + "' names another object");
				}
			}
			const json &c = req(o, "circle", op);
			only_keys(c, op + ".circle", {"center", "radius"});
			ob.center = vec(req(c, "center", op + ".circle"), op + ".circle.center");
			if (const json *r = opt(c, "radius")) {
				ob.radius = num(*r, op + ".circle.radius");
			}
			if (!(ob.radius > 0)) {
				fail(op + ".circle.radius", "a positive radius expected");
			}
			if (const json *v = opt(o, "kind")) {
				const std::string k = str(*v, op + ".kind");
				if (k != "pullable" && k != "static") {
					fail(op + ".kind", "pullable or static expected");
				}
				ob.pullable = k == "pullable";
			}
			if (const json *v = opt(o, "solid")) {
				ob.solid = boolean(*v, op + ".solid");
			}
			if (const json *v = opt(o, "face")) {
				ob.face = str(*v, op + ".face");
				if (ob.face != "wall" && ob.face != "mirror" && ob.face != "pass") {
					fail(op + ".face", "wall, mirror or pass expected");
				}
			}
			if (const json *v = opt(o, "label")) {
				ob.label = str(*v, op + ".label");
			}
			lv.objects.push_back(std::move(ob));
		}
	}
}

Level parse_v1(const json &j) {
	const std::string P = "level";
	only_keys(j, P, {"schema", "name", "bounds", "gravity", "room", "player", "camera", "background", "colliders", "features",
			"surfaces"});
	if (str(req(j, "schema", P), P + ".schema") != "rc-level/1") {
		fail(P + ".schema", "rc-level/1 or rc-level/2 expected");
	}
	Level lv;
	if (const json *v = opt(j, "name")) {
		lv.name = str(*v, P + ".name");
	}
	if (const json *v = opt(j, "bounds")) {
		lv.bounds = rect(*v, P + ".bounds");
	}
	if (const json *v = opt(j, "gravity")) {
		lv.gravity = vec(*v, P + ".gravity");
	}
	if (const json *r = opt(j, "room")) {
		only_keys(*r, P + ".room", {"rect", "built"});
		lv.room = rect(req(*r, "rect", P + ".room"), P + ".room.rect");
		if (const json *v = opt(*r, "built")) {
			lv.room_built = boolean(*v, P + ".room.built");
		}
	}
	if (const json *p = opt(j, "player")) {
		only_keys(*p, P + ".player", {"start"});
		lv.player_start = vec(req(*p, "start", P + ".player"), P + ".player.start");
	}
	if (const json *c = opt(j, "camera")) {
		only_keys(*c, P + ".camera", {"start", "smoothing", "smoothing_speed"});
		Level::Camera cam;
		cam.start = vec(req(*c, "start", P + ".camera"), P + ".camera.start");
		if (const json *v = opt(*c, "smoothing")) {
			cam.smoothing = boolean(*v, P + ".camera.smoothing");
		}
		if (const json *v = opt(*c, "smoothing_speed")) {
			cam.smoothing_speed = num(*v, P + ".camera.smoothing_speed");
		}
		lv.camera = cam;
	}
	if (const json *b = opt(j, "background")) {
		only_keys(*b, P + ".background", {"rect", "color"});
		Level::Background bg;
		bg.rect = rect(req(*b, "rect", P + ".background"), P + ".background.rect");
		const json &c = req(*b, "color", P + ".background");
		if (!c.is_array() || c.size() != 4) {
			fail(P + ".background.color", "[r, g, b, a] expected");
		}
		for (int i = 0; i < 4; i++) {
			bg.color[size_t(i)] = num(c[size_t(i)], P + ".background.color");
		}
		lv.background = bg;
	}
	if (const json *cs = opt(j, "colliders")) {
		if (!cs->is_array()) {
			fail(P + ".colliders", "an array expected");
		}
		for (size_t i = 0; i < cs->size(); i++) {
			const std::string cp = P + ".colliders[" + std::to_string(i) + "]";
			only_keys((*cs)[i], cp, {"a", "b"});
			lv.colliders.push_back({vec(req((*cs)[i], "a", cp), cp + ".a"), vec(req((*cs)[i], "b", cp), cp + ".b")});
		}
	}
	if (const json *f = opt(j, "features")) {
		only_keys(*f, P + ".features", {"game", "preview", "arrow", "hud", "cursor"});
		auto flag = [&](const char *k, bool &out) {
			if (const json *v = opt(*f, k)) {
				out = boolean(*v, P + ".features." + k);
			}
		};
		flag("game", lv.features.game);
		flag("preview", lv.features.preview);
		flag("arrow", lv.features.arrow);
		flag("hud", lv.features.hud);
		flag("cursor", lv.features.cursor);
	}
	const json &ss = req(j, "surfaces", P);
	if (!ss.is_array()) {
		fail(P + ".surfaces", "an array expected");
	}
	for (size_t i = 0; i < ss.size(); i++) {
		const std::string sp = P + ".surfaces[" + std::to_string(i) + "]";
		LevelSurface ls;
		ls.spec = surface_spec(ss[i], sp, true);
		const json &ids = req(ss[i], "ids", sp);
		if (!ids.is_array() || ids.empty()) {
			fail(sp + ".ids", "a non-empty array of ints expected");
		}
		for (const json &id : ids) {
			if (!id.is_number_integer()) {
				fail(sp + ".ids", "ints expected");
			}
			ls.ids.push_back(id.get<int>());
		}
		const size_t want = ls.spec.is_portal() && !ls.spec.pre_transform ? 2 : 1;
		if (ls.ids.size() != want) {
			fail(sp + ".ids", want == 2 ? "a portal takes two ids (the source, its image)" : "one id expected");
		}
		if (const json *v = opt(ss[i], "label")) {
			ls.label = str(*v, sp + ".label");
		}
		lv.surfaces.push_back(std::move(ls));
	}
	return lv;
}

} // namespace

Level parse_level_json(const json &j) {
	if (!j.is_object() || j.value("schema", json()) != "rc-level/2") {
		if (j.is_object() && j.contains("schema") && j["schema"] != "rc-level/1") {
			fail("level.schema", "rc-level/1 or rc-level/2 expected");
		}
		return parse_v1(j);
	}
	const Lowered low = lower_level(j);
	Level lv;
	try {
		lv = parse_v1(low.level);
	} catch (const LevelError &e) {
		// a lowered surface's path names the element it came from
		const std::string m = e.what(), head = "level.surfaces[";
		const size_t close = m.find(']');
		if (m.rfind(head, 0) == 0 && close != std::string::npos) {
			const size_t k = std::stoul(m.substr(head.size(), close - head.size()));
			if (k < low.origins.size()) {
				throw LevelError(low.origins[k] + m.substr(close + 1));
			}
		}
		throw;
	}
	parse_goals(j, lv);
	for (auto it = low.flags.begin(); it != low.flags.end(); ++it) {
		const json &v = it.value();
		rc::FlagValue f = v.is_boolean() ? rc::FlagValue(v.get<bool>())
				: v.is_number()			 ? rc::FlagValue(v.get<double>())
										 : rc::FlagValue(v.get<std::string>());
		lv.flags.emplace_back(it.key(), std::move(f));
	}
	return lv;
}

Level parse_level(std::string_view json_text) {
	json j;
	try {
		j = json::parse(json_text);
	} catch (const json::parse_error &e) {
		throw LevelError(std::string("not JSON: ") + e.what());
	}
	return parse_level_json(j);
}

SurfaceSpec object_spec(const Object &o, const V2 &center) {
	SurfaceSpec s;
	s.full_circle = true;
	s.center = center;
	s.radius = o.radius;
	s.player_solid = o.solid;
	SideSpec side;
	side.effect.type = o.face == "mirror" ? "reflection" : o.face == "pass" ? "pass" : "terminal";
	side.effect.raw = {{"type", side.effect.type}};
	side.interactive = o.pullable || o.face == "mirror";
	if (o.pullable) {
		HitSpec h;
		h.kind = "pull";
		h.raw = {{"kind", "pull"}};
		side.hits.push_back(h);
	}
	s.left = side;
	s.right = side;
	return s;
}

bool Region::contains(const V2 &p) const {
	if (circle) {
		const double dx = double(p.x) - center.x, dy = double(p.y) - center.y;
		return dx * dx + dy * dy <= radius * radius;
	}
	return p.x >= rect.x && p.y >= rect.y && p.x <= rect.x + rect.w && p.y <= rect.y + rect.h;
}

Level load_level(const std::string &path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		throw LevelError("cannot read " + path);
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	try {
		return parse_level(ss.str());
	} catch (const LevelError &e) {
		throw LevelError(path + ": " + e.what());
	}
}

} // namespace rcg
