// The level builder (SurfaceSpec.build, RigidMotionEffect.create_portal_pair,
// Surface.pre_transform_circle) and the scene defs (SceneDefs).
#include "rcg/build.hpp"

#include <cmath>

namespace rcg {
namespace {

using json = nlohmann::json;

bool known_type(const std::string &t) {
	return t == "pass" || t == "terminal" || t == "reflection" || t == "inversion" || t == "portal" ||
		   t == "line_normal_projection" || t == "circle_normal_projection" || t == "line_directional_projection" ||
		   t == "circle_directional_projection";
}

// SurfaceSpec.segment()
rc::arc::SegGeom segment_of(const SurfaceSpec &s) {
	if (s.full_circle) {
		return rc::arc::SegGeom::full_from_carrier(rc::arc::from_circle(s.center, s.radius));
	}
	rc::arc::SegGeom g = rc::arc::SegGeom::make(s.start, s.end, s.via);
	if (s.straight) {
		g.carrier = rc::arc::straight_carrier(s.start, s.end);
	}
	return g;
}

std::vector<std::string> validate(const SurfaceSpec &s);

// SpawnTemplate.validate: its surface, which cannot spawn (no nesting).
void validate_template(const SpawnTemplateSpec *t, std::vector<std::string> &out) {
	if (t == nullptr || !t->surface) {
		out.push_back("a spawn template needs a surface");
		return;
	}
	for (const std::string &e : validate(*t->surface)) {
		out.push_back(e);
	}
	std::vector<const SideSpec *> sides;
	for (const std::optional<SideSpec> *sd : {&t->surface->left, &t->surface->right}) {
		if (*sd) {
			sides.push_back(&**sd);
		}
	}
	for (const ResolverRow &r : t->surface->resolver) {
		for (const std::optional<SideSpec> *sd : {&r.left, &r.right}) {
			if (*sd) {
				sides.push_back(&**sd);
			}
		}
	}
	for (const SideSpec *sd : sides) {
		for (const HitSpec &h : sd->hits) {
			if (h.kind == "spawn") {
				out.push_back("a spawned surface cannot spawn (no nesting, GAME_SPEC 10.6)");
			}
		}
	}
}

// SideSpec.validate / EffectSpec.validate: the problems of a side on a carrier.
void validate_side(const SideSpec &side, bool carrier_is_line, const V2 &carrier_dir, std::vector<std::string> &out) {
	const std::string &t = side.effect.type;
	for (const HitSpec &h : side.hits) {
		if (h.kind == "spawn") {
			if (h.channel.empty()) {
				out.push_back("a spawn needs a channel");
			}
			if (!h.spawn_template) {
				out.push_back("a spawn needs a template");
			} else {
				validate_template(h.spawn_template.get(), out);
			}
		}
	}
	if (t == "pass" && side.hits.empty() && side.interactive) {
		out.push_back("a pass side without hit effects has nothing to plan (GAME_SPEC 9.1)");
	}
	if (!known_type(t)) {
		out.push_back("unknown effect type '" + t + "'");
		return;
	}
	const bool needs_line = t == "line_normal_projection" || t == "line_directional_projection";
	const bool needs_circle = t == "inversion" || t == "circle_normal_projection" || t == "circle_directional_projection";
	if (needs_line && !carrier_is_line) {
		out.push_back(t + " needs a line carrier");
	}
	if (needs_circle && carrier_is_line) {
		out.push_back(t + " needs a circle carrier");
	}
	const bool directional = t == "line_directional_projection" || t == "circle_directional_projection";
	if (directional && !side.effect.normal) {
		out.push_back(t + " needs a normal");
	}
	if (t == "line_directional_projection" && side.effect.normal && carrier_dir != V2{}) {
		const V2 n = V2::of((*side.effect.normal)[0], (*side.effect.normal)[1]);
		if (n.cross(carrier_dir) == 0.0f) {
			out.push_back(t + ": the normal runs along the line carrier, no beam leaves it (GAME_SPEC 23)");
		}
	}
	if (t == "portal" && !(side.effect.theta && side.effect.d)) {
		out.push_back("a portal needs theta and d");
	}
}

// SurfaceSpec.validate
std::vector<std::string> validate(const SurfaceSpec &s) {
	std::vector<std::string> out;
	if (s.full_circle) {
		if (!(s.radius > 0.0)) {
			out.push_back("a full circle needs a positive radius");
			return out;
		}
	} else if (s.start == s.end) {
		out.push_back("a segment needs distinct endpoints");
		return out;
	}
	if (s.pre_transform) {
		return out;
	}
	const bool is_line = segment_of(s).is_line();
	const V2 dir = is_line ? s.end - s.start : V2{};
	auto check = [&](const std::optional<SideSpec> &side) {
		if (side) {
			validate_side(*side, is_line, dir, out);
		}
	};
	check(s.left);
	check(s.right);
	for (const ResolverRow &r : s.resolver) {
		check(r.left);
		check(r.right);
	}
	if (s.is_portal() && s.right) {
		const EffectSpec &a = s.left->effect;
		const EffectSpec &b = s.right->effect;
		if (a.type != b.type || a.back != b.back || a.normal != b.normal || a.theta != b.theta || a.d != b.d) {
			out.push_back("a portal's sides are the same portal");
		}
	}
	return out;
}

// EffectSpec.build (the effect as the game holds it).
BuiltEffect effect_of(const EffectSpec &e) {
	BuiltEffect b;
	b.type = e.type;
	b.back = e.back;
	if (e.normal) {
		// the directional projections normalize their normal (float32, Vector2.normalized)
		b.normal = V2::of((*e.normal)[0], (*e.normal)[1]).normalized();
	}
	return b;
}

std::optional<BuiltSide> side_of(const std::optional<SideSpec> &s) {
	if (!s) {
		return std::nullopt;
	}
	return BuiltSide{effect_of(s->effect), s->interactive, s->hits};
}

// SurfaceSpec.spawn_channels: the channels its sides spawn into, first seen first.
std::vector<std::string> spawn_channels(const SurfaceSpec &s) {
	std::vector<std::string> out;
	auto scan = [&](const std::optional<SideSpec> &side) {
		if (!side) {
			return;
		}
		for (const HitSpec &h : side->hits) {
			if (h.kind == "spawn") {
				bool seen = false;
				for (const std::string &c : out) {
					seen = seen || c == h.channel;
				}
				if (!seen) {
					out.push_back(h.channel);
				}
			}
		}
	};
	scan(s.left);
	scan(s.right);
	for (const ResolverRow &r : s.resolver) {
		scan(r.left);
		scan(r.right);
	}
	return out;
}

json vec_json(const V2 &p) {
	if (p.is_inf()) {
		return nullptr;
	}
	return json::array({double(p.x), double(p.y)});
}

json flag_json(const FlagValue &v) {
	return std::visit([](const auto &x) -> json { return x; }, v);
}

// Effect.to_spec
json effect_json(const BuiltEffect &e) {
	json d = json::object();
	d["type"] = e.type;
	if (e.type == "portal") {
		d["theta"] = e.theta;
		d["d"] = json::array({double(e.d.x), double(e.d.y)});
	} else if (e.type == "line_normal_projection" || e.type == "circle_normal_projection") {
		d["back"] = e.back;
	} else if (e.type == "line_directional_projection" || e.type == "circle_directional_projection") {
		d["back"] = e.back;
		d["normal"] = json::array({double(e.normal.x), double(e.normal.y)});
	}
	return d;
}

// SceneDefs.describe_config
json describe_config(const std::optional<BuiltSide> &cfg, int host_id) {
	if (!cfg) {
		return nullptr;
	}
	json d = effect_json(cfg->effect);
	d["interactive"] = cfg->interactive;
	json writes = in_shot_writes(*cfg, host_id);
	if (!writes.empty()) {
		d["states"] = writes;
	}
	return d;
}

} // namespace

namespace {

std::optional<rc::SideDef> side_def(const std::optional<BuiltSide> &cfg, int host_id, int pair) {
	if (!cfg) {
		return std::nullopt;
	}
	const BuiltEffect &e = cfg->effect;
	rc::SideDef d;
	d.type = rc::effect_type_from_json(e.type);
	d.interactive = cfg->interactive;
	if (e.type == "line_normal_projection" || e.type == "circle_normal_projection" || e.type == "line_directional_projection" ||
			e.type == "circle_directional_projection") {
		d.back = e.back;
	}
	if (e.type == "line_directional_projection" || e.type == "circle_directional_projection") {
		d.has_normal = true;
		d.nx = e.normal.x;
		d.ny = e.normal.y;
	}
	if (e.type == "portal") {
		d.theta = e.theta;
		d.dx = e.d.x;
		d.dy = e.d.y;
		d.pair = pair != 0 ? pair : -1;
	}
	for (const json &w : in_shot_writes(*cfg, host_id)) {
		rc::StateChangeDef c;
		c.key = w["key"].get<std::string>();
		c.toggle = w["op"] == "toggle";
		const json &v = w["value"];
		c.value = v.is_boolean() ? rc::FlagValue(v.get<bool>()) : v.is_number() ? rc::FlagValue(v.get<double>()) : rc::FlagValue(v.get<std::string>());
		d.states.push_back(std::move(c));
	}
	return d;
}

} // namespace

std::vector<rc::SurfaceDef> defs_of(const std::vector<BuiltSurface> &surfaces) {
	std::vector<rc::SurfaceDef> out;
	for (const BuiltSurface &s : surfaces) {
		const rc::arc::GeneralizedCircle &g = s.seg.get_carrier();
		rc::SurfaceDef d;
		d.id = s.id;
		d.line = g.is_line();
		d.sx = s.seg.s.x;
		d.sy = s.seg.s.y;
		d.ex = s.seg.e.x;
		d.ey = s.seg.e.y;
		d.has_via = !s.seg.v.is_inf();
		if (d.has_via) {
			d.vx = s.seg.v.x;
			d.vy = s.seg.v.y;
		}
		d.full = s.seg.full;
		if (s.seg.full && !g.is_line()) {
			d.has_circle = true;
			d.cx = g.center_x();
			d.cy = g.center_y();
			d.radius = g.radius();
		}
		if (s.resolver.empty()) {
			d.left = side_def(s.left, s.id, s.portal_pair);
			d.right = side_def(s.right, s.id, s.portal_pair);
		} else {
			rc::ResolverDef r;
			r.key = s.resolver_key;
			for (const BuiltRow &row : s.resolver) {
				r.table.emplace_back(row.value, std::make_pair(side_def(row.left, s.id, 0), side_def(row.right, s.id, 0)));
			}
			d.resolver = r;
		}
		d.is_target = s.is_target();
		d.pre_transform = s.pre_transform;
		for (const PresenceSpec &c : s.presence) {
			d.presence.push_back(rc::PresenceCond{c.key, c.values});
		}
		out.push_back(std::move(d));
	}
	return out;
}

json describe_side(const BuiltSide *cfg, int host_id) {
	return describe_config(cfg ? std::optional<BuiltSide>(*cfg) : std::nullopt, host_id);
}

V2 apply_motion(const V2 &z, double theta, const V2 &d) {
	const double c = std::cos(theta);
	const double s = std::sin(theta);
	const double zx = z.x, zy = z.y, dx = d.x, dy = d.y;
	return V2::of(c * zx - s * zy + dx, s * zx + c * zy + dy);
}

std::vector<const BuiltSide *> BuiltSurface::all_sides() const {
	std::vector<const BuiltSide *> out;
	if (!resolver.empty()) {
		for (const BuiltRow &r : resolver) {
			out.push_back(r.left ? &*r.left : nullptr);
			out.push_back(r.right ? &*r.right : nullptr);
		}
	} else {
		out.push_back(left ? &*left : nullptr);
		out.push_back(right ? &*right : nullptr);
	}
	return out;
}

bool BuiltSurface::is_target() const {
	for (const BuiltSide *s : all_sides()) {
		if (s) {
			for (const HitSpec &h : s->hits) {
				if (h.kind == "target") {
					return true;
				}
			}
		}
	}
	return false;
}

json in_shot_writes(const BuiltSide &side, int host_id) {
	json out = json::array();
	auto write = [&](const std::string &key, const char *op, json value) {
		out.push_back({{"key", key}, {"op", op}, {"value", std::move(value)}});
	};
	for (const HitSpec &h : side.hits) {
		if (h.deferred) {
			continue;
		}
		if (h.kind == "state") {
			const bool toggle = h.op == "toggle";
			write(h.key, toggle ? "toggle" : "set", toggle ? json(true) : flag_json(h.value));
		} else if (h.kind == "target") {
			write("target:" + std::to_string(host_id), "set", true);
		} else if (h.kind == "reset") {
			for (const std::string &c : h.channels) {
				write("chan:" + c, "set", "armed");
			}
		} else if (h.kind == "spawn") {
			write("chan:" + h.channel, "set", "pending");
		}
	}
	return out;
}

std::vector<BuiltSurface> build_level(const Level &level) {
	std::vector<BuiltSurface> out;
	for (size_t i = 0; i < level.surfaces.size(); i++) {
		const LevelSurface &ls = level.surfaces[i];
		const SurfaceSpec &s = ls.spec;
		const std::vector<std::string> errors = validate(s);
		if (!errors.empty()) {
			std::string msg = level.name + ": surface " + std::to_string(ls.ids.front()) + ":";
			for (const std::string &e : errors) {
				msg += " " + e + ";";
			}
			throw LevelError(msg);
		}
		BuiltSurface b;
		b.id = ls.ids.front();
		b.label = ls.label;
		b.seg = segment_of(s);
		b.player_solid = s.player_solid;
		if (s.pre_transform) {
			// Surface.pre_transform_circle: an inversion on both sides, not plannable, not solid
			BuiltEffect inv_eff;
			inv_eff.type = "inversion";
			BuiltSide inv{inv_eff, false, {}};
			b.left = inv;
			b.right = inv;
			b.player_solid = false;
			b.pre_transform = true;
			out.push_back(std::move(b));
			continue;
		}
		// presence: authored, then a host exists while every channel it spawns into is armed
		b.presence = s.presence;
		for (const std::string &ch : spawn_channels(s)) {
			b.presence.push_back({"chan:" + ch, {"DEFAULT", "armed"}});
		}
		if (s.is_portal()) {
			const double theta = *s.left->effect.theta;
			const V2 d = V2::of((*s.left->effect.d)[0], (*s.left->effect.d)[1]);
			const SideSpec &ls_side = *s.left;
			const SideSpec &rs_side = s.right ? *s.right : *s.left;
			BuiltEffect src_eff;
			src_eff.type = "portal";
			src_eff.theta = theta;
			src_eff.d = d;
			BuiltEffect tgt_eff;
			tgt_eff.type = "portal";
			tgt_eff.theta = -theta;
			tgt_eff.d = -apply_motion(d, -theta, V2{});
			// the image of a line is a line, exactly (Segment.straight)
			const V2 ts = apply_motion(b.seg.s, theta, d), te = apply_motion(b.seg.e, theta, d), tv = apply_motion(b.seg.v, theta, d);
			rc::arc::SegGeom tseg = rc::arc::SegGeom::make(ts, te, tv, b.seg.full);
			if (b.seg.is_line() && !b.seg.full) {
				tseg.carrier = rc::arc::straight_carrier(ts, te);
			}
			b.label = "PortalSrc";
			b.left = BuiltSide{src_eff, ls_side.interactive, ls_side.hits};
			b.right = BuiltSide{src_eff, rs_side.interactive, rs_side.hits};
			b.portal_pair = ls.ids.at(1);
			BuiltSurface t = b;
			t.id = ls.ids.at(1);
			t.portal_pair = b.id;
			t.label = "PortalTgt";
			t.seg = tseg;
			t.left = BuiltSide{tgt_eff, ls_side.interactive, ls_side.hits};
			t.right = BuiltSide{tgt_eff, rs_side.interactive, rs_side.hits};
			out.push_back(std::move(b));
			out.push_back(std::move(t));
			continue;
		}
		if (!s.resolver.empty()) {
			b.resolver_key = s.resolver_key;
			for (const ResolverRow &r : s.resolver) {
				b.resolver.push_back({r.value, side_of(r.left), side_of(r.right)});
			}
		} else {
			b.left = side_of(s.left);
			b.right = side_of(s.right);
		}
		out.push_back(std::move(b));
	}
	return out;
}

json describe_surface(const BuiltSurface &s) {
	const rc::arc::GeneralizedCircle &g = s.seg.get_carrier();
	json circle = nullptr;
	if (s.seg.full && !g.is_line()) {
		circle = {{"center", json::array({g.center_x(), g.center_y()})}, {"radius", g.radius()}};
	}
	const bool categorical = !s.resolver.empty();
	json out = {
		{"circle", circle},
		{"id", s.id},
		{"start", vec_json(s.seg.s)},
		{"end", vec_json(s.seg.e)},
		{"via", vec_json(s.seg.v)},
		{"full", s.seg.full},
		{"line", g.is_line()},
		{"left", categorical ? json(nullptr) : describe_config(s.left, s.id)},
		{"right", categorical ? json(nullptr) : describe_config(s.right, s.id)},
		{"is_target", s.is_target()},
		{"player_solid", s.player_solid},
	};
	if (s.pre_transform) {
		out["pre_transform"] = true;
	}
	if (!s.presence.empty()) {
		json p = json::array();
		for (const PresenceSpec &c : s.presence) {
			p.push_back({{"key", c.key}, {"values", c.values}});
		}
		out["presence"] = p;
	}
	if (categorical) {
		json table = json::object();
		for (const BuiltRow &r : s.resolver) {
			table[r.value] = {{"left", describe_config(r.left, s.id)}, {"right", describe_config(r.right, s.id)}};
		}
		out["resolver"] = {{"key", s.resolver_key}, {"table", table}};
	}
	return out;
}

json describe_level(const std::vector<BuiltSurface> &surfaces) {
	json out = json::array();
	for (const BuiltSurface &s : surfaces) {
		json d = describe_surface(s);
		if (s.portal_pair != 0) {
			for (const char *key : {"left", "right"}) {
				if (d[key].is_object() && d[key]["type"] == "portal") {
					d[key]["pair"] = s.portal_pair;
				}
			}
		}
		out.push_back(std::move(d));
	}
	return out;
}

} // namespace rcg
