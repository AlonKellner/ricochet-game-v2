#include "gd_scene.hpp"
#include "gd_geom.hpp"

#include <godot_cpp/core/class_db.hpp>

using godot::ClassDB;
using godot::D_METHOD;
using godot::PropertyInfo;
using godot::Ref;
using godot::Variant;
using namespace rcgd;

// ---------------------------------------------------------------- BeamSceneSurf

#define RC_SURF_GETTER(name, type, expr)                 \
	type BeamSceneSurf::get_##name() const {            \
		return visit([](const auto &s) { return type(expr); }); \
	}

RC_SURF_GETTER(line, bool, s.line)
RC_SURF_GETTER(full, bool, s.full)
RC_SURF_GETTER(sx, double, s.sx)
RC_SURF_GETTER(sy, double, s.sy)
RC_SURF_GETTER(ex, double, s.ex)
RC_SURF_GETTER(ey, double, s.ey)
RC_SURF_GETTER(cx, double, s.cx)
RC_SURF_GETTER(cy, double, s.cy)
RC_SURF_GETTER(r, double, s.r)
RC_SURF_GETTER(r2, double, s.r2)
RC_SURF_GETTER(portal, bool, s.portal)
RC_SURF_GETTER(pair, int, s.pair)
RC_SURF_GETTER(theta, double, s.theta)
RC_SURF_GETTER(dx, double, s.dx)
RC_SURF_GETTER(dy, double, s.dy)
RC_SURF_GETTER(pa, double, s.pa)
RC_SURF_GETTER(pb, double, s.pb)
RC_SURF_GETTER(has_v, bool, s.has_v)
RC_SURF_GETTER(vx, double, s.vx)
RC_SURF_GETTER(vy, double, s.vy)
RC_SURF_GETTER(is_target, bool, s.is_target)

godot::String BeamSceneSurf::get_left_type() const {
	return visit([](const auto &s) { return effect_name(s.left_type); });
}
godot::String BeamSceneSurf::get_right_type() const {
	return visit([](const auto &s) { return effect_name(s.right_type); });
}
namespace {
godot::Array proj_array(const rc::Projective &p, bool present) {
	godot::Array a;
	if (!present) {
		return a;
	}
	a.push_back(p.back);
	a.push_back(p.nx);
	a.push_back(p.ny);
	return a;
}
} // namespace
godot::Array BeamSceneSurf::get_left_proj() const {
	return visit([](const auto &s) { return proj_array(s.left_proj, rc::is_projective(s.left_type)); });
}
godot::Array BeamSceneSurf::get_right_proj() const {
	return visit([](const auto &s) { return proj_array(s.right_proj, rc::is_projective(s.right_type)); });
}
// alpha / dvec: the portal map's rotation and translation; null unless a portal (BeamScene.Surf).
Ref<CN> BeamSceneSurf::get_alpha() const {
	return visit([](const auto &s) { return s.portal ? CN::wrap(s.alpha) : Ref<CN>(); });
}
Ref<CN> BeamSceneSurf::get_dvec() const {
	return visit([](const auto &s) { return s.portal ? CN::wrap(s.dvec) : Ref<CN>(); });
}
godot::String BeamSceneSurf::effect(bool left, const godot::Dictionary &flags) const {
	const rc::Flags f = flags_from_dict(flags);
	return visit([&](const auto &s) { return effect_name(s.effect(left, f)); });
}
Variant BeamSceneSurf::state_change(bool left, const godot::Dictionary &flags) const {
	const rc::Flags f = flags_from_dict(flags);
	return visit([&](const auto &s) -> Variant {
		const rc::StateChangeDef *ch = s.state_change(left, f);
		if (ch == nullptr) {
			return Variant();
		}
		godot::Dictionary d;
		d["key"] = to_gd(ch->key);
		d["op"] = ch->toggle ? "toggle" : "set";
		d["value"] = flag_to_variant(ch->value);
		return d;
	});
}
bool BeamSceneSurf::ghosted(bool left, const godot::Dictionary &flags) const {
	const rc::Flags f = flags_from_dict(flags);
	return visit([&](const auto &s) { return s.ghosted(left, f); });
}

void BeamSceneSurf::_bind_methods() {
#define RC_SURF_PROP(name, vtype)                                                              \
	ClassDB::bind_method(D_METHOD("get_" #name), &BeamSceneSurf::get_##name);                 \
	ADD_PROPERTY(PropertyInfo(Variant::vtype, #name), "", "get_" #name);
	RC_SURF_PROP(id, INT)
	RC_SURF_PROP(line, BOOL)
	RC_SURF_PROP(full, BOOL)
	RC_SURF_PROP(sx, FLOAT)
	RC_SURF_PROP(sy, FLOAT)
	RC_SURF_PROP(ex, FLOAT)
	RC_SURF_PROP(ey, FLOAT)
	RC_SURF_PROP(cx, FLOAT)
	RC_SURF_PROP(cy, FLOAT)
	RC_SURF_PROP(r, FLOAT)
	RC_SURF_PROP(r2, FLOAT)
	RC_SURF_PROP(portal, BOOL)
	RC_SURF_PROP(pair, INT)
	RC_SURF_PROP(theta, FLOAT)
	RC_SURF_PROP(dx, FLOAT)
	RC_SURF_PROP(dy, FLOAT)
	RC_SURF_PROP(pa, FLOAT)
	RC_SURF_PROP(pb, FLOAT)
	RC_SURF_PROP(has_v, BOOL)
	RC_SURF_PROP(vx, FLOAT)
	RC_SURF_PROP(vy, FLOAT)
	RC_SURF_PROP(is_target, BOOL)
	RC_SURF_PROP(left_type, STRING)
	RC_SURF_PROP(right_type, STRING)
	RC_SURF_PROP(left_proj, ARRAY)
	RC_SURF_PROP(right_proj, ARRAY)
#undef RC_SURF_PROP
	ClassDB::bind_method(D_METHOD("get_alpha"), &BeamSceneSurf::get_alpha);
	ClassDB::bind_method(D_METHOD("get_dvec"), &BeamSceneSurf::get_dvec);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "alpha", godot::PROPERTY_HINT_RESOURCE_TYPE, "CN", godot::PROPERTY_USAGE_NONE), "", "get_alpha");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "dvec", godot::PROPERTY_HINT_RESOURCE_TYPE, "CN", godot::PROPERTY_USAGE_NONE), "", "get_dvec");
	ClassDB::bind_method(D_METHOD("get_seg"), &BeamSceneSurf::get_seg);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "seg", godot::PROPERTY_HINT_RESOURCE_TYPE, "BeamGeomSeg", godot::PROPERTY_USAGE_NONE), "", "get_seg");
	ClassDB::bind_method(D_METHOD("effect", "left", "flags"), &BeamSceneSurf::effect, DEFVAL(godot::Dictionary()));
	ClassDB::bind_method(D_METHOD("state_change", "left", "flags"), &BeamSceneSurf::state_change, DEFVAL(godot::Dictionary()));
	ClassDB::bind_method(D_METHOD("ghosted", "left", "flags"), &BeamSceneSurf::ghosted);
}

// ---------------------------------------------------------------- BeamScene

void BeamScene::_bind_methods() {
	ClassDB::bind_static_method("BeamScene", D_METHOD("from_defs", "ctx", "defs"), &BeamScene::from_defs);
	ClassDB::bind_static_method("BeamScene", D_METHOD("supported", "defs"), &BeamScene::supported);
	ClassDB::bind_static_method("BeamScene", D_METHOD("GHOST"), &BeamScene::GHOST);
	ClassDB::bind_static_method("BeamScene", D_METHOD("TRANSFORMATIVE"), &BeamScene::TRANSFORMATIVE);
	ClassDB::bind_static_method("BeamScene", D_METHOD("PROJECTIVE"), &BeamScene::PROJECTIVE);
	ClassDB::bind_method(D_METHOD("get_surfaces"), &BeamScene::get_surfaces);
	ClassDB::bind_method(D_METHOD("get_by_id"), &BeamScene::get_by_id);
	ClassDB::bind_method(D_METHOD("get_limbs"), &BeamScene::get_limbs);
	ClassDB::bind_method(D_METHOD("set_emit_infinity", "on"), &BeamScene::set_emit_infinity);
	ClassDB::bind_method(D_METHOD("get_emit_infinity"), &BeamScene::get_emit_infinity);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "emit_infinity"), "set_emit_infinity", "get_emit_infinity");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "surfaces"), "", "get_surfaces");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "by_id"), "", "get_by_id");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "limbs"), "", "get_limbs");
}

void BeamScene::set_emit_infinity(bool on) {
	std::visit([on](auto &s) {
		if (s) {
			s->emit_infinity = on;
		}
	}, scene);
}

bool BeamScene::get_emit_infinity() const {
	return std::visit([](const auto &s) { return s ? s->emit_infinity : false; }, scene);
}

Ref<BeamScene> BeamScene::from_defs(const Ref<NumCtx> &ctx, const godot::Array &defs) {
	Ref<BeamScene> sc;
	sc.instantiate();
	const std::vector<rc::SurfaceDef> d = defs_from_array(defs);
	sc->limbs = ctx->limbs;
	std::vector<int> ids;
	if (ctx->limbs == 0) {
		auto s = std::make_shared<rc::Scene<rc::F64>>(rc::Scene<rc::F64>::from_defs(d, rc::F64{1.0}));
		for (const auto &surf : s->surfaces) {
			ids.push_back(surf->id);
		}
		sc->scene = s;
	} else if (ctx->limbs == DUAL_LIMBS) {
		auto s = std::make_shared<rc::Scene<rc::Dual>>(rc::Scene<rc::Dual>::from_defs(d, rc::Dual{1.0, 0.0}));
		for (const auto &surf : s->surfaces) {
			ids.push_back(surf->id);
		}
		sc->scene = s;
	} else {
		auto s = std::make_shared<rc::Scene<rc::BigFloat>>(rc::Scene<rc::BigFloat>::from_defs(d, rc::BigFloat::from_int(1, ctx->limbs)));
		for (const auto &surf : s->surfaces) {
			ids.push_back(surf->id);
		}
		sc->scene = s;
	}
	for (int id : ids) {
		Ref<BeamSceneSurf> w;
		w.instantiate();
		w->scene = sc->scene;
		w->surface_id = id;
		sc->surfaces_.push_back(w);
		sc->by_id_[id] = w;
	}
	return sc;
}

bool BeamScene::supported(const godot::Array &defs) {
	return rc::scene_supported(defs_from_array(defs));
}
