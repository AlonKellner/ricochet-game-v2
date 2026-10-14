#include "gd_common.hpp"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace rcgd {

rc::FlagValue flag_from_variant(const godot::Variant &v) {
	switch (v.get_type()) {
		case godot::Variant::BOOL:
			return bool(v);
		case godot::Variant::INT:
			return double(int64_t(v));
		case godot::Variant::FLOAT:
			return double(v);
		default:
			return to_std(godot::String(v));
	}
}

godot::Variant flag_to_variant(const rc::FlagValue &v) {
	if (const bool *b = std::get_if<bool>(&v)) {
		return *b;
	}
	if (const double *d = std::get_if<double>(&v)) {
		return *d;
	}
	return to_gd(std::get<std::string>(v));
}

rc::Flags flags_from_dict(const godot::Dictionary &d) {
	rc::Flags f;
	const godot::Array keys = d.keys();
	for (int64_t i = 0; i < keys.size(); i++) {
		f.set(to_std(godot::String(keys[i])), flag_from_variant(d[keys[i]]));
	}
	return f;
}

godot::Dictionary flags_to_dict(const rc::Flags &f) {
	godot::Dictionary d;
	for (const auto &e : f.entries()) {
		d[to_gd(e.first)] = flag_to_variant(e.second);
	}
	return d;
}

namespace {

double num_at(const godot::Variant &arr, int64_t i) {
	const godot::Array a = arr;
	return double(a[i]);
}

std::optional<rc::StateChangeDef> state_from(const godot::Dictionary &cfg) {
	if (!cfg.has("state") || cfg["state"].get_type() == godot::Variant::NIL) {
		return std::nullopt;
	}
	const godot::Dictionary st = cfg["state"];
	rc::StateChangeDef s;
	s.key = to_std(godot::String(st["key"]));
	s.toggle = godot::String(st.get("op", "set")) == godot::String("toggle");
	s.value = st.has("value") ? flag_from_variant(st["value"]) : rc::FlagValue(true);
	return s;
}

std::optional<rc::SideDef> side_from(const godot::Variant &v) {
	if (v.get_type() == godot::Variant::NIL) {
		return std::nullopt;
	}
	const godot::Dictionary cfg = v;
	rc::SideDef s;
	s.type = rc::effect_type_from_json(to_std(godot::String(cfg.get("type", "pass"))));
	s.interactive = bool(cfg.get("interactive", false));
	s.back = bool(cfg.get("back", false));
	if (cfg.has("normal") && cfg["normal"].get_type() != godot::Variant::NIL) {
		s.has_normal = true;
		s.nx = num_at(cfg["normal"], 0);
		s.ny = num_at(cfg["normal"], 1);
	}
	if (cfg.has("theta")) {
		s.theta = double(cfg["theta"]);
	}
	if (cfg.has("d") && cfg["d"].get_type() != godot::Variant::NIL) {
		s.dx = num_at(cfg["d"], 0);
		s.dy = num_at(cfg["d"], 1);
	}
	if (cfg.has("pair") && cfg["pair"].get_type() != godot::Variant::NIL) {
		s.pair = int(int64_t(cfg["pair"]));
	}
	s.state = state_from(cfg);
	return s;
}

} // namespace

std::vector<rc::SurfaceDef> defs_from_array(const godot::Array &defs) {
	std::vector<rc::SurfaceDef> out;
	for (int64_t i = 0; i < defs.size(); i++) {
		const godot::Dictionary d = defs[i];
		rc::SurfaceDef s;
		s.id = int(int64_t(d["id"]));
		s.line = bool(d.get("line", false));
		s.sx = num_at(d["start"], 0);
		s.sy = num_at(d["start"], 1);
		s.ex = num_at(d["end"], 0);
		s.ey = num_at(d["end"], 1);
		if (d.has("via") && d["via"].get_type() != godot::Variant::NIL) {
			s.has_via = true;
			s.vx = num_at(d["via"], 0);
			s.vy = num_at(d["via"], 1);
		} else {
			s.has_via = false;
		}
		s.full = bool(d.get("full", false));
		if (d.has("circle") && d["circle"].get_type() != godot::Variant::NIL) {
			const godot::Dictionary c = d["circle"];
			s.has_circle = true;
			s.cx = num_at(c["center"], 0);
			s.cy = num_at(c["center"], 1);
			s.radius = double(c["radius"]);
		}
		s.left = side_from(d.get("left", godot::Variant()));
		s.right = side_from(d.get("right", godot::Variant()));
		s.is_target = bool(d.get("is_target", false));
		if (d.has("resolver") && d["resolver"].get_type() != godot::Variant::NIL) {
			const godot::Dictionary r = d["resolver"];
			rc::ResolverDef rd;
			rd.key = to_std(godot::String(r["key"]));
			const godot::Dictionary table = r["table"];
			const godot::Array keys = table.keys();
			for (int64_t k = 0; k < keys.size(); k++) {
				const godot::Dictionary entry = table[keys[k]];
				rd.table.emplace_back(to_std(godot::String(keys[k])),
					std::make_pair(side_from(entry.get("left", godot::Variant())), side_from(entry.get("right", godot::Variant()))));
			}
			s.resolver = rd;
		}
		out.push_back(std::move(s));
	}
	return out;
}

std::vector<rc::PlanItem> plan_from_items(const godot::Array &items) {
	std::vector<rc::PlanItem> out;
	for (int64_t i = 0; i < items.size(); i++) {
		const godot::Variant it = items[i];
		rc::PlanItem p;
		godot::Object *obj = it;
		if (obj == nullptr) {
			continue;
		}
		p.surface_id = int(int64_t(obj->get("surface_id")));
		const godot::Variant left = obj->get("left");
		if (left.get_type() == godot::Variant::BOOL) {
			p.left = bool(left);
		} else {
			// PlanManager.PlanEntry: side (Side.Value: LEFT = 0)
			p.left = int64_t(obj->get("side")) == 0;
		}
		godot::Variant hint = obj->get("hint");
		if (hint.get_type() == godot::Variant::NIL) {
			hint = obj->get("click");
		}
		if (hint.get_type() == godot::Variant::VECTOR2) {
			const godot::Vector2 c = hint;
			if (std::isfinite(c.x) && std::isfinite(c.y)) {
				p.hint = rc::effects::F32Point{float(c.x), float(c.y)};
			}
		}
		out.push_back(p);
	}
	return out;
}

} // namespace rcgd
