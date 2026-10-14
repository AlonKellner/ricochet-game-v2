#include "rc/scene.hpp"

#include "rc/error.hpp"

namespace rc {

EffectType effect_type_from_json(const std::string &name) {
	if (name.empty() || name == "pass") {
		return EffectType::Pass;
	}
	if (name == "terminal") {
		return EffectType::Terminal;
	}
	if (name == "reflection") {
		return EffectType::Reflection;
	}
	if (name == "inversion") {
		return EffectType::Inversion;
	}
	if (name == "portal") {
		return EffectType::Portal;
	}
	if (name == "line_normal_projection") {
		return EffectType::LineNormalProjection;
	}
	if (name == "circle_normal_projection") {
		return EffectType::CircleNormalProjection;
	}
	if (name == "line_directional_projection") {
		return EffectType::LineDirectionalProjection;
	}
	if (name == "circle_directional_projection") {
		return EffectType::CircleDirectionalProjection;
	}
	RC_ASSERT(false, "unknown effect type");
	return EffectType::Pass;
}

const char *effect_type_json_name(EffectType t) {
	switch (t) {
		case EffectType::Pass: return "";
		case EffectType::Terminal: return "terminal";
		case EffectType::Reflection: return "reflection";
		case EffectType::Inversion: return "inversion";
		case EffectType::Portal: return "portal";
		case EffectType::LineNormalProjection: return "line_normal_projection";
		case EffectType::CircleNormalProjection: return "circle_normal_projection";
		case EffectType::LineDirectionalProjection: return "line_directional_projection";
		case EffectType::CircleDirectionalProjection: return "circle_directional_projection";
	}
	return "";
}

bool scene_supported(const std::vector<SurfaceDef> &defs) {
	for (const SurfaceDef &d : defs) {
		if (d.resolver) {
			for (const auto &row : d.resolver->table) {
				const auto &l = row.second.first;
				const auto &r = row.second.second;
				if (!is_categorical_type(l ? l->type : EffectType::Pass) || !is_categorical_type(r ? r->type : EffectType::Pass)) {
					return false;
				}
			}
		}
		for (const std::optional<SideDef> *cfg : {&d.left, &d.right}) {
			// a portal needs its paired surface (the beam departs from it)
			if (*cfg && (*cfg)->type == EffectType::Portal && (*cfg)->pair < 0) {
				return false;
			}
		}
	}
	return true;
}

} // namespace rc
