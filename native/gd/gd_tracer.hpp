// Bindings of the tracer and the trace data types (Stage 96): NDirection,
// NRay, NTraceHit, NTraceStep, NTracePath, NBeamTracerPair, NBeamTracer. The
// tracer is an instance (it owns its caches: TDD_10, 1.4); the GDScript
// BeamTracer's static call sites move to an instance at the flip. NTraceHit
// carries the hit surface's id instead of a Segment object.
#pragma once

#include "gd_ladder.hpp"
#include "rc/tracer.hpp"

#include <godot_cpp/variant/vector2.hpp>

inline godot::Vector2 to_vec(const rc::V2 &v) { return godot::Vector2(v.x, v.y); }
inline rc::V2 to_v2(const godot::Vector2 &v) { return rc::V2{float(v.x), float(v.y)}; }

class NDirection : public godot::RefCounted {
	GDCLASS(NDirection, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Vector2 start;
	godot::Vector2 end;

	static godot::Ref<NDirection> from_coords(const godot::Vector2 &p_start, const godot::Vector2 &p_end);
	godot::Vector2 get_start() const { return start; }
	void set_start(const godot::Vector2 &v) { start = v; }
	godot::Vector2 get_end() const { return end; }
	void set_end(const godot::Vector2 &v) { end = v; }
	bool is_zero_length() const { return start == end; }
	godot::Vector2 to_vector() const { return end - start; }
	godot::Vector2 to_normalized() const;
};

class NRay : public godot::RefCounted {
	GDCLASS(NRay, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Vector2 origin;
	godot::Ref<NDirection> direction;

	static godot::Ref<NRay> from_coords(const godot::Vector2 &p_origin, const godot::Ref<NDirection> &p_direction);
	static godot::Ref<NRay> wrap(const rc::Ray &r);
	rc::Ray to_rc() const;
	godot::Vector2 get_origin() const { return origin; }
	void set_origin(const godot::Vector2 &v) { origin = v; }
	godot::Ref<NDirection> get_direction() const { return direction; }
	void set_direction(const godot::Ref<NDirection> &d) { direction = d; }
};

class NTraceHit : public godot::RefCounted {
	GDCLASS(NTraceHit, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	double t = 0.0;
	godot::Vector2 point;
	int surface_id = -1;
	int side = 0;
	bool on_segment = false;

	static godot::Ref<NTraceHit> make(double p_t, const godot::Vector2 &p_point, int p_surface_id, int p_side, bool p_on_segment);
	static godot::Ref<NTraceHit> wrap(const rc::TraceHit &h);
	rc::TraceHit to_rc() const;
	double get_t() const { return t; }
	void set_t(double v) { t = v; }
	godot::Vector2 get_point() const { return point; }
	void set_point(const godot::Vector2 &v) { point = v; }
	int get_surface_id() const { return surface_id; }
	void set_surface_id(int v) { surface_id = v; }
	int get_side() const { return side; }
	void set_side(int v) { side = v; }
	bool get_on_segment() const { return on_segment; }
	void set_on_segment(bool v) { on_segment = v; }
};

class NTraceStep : public godot::RefCounted {
	GDCLASS(NTraceStep, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static constexpr int IDENTITY_FRAME = rc::IDENTITY_FRAME;

	godot::Vector2 start;
	godot::Vector2 end;
	godot::Vector2 via;
	int frame_id = IDENTITY_FRAME;
	godot::Ref<NTraceHit> hit;
	godot::Ref<NRay> ray;
	bool is_arc_step = false;
	int type = 0;
	int surface_id = -1;
	int hit_side = -1;
	bool hit_on_segment = false;
	bool after_portal = false;
	bool ghost = false;
	double est_error = -1.0;
	int precision_bits = 0;

	static godot::Ref<NTraceStep> make(const godot::Vector2 &p_start, const godot::Vector2 &p_end, int p_frame_id = 0,
			const godot::Ref<NTraceHit> &p_hit = godot::Ref<NTraceHit>(), const godot::Ref<NRay> &p_ray = godot::Ref<NRay>(),
			const godot::Vector2 &p_via = godot::Vector2(), bool p_is_arc = false);
	static godot::Ref<NTraceStep> wrap(const rc::TraceStep &s);
	rc::TraceStep to_rc() const;
	godot::Ref<NTraceStep> with_type(int new_type) const;
	void copy_meta_from(const godot::Ref<NTraceStep> &o);

#define RC_STEP_ACCESSOR(name, type)        \
	type get_##name() const { return name; } \
	void set_##name(type v) { name = v; }
	RC_STEP_ACCESSOR(start, godot::Vector2)
	RC_STEP_ACCESSOR(end, godot::Vector2)
	RC_STEP_ACCESSOR(via, godot::Vector2)
	RC_STEP_ACCESSOR(frame_id, int)
	RC_STEP_ACCESSOR(hit, godot::Ref<NTraceHit>)
	RC_STEP_ACCESSOR(ray, godot::Ref<NRay>)
	RC_STEP_ACCESSOR(is_arc_step, bool)
	RC_STEP_ACCESSOR(type, int)
	RC_STEP_ACCESSOR(surface_id, int)
	RC_STEP_ACCESSOR(hit_side, int)
	RC_STEP_ACCESSOR(hit_on_segment, bool)
	RC_STEP_ACCESSOR(after_portal, bool)
	RC_STEP_ACCESSOR(ghost, bool)
	RC_STEP_ACCESSOR(est_error, double)
	RC_STEP_ACCESSOR(precision_bits, int)
#undef RC_STEP_ACCESSOR
};

class NTracePath : public godot::RefCounted {
	GDCLASS(NTracePath, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Array steps;
	godot::Dictionary targets_hit; // surface id -> true (TracePath.targets_hit)
	int cursor_index = -1;
	int hit_count = 0;

	static godot::Ref<NTracePath> wrap(const rc::TracePath &p);
	godot::Array get_steps() const { return steps; }
	void set_steps(const godot::Array &v) { steps = v; }
	godot::Dictionary get_targets_hit() const { return targets_hit; }
	void set_targets_hit(const godot::Dictionary &v) { targets_hit = v; }
	int get_cursor_index() const { return cursor_index; }
	void set_cursor_index(int v) { cursor_index = v; }
	int get_hit_count() const { return hit_count; }
	void set_hit_count(int v) { hit_count = v; }
};

class NBeamTracerPair : public godot::RefCounted {
	GDCLASS(NBeamTracerPair, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Ref<NTracePath> planned;
	godot::Ref<NTracePath> physical;
	godot::Ref<NPrecisionLadderResult> result;
	godot::Dictionary final_flags;

	godot::Ref<NTracePath> get_planned() const { return planned; }
	godot::Ref<NTracePath> get_physical() const { return physical; }
	godot::Ref<NPrecisionLadderResult> get_result() const { return result; }
	godot::Dictionary get_final_flags() const { return final_flags; }
};

class NBeamTracer : public godot::RefCounted {
	GDCLASS(NBeamTracer, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rc::Tracer tracer;

	static double TOL() { return rc::TRACE_TOL; }
	static bool scene_supported(const godot::Array &defs);
	static godot::Array plan_items(const godot::Array &plan_entries);

	// defs: BeamScene.defs_from_surfaces(surfaces). plan_entries: PlanManager.PlanEntry or NBeamSolverPlanItem.
	godot::Ref<NBeamTracerPair> trace_pair(const godot::Array &defs, const godot::Vector2 &player, const godot::Vector2 &cursor,
			const godot::Array &plan_entries, const godot::Dictionary &flags = godot::Dictionary());
	bool plan_refuses(const godot::Array &defs, const godot::Array &plan_entries, int surface_id, bool left,
			const godot::Dictionary &flags = godot::Dictionary());
	void clear_cache() { tracer.clear_cache(); }
	// events of one tier (limbs), their errors, the frames map (key -> {"id": n}, updated), the aim ray (or null).
	godot::Ref<NTracePath> to_path(const godot::Array &events, const godot::Array &errs, int limbs, const godot::Dictionary &frames,
			const godot::Variant &aim = godot::Variant(), const godot::Dictionary &flags = godot::Dictionary());
};
