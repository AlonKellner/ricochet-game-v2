#include "rcg/trace_json.hpp"

namespace rcg::trace_json {
namespace {

using json = nlohmann::json;
using ojson = nlohmann::ordered_json;

ojson flag_value(const rc::FlagValue &v) {
	return std::visit([](const auto &x) -> ojson { return x; }, v);
}

ojson side(const std::optional<rc::SideDef> &s) {
	if (!s) {
		return nullptr;
	}
	ojson o;
	o["type"] = rc::effect_type_json_name(s->type);
	o["interactive"] = s->interactive;
	o["back"] = s->back;
	o["normal"] = s->has_normal ? ojson::array({s->nx, s->ny}) : ojson(nullptr);
	o["theta"] = s->theta;
	o["d"] = ojson::array({s->dx, s->dy});
	o["pair"] = s->pair >= 0 ? ojson(s->pair) : ojson(nullptr);
	ojson states = ojson::array();
	for (const rc::StateChangeDef &c : s->states) {
		states.push_back({{"key", c.key}, {"op", c.toggle ? "toggle" : "set"}, {"value", flag_value(c.value)}});
	}
	o["states"] = states;
	if (s->landing) {
		o["landing"] = true; // (Stage 134; absent when false: older defs read back as written)
	}
	return o;
}

json vec(const rc::V2 &v) { return json::array({double(v.x), double(v.y)}); }

} // namespace

ojson defs(const std::vector<rc::SurfaceDef> &defs) {
	ojson out = ojson::array();
	for (const rc::SurfaceDef &s : defs) {
		ojson d;
		d["id"] = s.id;
		d["line"] = s.line;
		d["start"] = {s.sx, s.sy};
		d["end"] = {s.ex, s.ey};
		d["via"] = s.has_via ? ojson::array({s.vx, s.vy}) : ojson(nullptr);
		d["full"] = s.full;
		d["circle"] = s.has_circle ? ojson{{"center", {s.cx, s.cy}}, {"radius", s.radius}} : ojson(nullptr);
		d["left"] = side(s.left);
		d["right"] = side(s.right);
		d["is_target"] = s.is_target;
		d["pre_transform"] = s.pre_transform;
		ojson presence = ojson::array();
		for (const rc::PresenceCond &c : s.presence) {
			presence.push_back({{"key", c.key}, {"values", c.values}});
		}
		d["presence"] = presence;
		if (s.resolver) {
			ojson table = ojson::object();
			for (const auto &[k, sides] : s.resolver->table) {
				table[k] = {{"left", side(sides.first)}, {"right", side(sides.second)}};
			}
			d["resolver"] = {{"key", s.resolver->key}, {"table", table}};
		} else {
			d["resolver"] = nullptr;
		}
		out.push_back(d);
	}
	return out;
}

ojson plan(const std::vector<rc::PlanItem> &plan) {
	ojson out = ojson::array();
	for (const rc::PlanItem &p : plan) {
		ojson e;
		e["surface_id"] = p.surface_id;
		e["side"] = p.left ? "LEFT" : "RIGHT";
		e["click"] = p.hint ? ojson::array({double(p.hint->x), double(p.hint->y)}) : ojson(nullptr);
		if (p.span) {
			const auto &s = *p.span;
			e["span"] = {{"start", {s[0], s[1]}}, {"end", {s[2], s[3]}}, {"via", {s[4], s[5]}}};
		} else {
			e["span"] = nullptr;
		}
		out.push_back(e);
	}
	return out;
}

ojson flags(const rc::Flags &f) {
	ojson out = ojson::object();
	for (const auto &[k, v] : f.entries()) {
		out[k] = flag_value(v);
	}
	return out;
}

json path(const rc::TracePath &p) {
	json steps = json::array();
	for (const rc::TraceStep &s : p.steps) {
		json hit = nullptr;
		if (s.hit) {
			hit = json::array({s.hit->t, vec(s.hit->point), s.hit->surface_id, int(s.hit->side), s.hit->on_segment});
		}
		steps.push_back(json::array({vec(s.start), vec(s.end), vec(s.via), s.frame_id, s.is_arc_step, int(s.type), s.surface_id, s.hit_side,
			s.hit_on_segment, s.after_portal, s.ghost, hit}));
	}
	json hits = json::array();
	for (const rc::HitRecord &h : p.hits) {
		hits.push_back(json::array({h.surface_id, int(h.side), h.on_segment, h.fired, h.state, vec(h.point), vec(h.tangent),
			json(flags(h.flags_before)), h.step_index}));
	}
	return {{"steps", steps}, {"hits", hits}, {"targets_hit", p.targets_hit}, {"cursor_index", p.cursor_index}, {"hit_count", p.hit_count},
		{"truncated", p.truncated}};
}

json pair(const std::optional<rc::TracePair> &p) {
	if (!p) {
		return nullptr;
	}
	return {{"physical", path(p->physical)}, {"planned", path(p->planned)}, {"final_flags", json(flags(p->final_flags))},
		{"cursor_point_agrees", p->cursor_point_agrees}, {"shared", p->shared}};
}

} // namespace rcg::trace_json
