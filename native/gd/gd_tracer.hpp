// Bindings of the tracer and the trace data types (Stage 96): Point, Direction,
// Ray, TraceHit, TraceStep, TracePath, BeamTracerPair, BeamTracer. The data
// classes keep the GDScript shapes (a Point carries `coords`; a TraceHit's
// `segment` is the game Segment the caller maps to the hit's surface id), so
// the visual and game layers consume them unchanged. Construction goes through
// `make` / `from_coords` / `at`: GDExtension classes take no constructor
// arguments. The tracer is an instance that owns its caches (TDD_10, 1.4).
#pragma once

#include "gd_ladder.hpp"
#include "rc/tracer.hpp"

#include <godot_cpp/variant/vector2.hpp>

inline godot::Vector2 to_vec(const rc::V2 &v) { return godot::Vector2(v.x, v.y); }
inline rc::V2 to_v2(const godot::Vector2 &v) { return rc::V2{float(v.x), float(v.y)}; }

class Point : public godot::RefCounted {
	GDCLASS(Point, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Vector2 coords;

	static godot::Ref<Point> at(const godot::Vector2 &position);
	godot::Vector2 get_coords() const { return coords; }
	void set_coords(const godot::Vector2 &v) { coords = v; }
	bool same_position(const godot::Ref<Point> &other) const { return other.is_valid() && coords == other->coords; }
};

class Direction : public godot::RefCounted {
	GDCLASS(Direction, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Ref<Point> start;
	godot::Ref<Point> end;

	static godot::Ref<Direction> make(const godot::Ref<Point> &p_start, const godot::Ref<Point> &p_end);
	static godot::Ref<Direction> from_coords(const godot::Vector2 &p_start, const godot::Vector2 &p_end);
	godot::Ref<Point> get_start() const { return start; }
	void set_start(const godot::Ref<Point> &v) { start = v; }
	godot::Ref<Point> get_end() const { return end; }
	void set_end(const godot::Ref<Point> &v) { end = v; }
	godot::Vector2 start_coords() const { return start.is_valid() ? start->coords : godot::Vector2(); }
	godot::Vector2 end_coords() const { return end.is_valid() ? end->coords : godot::Vector2(); }
	bool is_zero_length() const { return start_coords() == end_coords(); }
	godot::Vector2 to_vector() const { return end_coords() - start_coords(); }
	godot::Vector2 to_normalized() const;
};

class Ray : public godot::RefCounted {
	GDCLASS(Ray, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Ref<Point> origin;
	godot::Ref<Direction> direction;

	static godot::Ref<Ray> make(const godot::Ref<Point> &p_origin, const godot::Ref<Direction> &p_direction);
	static godot::Ref<Ray> from_coords(const godot::Vector2 &p_origin, const godot::Ref<Direction> &p_direction);
	static godot::Ref<Ray> wrap(const rc::Ray &r);
	rc::Ray to_rc() const;
	godot::Ref<Point> get_origin() const { return origin; }
	void set_origin(const godot::Ref<Point> &v) { origin = v; }
	godot::Ref<Direction> get_direction() const { return direction; }
	void set_direction(const godot::Ref<Direction> &d) { direction = d; }
};

// One Ray object per distinct aim within a trace (GDScript: every step of a
// pair references the same aim Ray object).
struct RayCache {
	std::vector<std::pair<rc::Ray, godot::Ref<Ray>>> rays;
	godot::Ref<Ray> get(const rc::Ray &r);
};

class TraceHit : public godot::RefCounted {
	GDCLASS(TraceHit, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	double t = 0.0;
	godot::Ref<Point> point;
	godot::Variant segment; // the game Segment (or null)
	int surface_id = -1;
	int side = 0;
	bool on_segment = false;
	int at_endpoint = 0;
	bool blocked_left = false;
	bool blocked_right = false;

	static godot::Ref<TraceHit> make(double p_t, const godot::Vector2 &p_point, const godot::Variant &p_segment, int p_side, bool p_on_segment,
			int p_at_endpoint = 0, bool p_blocked_left = false, bool p_blocked_right = false);
	// segments: surface id -> game Segment, for `segment`
	static godot::Ref<TraceHit> wrap(const rc::TraceHit &h, const godot::Dictionary &segments);
	rc::TraceHit to_rc() const;
	bool is_fully_blocked() const { return blocked_left && blocked_right; }

#define RC_HIT_ACCESSOR(name, type)         \
	type get_##name() const { return name; } \
	void set_##name(type v) { name = v; }
	RC_HIT_ACCESSOR(t, double)
	RC_HIT_ACCESSOR(point, godot::Ref<Point>)
	RC_HIT_ACCESSOR(segment, godot::Variant)
	RC_HIT_ACCESSOR(surface_id, int)
	RC_HIT_ACCESSOR(side, int)
	RC_HIT_ACCESSOR(on_segment, bool)
	RC_HIT_ACCESSOR(at_endpoint, int)
	RC_HIT_ACCESSOR(blocked_left, bool)
	RC_HIT_ACCESSOR(blocked_right, bool)
#undef RC_HIT_ACCESSOR
};

class TraceStep : public godot::RefCounted {
	GDCLASS(TraceStep, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static constexpr int IDENTITY_FRAME = rc::IDENTITY_FRAME;

	godot::Vector2 start;
	godot::Vector2 end;
	godot::Vector2 via;
	int frame_id = IDENTITY_FRAME;
	godot::Ref<TraceHit> hit;
	godot::Ref<Ray> ray;
	bool is_arc_step = false;
	int type = 0;
	int surface_id = -1;
	int hit_side = -1;
	bool hit_on_segment = false;
	bool after_portal = false;
	bool ghost = false;
	double est_error = -1.0;
	int precision_bits = 0;

	static godot::Ref<TraceStep> make(const godot::Vector2 &p_start = godot::Vector2(), const godot::Vector2 &p_end = godot::Vector2(), int p_frame_id = 0,
			const godot::Ref<TraceHit> &p_hit = godot::Ref<TraceHit>(), const godot::Ref<Ray> &p_ray = godot::Ref<Ray>(),
			const godot::Vector2 &p_via = godot::Vector2(), bool p_is_arc = false);
	static godot::Ref<TraceStep> wrap(const rc::TraceStep &s, const godot::Dictionary &segments, RayCache &rays);
	rc::TraceStep to_rc() const;
	godot::Ref<TraceStep> with_type(int new_type) const;
	void copy_meta_from(const godot::Ref<TraceStep> &o);

#define RC_STEP_ACCESSOR(name, type)        \
	type get_##name() const { return name; } \
	void set_##name(type v) { name = v; }
	RC_STEP_ACCESSOR(start, godot::Vector2)
	RC_STEP_ACCESSOR(end, godot::Vector2)
	RC_STEP_ACCESSOR(via, godot::Vector2)
	RC_STEP_ACCESSOR(frame_id, int)
	RC_STEP_ACCESSOR(hit, godot::Ref<TraceHit>)
	RC_STEP_ACCESSOR(ray, godot::Ref<Ray>)
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

class TracePath : public godot::RefCounted {
	GDCLASS(TracePath, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Array steps;
	godot::Dictionary targets_hit; // surface id -> true
	int cursor_index = -1;
	int hit_count = 0;

	static godot::Ref<TracePath> wrap(const rc::TracePath &p, const godot::Dictionary &segments, RayCache &rays);
	// PathRenderer.build_physical_hits: surface id -> [{side, on_segment, has_continuation}]
	godot::Dictionary physical_hits() const;
	godot::Array get_steps() const { return steps; }
	void set_steps(const godot::Array &v) { steps = v; }
	godot::Dictionary get_targets_hit() const { return targets_hit; }
	void set_targets_hit(const godot::Dictionary &v) { targets_hit = v; }
	int get_cursor_index() const { return cursor_index; }
	void set_cursor_index(int v) { cursor_index = v; }
	int get_hit_count() const { return hit_count; }
	void set_hit_count(int v) { hit_count = v; }
};

class BeamTracerPair : public godot::RefCounted {
	GDCLASS(BeamTracerPair, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Ref<TracePath> planned;
	godot::Ref<TracePath> physical;
	godot::Ref<PrecisionLadderResult> result;
	godot::Dictionary final_flags;

	godot::Ref<TracePath> get_planned() const { return planned; }
	godot::Ref<TracePath> get_physical() const { return physical; }
	godot::Ref<PrecisionLadderResult> get_result() const { return result; }
	godot::Dictionary get_final_flags() const { return final_flags; }
};

class BeamTracer : public godot::RefCounted {
	GDCLASS(BeamTracer, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rc::Tracer tracer;

	static double TOL() { return rc::TRACE_TOL; }
	static bool scene_supported(const godot::Array &defs);
	static godot::Array plan_items(const godot::Array &plan_entries);

	// defs: SceneDefs.defs_from_surfaces(surfaces); plan_entries: PlanManager.PlanEntry or
	// BeamSolverPlanItem; segments: surface id -> game Segment (SceneDefs.segments_by_id) for the hits.
	godot::Ref<BeamTracerPair> trace_pair(const godot::Array &defs, const godot::Vector2 &player, const godot::Vector2 &cursor,
			const godot::Array &plan_entries, const godot::Dictionary &flags = godot::Dictionary(),
			const godot::Dictionary &segments = godot::Dictionary());
	bool plan_refuses(const godot::Array &defs, const godot::Array &plan_entries, int surface_id, bool left,
			const godot::Dictionary &flags = godot::Dictionary());
	void clear_cache() { tracer.clear_cache(); }
	// events of one tier (limbs), their errors, the frames map (key -> {"id": n}, updated), the aim ray (or null).
	godot::Ref<TracePath> to_path(const godot::Array &events, const godot::Array &errs, int limbs, const godot::Dictionary &frames,
			const godot::Variant &aim = godot::Variant(), const godot::Dictionary &flags = godot::Dictionary(),
			const godot::Dictionary &segments = godot::Dictionary());
};
