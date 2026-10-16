// Bindings of the solver (Stage 92-94): HPBeam, BeamSolverPlanItem,
// BeamSolverEvent, BeamSolver (trace_direction / planned / physical and the
// plan helpers), FastSolver (the float64 walk with gains: the Dual tier) and
// FastSolverScene / FastSolverOut. Flat registration stands in for the
// GDScript inner classes (BeamSolver.Event -> BeamSolverEvent, ...).
#pragma once

#include "gd_num.hpp"
#include "gd_scene.hpp"

#include <godot_cpp/core/binder_common.hpp>

class HPBeam : public godot::RefCounted {
	GDCLASS(HPBeam, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rcgd::BeamV beam = rc::BeamPtr<rc::F64>();

	static godot::Ref<HPBeam> wrap(rcgd::BeamV b);
	static godot::Ref<HPBeam> make(const godot::Ref<CN> &p, const godot::Ref<CN> &tau, const godot::Ref<Num> &kappa);
	godot::Ref<CN> get_p() const;
	godot::Ref<CN> get_tau() const;
	godot::Ref<Num> get_kappa() const;
	godot::Ref<CN> at(const godot::Ref<Num> &t) const;
	godot::Variant at_infinity() const;
	godot::Ref<CN> tangent_at(const godot::Ref<Num> &t) const;
	godot::Ref<Num> param_of(const godot::Ref<CN> &z) const;
	// HPBeam.inf_like / through / invert_point (statics) and hits / invert_at (gd_geom.cpp)
	static godot::Ref<Num> inf_like(const godot::Ref<Num> &x);
	static godot::Ref<HPBeam> through(const godot::Ref<CN> &P, const godot::Ref<CN> &X, const godot::Variant &delta);
	static godot::Ref<CN> invert_point(const godot::Ref<CN> &z, const godot::Variant &circ);
	godot::Array hits(const godot::Variant &circ, bool depart_on) const;
	godot::Ref<HPBeam> invert_at(const godot::Ref<Num> &t, const godot::Variant &circ, const godot::Ref<CN> &h_exact = godot::Ref<CN>()) const;
};

class BeamSolverPlanItem : public godot::RefCounted {
	GDCLASS(BeamSolverPlanItem, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	int surface_id = 0;
	bool left = true;
	godot::Variant hint; // Vector2 or null

	static godot::Ref<BeamSolverPlanItem> make(int sid, bool p_left, const godot::Variant &p_hint = godot::Variant());
	int get_surface_id() const { return surface_id; }
	void set_surface_id(int v) { surface_id = v; }
	bool get_left() const { return left; }
	void set_left(bool v) { left = v; }
	godot::Variant get_hint() const { return hint; }
	void set_hint(const godot::Variant &v) { hint = v; }
};

class BeamSolverEvent : public godot::RefCounted {
	GDCLASS(BeamSolverEvent, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rcgd::EventV event = rc::Event<rc::F64>{};
	// The HPBeam object of `beam`: events of one walk on the same beam share it
	// (GDScript identity: a popped frame restores the earlier beam object).
	mutable godot::Ref<HPBeam> beam_obj;

	template <class F>
	auto visit(F &&f) const { return std::visit(f, event); }

	static godot::Ref<BeamSolverEvent> wrap(rcgd::EventV e);
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
	godot::Variant get_point() const; // CN or null
	void set_point(const godot::Variant &v);
	godot::Ref<HPBeam> get_beam() const;
	void set_beam(const godot::Ref<HPBeam> &b);
	godot::Variant get_t_from() const;
	godot::Variant get_t_to() const; // Num or null
	double get_margin() const;
	bool get_seeded() const;
	bool get_state() const;
	bool get_fired() const;
	godot::Dictionary get_flags() const;
	bool get_anchored() const;
	int get_beam_view() const;
	godot::String describe() const;
};

// Events of one walk at the tier of its scene, with the gains (FastSolver).
godot::Array events_to_array(const std::vector<rc::Event<rc::F64>> &ev);
godot::Array events_to_array(const std::vector<rc::Event<rc::Dual>> &ev);
godot::Array events_to_array(const std::vector<rc::Event<rc::BigFloat>> &ev);

// BeamSolver.AnchorChain: the anchored beams of a plan and the back-propagated
// hit points of its projective entries (null when the construction is degenerate).
class BeamSolverAnchorChain : public godot::RefCounted {
	GDCLASS(BeamSolverAnchorChain, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Array beams;     // HPBeam
	godot::Dictionary hits; // plan entry index -> CN
	godot::Array get_beams() const { return beams; }
	godot::Dictionary get_hits() const { return hits; }
};

class BeamSolver : public godot::RefCounted {
	GDCLASS(BeamSolver, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	enum Mode { PHYSICAL = 0, PLANNED = 1 };
	static constexpr int MAX_HITS = rc::MAX_HITS;
	static constexpr int INFINITY_ID = rc::INFINITY_ID;
	static godot::Array TRANSFORMATIVE() { return rcgd::effect_names_where(&rc::is_transformative); }
	static godot::Array PROJECTIVE() { return rcgd::effect_names_where(&rc::is_projective); }

	static godot::Array trace_direction(const godot::Ref<NumCtx> &ctx, const godot::Ref<BeamScene> &scene, const godot::Ref<CN> &player,
			const godot::Ref<CN> &dir, const godot::Array &plan, const godot::Variant &cursor, int mode, int max_events = MAX_HITS,
			const godot::Dictionary &flags = godot::Dictionary());
	static godot::Array planned(const godot::Ref<NumCtx> &ctx, const godot::Ref<BeamScene> &scene, const godot::Ref<CN> &player,
			const godot::Ref<CN> &cursor, const godot::Array &plan, int max_events = MAX_HITS, const godot::Dictionary &flags = godot::Dictionary());
	static godot::Array physical(const godot::Ref<NumCtx> &ctx, const godot::Ref<BeamScene> &scene, const godot::Ref<CN> &player,
			const godot::Ref<CN> &cursor, const godot::Array &plan, int max_events = MAX_HITS, const godot::Dictionary &flags = godot::Dictionary());
	static godot::Array plan_effects(const godot::Ref<BeamScene> &scene, const godot::Array &plan, const godot::Dictionary &flags = godot::Dictionary());
	static godot::Dictionary plan_flags(const godot::Ref<BeamScene> &scene, const godot::Array &plan, const godot::Dictionary &flags = godot::Dictionary());
	// Stage 101: the frame's image of infinity after `plan` (a CN, or null for infinity)
	static godot::Variant plan_pole(const godot::Ref<BeamScene> &scene, const godot::Array &plan, const godot::Dictionary &flags = godot::Dictionary());
	static godot::Dictionary apply_state(const godot::Dictionary &flags, const godot::Dictionary &change);
	static bool fixes_cursor(const godot::Ref<BeamScene> &scene, const godot::Ref<CN> &cursor, const godot::Array &plan,
			const godot::Dictionary &flags = godot::Dictionary());
	static godot::Array targets_of(const godot::Ref<BeamScene> &scene, const godot::Array &events);
	static godot::String map_kind(const godot::Ref<BeamSceneSurf> &surf, const godot::String &etype);
	static godot::Ref<BeamSolverAnchorChain> anchor_chain(const godot::Ref<BeamScene> &scene, const godot::Ref<CN> &player,
			const godot::Ref<CN> &cursor, const godot::Array &plan, const godot::Dictionary &flags = godot::Dictionary());
};

VARIANT_ENUM_CAST(BeamSolver::Mode);

// FastSolver.Scene: the float64 scene (the Dual tier).
class FastSolverScene : public godot::RefCounted {
	GDCLASS(FastSolverScene, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Ref<BeamScene> scene; // tier: Dual
	std::shared_ptr<rc::SolveCache<rc::Dual>> cache = std::make_shared<rc::SolveCache<rc::Dual>>();

	static godot::Ref<FastSolverScene> from_defs(const godot::Array &defs);
	godot::Ref<BeamScene> get_scene() const { return scene; }
};

class FastSolverOut : public godot::RefCounted {
	GDCLASS(FastSolverOut, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Array events;
	godot::Array gain;
	godot::Array get_events() const { return events; }
	godot::Array get_gain() const { return gain; }
};

class FastSolver : public godot::RefCounted {
	GDCLASS(FastSolver, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static godot::Ref<FastSolverOut> trace_direction(const godot::Ref<FastSolverScene> &sc, const godot::PackedFloat64Array &player,
			const godot::PackedFloat64Array &dir, const godot::Array &plan, const godot::Variant &cursor, int mode,
			int max_events = rc::MAX_HITS, const godot::Dictionary &flags = godot::Dictionary());
	static godot::Ref<FastSolverOut> planned(const godot::Ref<FastSolverScene> &sc, const godot::PackedFloat64Array &player,
			const godot::PackedFloat64Array &cursor, const godot::Array &plan, int max_events = rc::MAX_HITS,
			const godot::Dictionary &flags = godot::Dictionary());
	static godot::Ref<FastSolverOut> physical(const godot::Ref<FastSolverScene> &sc, const godot::PackedFloat64Array &player,
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
	static godot::PackedFloat64Array surf_hits(const godot::PackedFloat64Array &b, const godot::Ref<BeamSceneSurf> &s, bool depart_on);
	static godot::PackedFloat64Array invert_circle(const godot::PackedFloat64Array &b, double t, double dt, double cx, double cy, double r, double r2,
			bool has_h = false, double hx = 0.0, double hy = 0.0);
	static godot::PackedFloat64Array reflect_line(const godot::PackedFloat64Array &b, double t, double dt, double sx, double sy, double ex, double ey,
			bool has_h = false, double hx = 0.0, double hy = 0.0);
	static godot::PackedFloat64Array portal(const godot::PackedFloat64Array &b, double t, double dt, const godot::Ref<BeamSceneSurf> &s,
			bool has_h = false, double hx = 0.0, double hy = 0.0);
	static godot::PackedFloat64Array project(const godot::PackedFloat64Array &b, double t, double dt, const godot::Ref<BeamSceneSurf> &s, bool left,
			bool has_h = false, double hx = 0.0, double hy = 0.0);
	static godot::PackedFloat64Array apply_surf(const godot::PackedFloat64Array &b, double t, double dt, const godot::Ref<BeamSceneSurf> &s,
			bool has_h = false, double hx = 0.0, double hy = 0.0);
	static bool on_segment(const godot::Ref<BeamSceneSurf> &s, double px, double py);
	static bool side_left(const godot::Ref<BeamSceneSurf> &s, double px, double py, double dx, double dy);
	static int endpoint_at(const godot::Ref<BeamSceneSurf> &s, double px, double py);
	static godot::Array blocked_sides(const godot::Ref<BeamSceneSurf> &s, int which, double dx, double dy);
};
