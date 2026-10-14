// Bindings of the scene (Stage 93): NBeamScene.from_defs(ctx, defs) with its
// surfaces (NBeamSceneSurf: the raw float64 geometry, effect resolution under
// flags, ghost mode) and NBeamScene.supported(defs).
#pragma once

#include "gd_common.hpp"
#include "gd_num.hpp"

class NBeamGeomSeg;

class NBeamSceneSurf : public godot::RefCounted {
	GDCLASS(NBeamSceneSurf, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// the owning scene keeps the Surf alive; the tier is the scene's
	rcgd::SceneV scene;
	int surface_id = 0;

	template <class F>
	auto visit(F &&f) const {
		return std::visit([&](const auto &sc) { return f(*sc->find(surface_id)); }, scene);
	}

	int get_id() const { return surface_id; }
	bool get_line() const;
	bool get_full() const;
	double get_sx() const;
	double get_sy() const;
	double get_ex() const;
	double get_ey() const;
	double get_cx() const;
	double get_cy() const;
	double get_r() const;
	double get_r2() const;
	bool get_portal() const;
	int get_pair() const;
	double get_theta() const;
	double get_dx() const;
	double get_dy() const;
	double get_pa() const;
	double get_pb() const;
	bool get_has_v() const;
	double get_vx() const;
	double get_vy() const;
	bool get_is_target() const;
	godot::String get_left_type() const;
	godot::String get_right_type() const;
	godot::Array get_left_proj() const;
	godot::Array get_right_proj() const;
	godot::Ref<NCN> get_alpha() const;
	godot::Ref<NCN> get_dvec() const;
	godot::Ref<NBeamGeomSeg> get_seg() const; // gd_geom.cpp
	godot::String effect(bool left, const godot::Dictionary &flags = godot::Dictionary()) const;
	godot::Variant state_change(bool left, const godot::Dictionary &flags = godot::Dictionary()) const;
	bool ghosted(bool left, const godot::Dictionary &flags) const;
};

class NBeamScene : public godot::RefCounted {
	GDCLASS(NBeamScene, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rcgd::SceneV scene;
	int limbs = 0;
	godot::Array surfaces_; // NBeamSceneSurf objects
	godot::Dictionary by_id_;

	static godot::Ref<NBeamScene> from_defs(const godot::Ref<NNumCtx> &ctx, const godot::Array &defs);
	static bool supported(const godot::Array &defs);
	godot::Array get_surfaces() const { return surfaces_; }
	godot::Dictionary get_by_id() const { return by_id_; }
	int get_limbs() const { return limbs; }

	static constexpr const char *GHOST = "ghost";
};
