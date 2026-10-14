// Bindings of the solver (Stage 92-94): NHPBeam, NBeamSolverPlanItem,
// NBeamSolverEvent, NBeamSolver (trace_direction / planned / physical and the
// plan helpers), NFastSolver (the float64 walk with gains: the Dual tier) and
// NFastSolverScene / NFastSolverOut. Flat registration stands in for the
// GDScript inner classes (BeamSolver.Event -> NBeamSolverEvent, ...).
#pragma once

#include "gd_num.hpp"
#include "gd_scene.hpp"

#include <godot_cpp/core/binder_common.hpp>

class NHPBeam : public godot::RefCounted {
	GDCLASS(NHPBeam, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rcgd::BeamV beam = rc::BeamPtr<rc::F64>();

	static godot::Ref<NHPBeam> wrap(rcgd::BeamV b);
	static godot::Ref<NHPBeam> make(const godot::Ref<NCN> &p, const godot::Ref<NCN> &tau, const godot::Ref<NNum> &kappa);
	godot::Ref<NCN> get_p() const;
	godot::Ref<NCN> get_tau() const;
	godot::Ref<NNum> get_kappa() const;
	godot::Ref<NCN> at(const godot::Ref<NNum> &t) const;
	godot::Variant at_infinity() const;
	godot::Ref<NCN> tangent_at(const godot::Ref<NNum> &t) const;
	godot::Ref<NNum> param_of(const godot::Ref<NCN> &z) const;
	// HPBeam.inf_like / through / invert_point (statics) and hits / invert_at (gd_geom.cpp)
	static godot::Ref<NNum> inf_like(const godot::Ref<NNum> &x);
	static godot::Ref<NHPBeam> through(const godot::Ref<NCN> &P, const godot::Ref<NCN> &X, const godot::Variant &delta);
	static godot::Ref<NCN> invert_point(const godot::Ref<NCN> &z, const godot::Variant &circ);
	godot::Array hits(const godot::Variant &circ, bool depart_on) const;
	godot::Ref<NHPBeam> invert_at(const godot::Ref<NNum> &t, const godot::Variant &circ, const godot::Ref<NCN> &h_exact = godot::Ref<NCN>()) const;
};

class NBeamSolverPlanItem : public godot::RefCounted {
	GDCLASS(NBeamSolverPlanItem, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	int surface_id = 0;
	bool left = true;
	godot::Variant hint; // Vector2 or null

	static godot::Ref<NBeamSolverPlanItem> make(int sid, bool p_left, const godot::Variant &p_hint = godot::Variant());
	int get_surface_id() const { return surface_id; }
	void set_surface_id(int v) { surface_id = v; }
	bool get_left() const { return left; }
	void set_left(bool v) { left = v; }
	godot::Variant get_hint() const { return hint; }
	void set_hint(const godot::Variant &v) { hint = v; }
};

class NBeamSolverEvent : public godot::RefCounted {
	GDCLASS(NBeamSolverEvent, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rcgd::EventV event = rc::Event<rc::F64>{};

	template <class F>
	auto visit(F &&f) const { return std::visit(f, event); }

	static godot::Ref<NBeamSolverEvent> wrap(rcgd::EventV e);
	godot::String get_kind() const;
	int get_surface_id() const;
	bool get_left() const;
	bool get_applied() const;
	bool get_terminal() const;
	bool get_on_segment() const;
	bool get_rehit() const;
	bool get_popped() const;
	bool get_teleport() const;
	bool get_projective() const;
	int get_epoch() const;
	godot::Variant get_point() const; // NCN or null
	godot::Ref<NHPBeam> get_beam() const;
	godot::Variant get_t_from() const;
	godot::Variant get_t_to() const; // NNum or null
	double get_margin() const;
	bool get_seeded() const;
	bool get_state() const;
	godot::Dictionary get_flags() const;
	bool get_anchored() const;
	int get_beam_view() const;
	godot::String describe() const;
};

// Events of one walk at the tier of its scene, with the gains (NFastSolver).
godot::Array events_to_array(const std::vector<rc::Event<rc::F64>> &ev);
godot::Array events_to_array(const std::vector<rc::Event<rc::Dual>> &ev);
godot::Array events_to_array(const std::vector<rc::Event<rc::BigFloat>> &ev);

class NBeamSolver : public godot::RefCounted {
	GDCLASS(NBeamSolver, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	enum Mode { PHYSICAL = 0, PLANNED = 1 };
	static constexpr int MAX_HITS = rc::MAX_HITS;

	static godot::Array trace_direction(const godot::Ref<NNumCtx> &ctx, const godot::Ref<NBeamScene> &scene, const godot::Ref<NCN> &player,
			const godot::Ref<NCN> &dir, const godot::Array &plan, const godot::Variant &cursor, int mode, int max_events = MAX_HITS,
			const godot::Dictionary &flags = godot::Dictionary());
	static godot::Array planned(const godot::Ref<NNumCtx> &ctx, const godot::Ref<NBeamScene> &scene, const godot::Ref<NCN> &player,
			const godot::Ref<NCN> &cursor, const godot::Array &plan, int max_events = MAX_HITS, const godot::Dictionary &flags = godot::Dictionary());
	static godot::Array physical(const godot::Ref<NNumCtx> &ctx, const godot::Ref<NBeamScene> &scene, const godot::Ref<NCN> &player,
			const godot::Ref<NCN> &cursor, const godot::Array &plan, int max_events = MAX_HITS, const godot::Dictionary &flags = godot::Dictionary());
	static godot::Array plan_effects(const godot::Ref<NBeamScene> &scene, const godot::Array &plan, const godot::Dictionary &flags = godot::Dictionary());
	static godot::Dictionary plan_flags(const godot::Ref<NBeamScene> &scene, const godot::Array &plan, const godot::Dictionary &flags = godot::Dictionary());
	static godot::Dictionary apply_state(const godot::Dictionary &flags, const godot::Dictionary &change);
	static bool fixes_cursor(const godot::Ref<NBeamScene> &scene, const godot::Ref<NCN> &cursor, const godot::Array &plan,
			const godot::Dictionary &flags = godot::Dictionary());
	static godot::Array targets_of(const godot::Ref<NBeamScene> &scene, const godot::Array &events);
	static godot::String map_kind(const godot::Ref<NBeamSceneSurf> &surf, const godot::String &etype);
};

VARIANT_ENUM_CAST(NBeamSolver::Mode);

// FastSolver.Scene: the float64 scene (the Dual tier).
class NFastSolverScene : public godot::RefCounted {
	GDCLASS(NFastSolverScene, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Ref<NBeamScene> scene; // tier: Dual
	std::shared_ptr<rc::SolveCache<rc::Dual>> cache = std::make_shared<rc::SolveCache<rc::Dual>>();

	static godot::Ref<NFastSolverScene> from_defs(const godot::Array &defs);
	godot::Ref<NBeamScene> get_scene() const { return scene; }
};

class NFastSolverOut : public godot::RefCounted {
	GDCLASS(NFastSolverOut, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Array events;
	godot::Array gain;
	godot::Array get_events() const { return events; }
	godot::Array get_gain() const { return gain; }
};

class NFastSolver : public godot::RefCounted {
	GDCLASS(NFastSolver, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static godot::Ref<NFastSolverOut> trace_direction(const godot::Ref<NFastSolverScene> &sc, const godot::PackedFloat64Array &player,
			const godot::PackedFloat64Array &dir, const godot::Array &plan, const godot::Variant &cursor, int mode,
			int max_events = rc::MAX_HITS, const godot::Dictionary &flags = godot::Dictionary());
	static godot::Ref<NFastSolverOut> planned(const godot::Ref<NFastSolverScene> &sc, const godot::PackedFloat64Array &player,
			const godot::PackedFloat64Array &cursor, const godot::Array &plan, int max_events = rc::MAX_HITS,
			const godot::Dictionary &flags = godot::Dictionary());
	static godot::Ref<NFastSolverOut> physical(const godot::Ref<NFastSolverScene> &sc, const godot::PackedFloat64Array &player,
			const godot::PackedFloat64Array &cursor, const godot::Array &plan, int max_events = rc::MAX_HITS,
			const godot::Dictionary &flags = godot::Dictionary());
	// raw float64 math on PackedFloat64Array beams [px, py, tx, ty, k, dpx, dpy, dtx, dty, dk]
	static godot::PackedFloat64Array beam(double px, double py, double tx, double ty, double k);
	static godot::PackedFloat64Array seeded(const godot::PackedFloat64Array &b);
	static godot::PackedFloat64Array at(const godot::PackedFloat64Array &b, double t, double dt);
	static godot::PackedFloat64Array at_view(const godot::PackedFloat64Array &b, double t);
	static godot::PackedFloat64Array tangent_at(const godot::PackedFloat64Array &b, double t, double dt);
	static double param_of(const godot::PackedFloat64Array &b, double zx, double zy);
	// roots and maps on raw beams (FastSolver twins over the Dual kernel)
	static godot::PackedFloat64Array circle_hits(const godot::PackedFloat64Array &b, double cx, double cy, double r2, bool depart_on);
	static godot::PackedFloat64Array line_hits(const godot::PackedFloat64Array &b, double sx, double sy, double ex, double ey, bool depart_on);
	static godot::PackedFloat64Array surf_hits(const godot::PackedFloat64Array &b, const godot::Ref<NBeamSceneSurf> &s, bool depart_on);
	static godot::PackedFloat64Array invert_circle(const godot::PackedFloat64Array &b, double t, double dt, double cx, double cy, double r, double r2,
			bool has_h = false, double hx = 0.0, double hy = 0.0);
	static godot::PackedFloat64Array reflect_line(const godot::PackedFloat64Array &b, double t, double dt, double sx, double sy, double ex, double ey,
			bool has_h = false, double hx = 0.0, double hy = 0.0);
	static godot::PackedFloat64Array portal(const godot::PackedFloat64Array &b, double t, double dt, const godot::Ref<NBeamSceneSurf> &s,
			bool has_h = false, double hx = 0.0, double hy = 0.0);
	static godot::PackedFloat64Array project(const godot::PackedFloat64Array &b, double t, double dt, const godot::Ref<NBeamSceneSurf> &s, bool left,
			bool has_h = false, double hx = 0.0, double hy = 0.0);
	static godot::PackedFloat64Array apply_surf(const godot::PackedFloat64Array &b, double t, double dt, const godot::Ref<NBeamSceneSurf> &s,
			bool has_h = false, double hx = 0.0, double hy = 0.0);
	static bool on_segment(const godot::Ref<NBeamSceneSurf> &s, double px, double py);
	static bool side_left(const godot::Ref<NBeamSceneSurf> &s, double px, double py, double dx, double dy);
	static int endpoint_at(const godot::Ref<NBeamSceneSurf> &s, double px, double py);
	static godot::Array blocked_sides(const godot::Ref<NBeamSceneSurf> &s, int which, double dx, double dy);
};
