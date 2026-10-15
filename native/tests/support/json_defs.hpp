// Scene defs from the JSON the game dumps / the oracle traces (test support).
#pragma once
#include "rc/scene.hpp"
#include "rc/solver.hpp"
#include <nlohmann/json.hpp>
using ojson = nlohmann::ordered_json;
namespace rc {
inline FlagValue flag_from_json(const ojson &v) {
	if (v.is_boolean()) return v.get<bool>();
	if (v.is_number()) return v.get<double>();
	return v.get<std::string>();
}
inline Flags flags_from_json(const ojson &j) {
	Flags f;
	if (j.is_object()) for (auto it = j.begin(); it != j.end(); ++it) f.set(it.key(), flag_from_json(it.value()));
	return f;
}
inline std::optional<SideDef> side_from_json(const ojson &j) {
	if (j.is_null()) return std::nullopt;
	SideDef s;
	s.type = effect_type_from_json(j.value("type", std::string("pass")));
	s.interactive = j.value("interactive", false);
	s.back = j.value("back", false);
	if (j.contains("normal") && !j["normal"].is_null()) { s.has_normal = true; s.nx = j["normal"][0].get<double>(); s.ny = j["normal"][1].get<double>(); }
	if (j.contains("theta")) s.theta = j["theta"].get<double>();
	if (j.contains("d")) { s.dx = j["d"][0].get<double>(); s.dy = j["d"][1].get<double>(); }
	if (j.contains("pair") && !j["pair"].is_null()) s.pair = j["pair"].get<int>();
	if (j.contains("state") && !j["state"].is_null()) {
		const ojson &st = j["state"];
		s.state = StateChangeDef{st["key"].get<std::string>(), st.value("op", std::string("set")) == "toggle", flag_from_json(st["value"])};
	}
	return s;
}
inline std::vector<SurfaceDef> defs_from_json(const ojson &arr) {
	std::vector<SurfaceDef> out;
	for (const ojson &d : arr) {
		SurfaceDef s;
		s.id = d["id"].get<int>();
		s.line = d.value("line", false);
		s.sx = d["start"][0].get<double>(); s.sy = d["start"][1].get<double>();
		s.ex = d["end"][0].get<double>(); s.ey = d["end"][1].get<double>();
		if (d.contains("via") && !d["via"].is_null()) { s.has_via = true; s.vx = d["via"][0].get<double>(); s.vy = d["via"][1].get<double>(); } else s.has_via = false;
		s.full = d.value("full", false);
		if (d.contains("circle") && !d["circle"].is_null()) { s.has_circle = true; s.cx = d["circle"]["center"][0].get<double>(); s.cy = d["circle"]["center"][1].get<double>(); s.radius = d["circle"]["radius"].get<double>(); }
		s.left = side_from_json(d.contains("left") ? d["left"] : ojson());
		s.right = side_from_json(d.contains("right") ? d["right"] : ojson());
		s.is_target = d.value("is_target", false);
		s.pre_transform = d.value("pre_transform", false);
		if (d.contains("resolver") && !d["resolver"].is_null()) {
			ResolverDef r; r.key = d["resolver"]["key"].get<std::string>();
			for (auto it = d["resolver"]["table"].begin(); it != d["resolver"]["table"].end(); ++it)
				r.table.emplace_back(it.key(), std::make_pair(side_from_json(it.value().contains("left") ? it.value()["left"] : ojson()), side_from_json(it.value().contains("right") ? it.value()["right"] : ojson())));
			s.resolver = r;
		}
		out.push_back(s);
	}
	return out;
}
inline std::vector<PlanItem> plan_from_json(const ojson &arr) {
	std::vector<PlanItem> out;
	for (const ojson &e : arr) {
		PlanItem p; p.surface_id = e["surface_id"].get<int>(); p.left = e["side"].get<std::string>() == "LEFT";
		if (e.contains("click") && !e["click"].is_null()) p.hint = effects::F32Point{float(e["click"][0].get<double>()), float(e["click"][1].get<double>())};
		out.push_back(p);
	}
	return out;
}
}

namespace rc {
inline double from_hex_str(const std::string &h) {
	unsigned char b[8];
	for (int i = 0; i < 8; i++) b[i] = (unsigned char)std::stoul(h.substr(size_t(i) * 2, 2), nullptr, 16);
	double d; std::memcpy(&d, b, 8); return d;
}
// Defs from the JSON structure with every float taken from the goldens'
// "inputs" (the bit patterns GDScript parsed: Godot's decimal parsing is not
// correctly rounded).
inline std::vector<SurfaceDef> defs_from_json_hex(const ojson &arr, const ojson &inputs) {
	std::vector<SurfaceDef> out = defs_from_json(arr);
	const ojson &hs = inputs["surfaces"];
	for (size_t i = 0; i < out.size() && i < hs.size(); i++) {
		const ojson &h = hs[i]; SurfaceDef &s = out[i];
		s.sx = from_hex_str(h["start"][0]); s.sy = from_hex_str(h["start"][1]); s.ex = from_hex_str(h["end"][0]); s.ey = from_hex_str(h["end"][1]);
		if (h.contains("via")) { s.vx = from_hex_str(h["via"][0]); s.vy = from_hex_str(h["via"][1]); }
		if (h.contains("circle")) { s.cx = from_hex_str(h["circle"][0]); s.cy = from_hex_str(h["circle"][1]); s.radius = from_hex_str(h["circle"][2]); }
		for (int side = 0; side < 2; side++) {
			const char *key = side == 0 ? "left" : "right"; std::optional<SideDef> &sd = side == 0 ? s.left : s.right;
			if (!h.contains(key) || !sd) continue;
			const ojson &hh = h[key];
			if (hh.contains("normal")) { sd->nx = from_hex_str(hh["normal"][0]); sd->ny = from_hex_str(hh["normal"][1]); }
			if (hh.contains("theta")) sd->theta = from_hex_str(hh["theta"]);
			if (hh.contains("d")) { sd->dx = from_hex_str(hh["d"][0]); sd->dy = from_hex_str(hh["d"][1]); }
			if (hh.contains("state_value") && sd->state) sd->state->value = from_hex_str(hh["state_value"]);
		}
	}
	return out;
}
}
