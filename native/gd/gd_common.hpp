// Shared helpers of the bindings (Stage 90-96): the precision tier as a
// runtime variant, Variant <-> core conversions (scene defs Dictionaries,
// flags Dictionaries, plan items), and small string helpers. The bindings
// register under N-prefixed names for the dual-implementation period; the
// GDScript classes keep their names until each class flips (TDD_10, 1.6).
#pragma once

#include "rc/scene.hpp"
#include "rc/solver.hpp"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <memory>
#include <optional>
#include <variant>

namespace rcgd {

// A value of one tier. limbs 0 = F64, -1 = Dual, n = BigFloat with n limbs (NumCtx).
constexpr int DUAL_LIMBS = -1;

using NumV = std::variant<rc::F64, rc::Dual, rc::BigFloat>;
using CNV = std::variant<rc::CN<rc::F64>, rc::CN<rc::Dual>, rc::CN<rc::BigFloat>>;
using BeamV = std::variant<rc::BeamPtr<rc::F64>, rc::BeamPtr<rc::Dual>, rc::BeamPtr<rc::BigFloat>>;
using SceneV = std::variant<std::shared_ptr<rc::Scene<rc::F64>>, std::shared_ptr<rc::Scene<rc::Dual>>, std::shared_ptr<rc::Scene<rc::BigFloat>>>;
using EventV = std::variant<rc::Event<rc::F64>, rc::Event<rc::Dual>, rc::Event<rc::BigFloat>>;

inline int tier_index(int limbs) { return limbs == 0 ? 0 : (limbs == DUAL_LIMBS ? 1 : 2); }

// A number of the tier from a float64 value.
inline NumV num_of(int limbs, double f) {
	if (limbs == 0) {
		return rc::F64{f};
	}
	if (limbs == DUAL_LIMBS) {
		return rc::Dual{f, 0.0};
	}
	return rc::BigFloat::from_float(f, limbs);
}

inline int limbs_of(const NumV &n) {
	if (std::holds_alternative<rc::F64>(n)) {
		return 0;
	}
	if (std::holds_alternative<rc::Dual>(n)) {
		return DUAL_LIMBS;
	}
	return int(std::get<rc::BigFloat>(n).limbs.size());
}

inline double to_float(const NumV &n) {
	return std::visit([](const auto &x) { return x.to_float(); }, n);
}

// ---- strings

inline std::string to_std(const godot::String &s) {
	return std::string(s.utf8().get_data());
}
inline godot::String to_gd(const std::string &s) {
	return godot::String::utf8(s.c_str());
}

// ---- flags

rc::FlagValue flag_from_variant(const godot::Variant &v);
godot::Variant flag_to_variant(const rc::FlagValue &v);
rc::Flags flags_from_dict(const godot::Dictionary &d);
godot::Dictionary flags_to_dict(const rc::Flags &f);

// ---- scene defs (the JSON-able Dictionaries of BeamScene.defs_from_surfaces)

std::vector<rc::SurfaceDef> defs_from_array(const godot::Array &defs);

// ---- plan items: NBeamSolverPlanItem objects, or PlanManager.PlanEntry-like objects
// (surface_id, side, click) in trace_pair's plan_entries.
std::vector<rc::PlanItem> plan_from_items(const godot::Array &items);

// ---- effect names
inline godot::String effect_name(rc::EffectType t) {
	return godot::String(rc::effect_type_json_name(t));
}

} // namespace rcgd
