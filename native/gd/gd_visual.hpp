// Bindings of the visual / testing kernels (Stage 97): GeneralizedCircle,
// Segment (the game's segment data class: Points, carrier, sides, click
// distance), VisualConverter, StepTreeMerge, ArrowAdvance (+ Result) and
// SurfaceGeom. The classes keep the GDScript shapes; construction goes through
// `make` / `from_coords` / `from_circle` / `from_line`.
#pragma once

#include "gd_tracer.hpp"
#include "rc/visual.hpp"

#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/rect2.hpp>

inline rc::Rect2f to_rect(const godot::Rect2 &r) { return rc::Rect2f{to_v2(r.position), to_v2(r.size)}; }
inline godot::Rect2 to_gd_rect(const rc::Rect2f &r) { return godot::Rect2(to_vec(r.position), to_vec(r.size)); }

class GeneralizedCircle : public godot::RefCounted {
	GDCLASS(GeneralizedCircle, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rc::arc::GeneralizedCircle g;

	static godot::Ref<GeneralizedCircle> wrap(const rc::arc::GeneralizedCircle &c);
	static godot::Ref<GeneralizedCircle> make(double a, double b, double c, double d);
	static godot::Ref<GeneralizedCircle> from_line(double b, double c, double d);
	static godot::Ref<GeneralizedCircle> from_circle(const godot::Vector2 &center, double radius);
	double get_a() const { return g.a; }
	void set_a(double v) { g.a = v; }
	double get_b() const { return g.b; }
	void set_b(double v) { g.b = v; }
	double get_c() const { return g.c; }
	void set_c(double v) { g.c = v; }
	double get_d() const { return g.d; }
	void set_d(double v) { g.d = v; }
	bool is_line() const { return g.is_line(); }
	godot::Vector2 center() const;
	double radius() const;
	bool same_circle(const godot::Ref<GeneralizedCircle> &other) const;
	double evaluate(const godot::Vector2 &point) const { return g.evaluate(point.x, point.y); }
};

class Segment : public godot::RefCounted {
	GDCLASS(Segment, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Ref<Point> start;
	godot::Ref<Point> end;
	godot::Ref<Point> via;
	bool full = false;
	godot::Ref<GeneralizedCircle> carrier; // derived on demand (Segment._carrier)

	static godot::Ref<Segment> make(const godot::Ref<Point> &p_start, const godot::Ref<Point> &p_end, const godot::Ref<Point> &p_via);
	static godot::Ref<Segment> from_coords(const godot::Vector2 &s, const godot::Vector2 &e, const godot::Vector2 &v);
	static godot::Ref<Segment> full_from_carrier(const godot::Ref<GeneralizedCircle> &carrier);
	static godot::Ref<Segment> straight(const godot::Vector2 &s, const godot::Vector2 &e, const godot::Vector2 &v);
	static godot::Ref<GeneralizedCircle> derive_carrier(const godot::Vector2 &s, const godot::Vector2 &e, const godot::Vector2 &v);

	rc::arc::SegGeom geom() const; // coordinates + the cached carrier
	godot::Ref<Point> get_start() const { return start; }
	void set_start(const godot::Ref<Point> &p) { start = p; carrier.unref(); }
	godot::Ref<Point> get_end() const { return end; }
	void set_end(const godot::Ref<Point> &p) { end = p; carrier.unref(); }
	godot::Ref<Point> get_via() const { return via; }
	void set_via(const godot::Ref<Point> &p) { via = p; carrier.unref(); }
	bool get_full() const { return full; }
	void set_full(bool v) { full = v; }
	godot::Ref<GeneralizedCircle> get_carrier();
	bool is_line() { return get_carrier()->is_line(); }
	int determine_side(const godot::Vector2 &point);
	double winding();
	double distance_to(const godot::Vector2 &point);
};

class VisualConverter : public godot::RefCounted {
	GDCLASS(VisualConverter, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static constexpr int POINTS_PER_FULL_CIRCLE = rc::arc::POINTS_PER_FULL_CIRCLE;
	static double MAX_ARC_RADIUS() { return rc::arc::MAX_ARC_RADIUS; }
	static godot::Rect2 DEFAULT_BOUNDS() { return godot::Rect2(0, 0, 1920, 1080); }

	static godot::Vector2 compute_display_end(const godot::Ref<TraceStep> &step, const godot::Array &surfaces = godot::Array());
	static godot::Vector2 project_onto_carrier(const godot::Vector2 &point, const godot::Ref<GeneralizedCircle> &carrier);
	static godot::Ref<TracePath> prepare_for_display(const godot::Ref<TracePath> &path, const godot::Rect2 &bounds, const godot::Array &surfaces = godot::Array());
	static godot::Vector2 clip_to_bounds(const godot::Vector2 &origin, const godot::Vector2 &dir, const godot::Rect2 &bounds);
	static bool is_arc(const godot::Vector2 &start, const godot::Vector2 &via, const godot::Vector2 &end_v);
	static godot::Dictionary arc_params(const godot::Vector2 &start, const godot::Vector2 &via, const godot::Vector2 &end_v);
};

class StepTreeMerge : public godot::RefCounted {
	GDCLASS(StepTreeMerge, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static godot::Array classify_physical(const godot::Ref<TracePath> &path);
	static godot::Array merge(const godot::Array &planned_steps, const godot::Array &physical_steps, int cursor_index);
};

class ArrowAdvanceResult : public godot::RefCounted {
	GDCLASS(ArrowAdvanceResult, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	int step_index = 0;
	double progress = 0.0;
	godot::Vector2 position;
	godot::Vector2 direction = godot::Vector2(1, 0);
	bool finished = false;

#define RC_ADV_ACCESSOR(name, type)         \
	type get_##name() const { return name; } \
	void set_##name(type v) { name = v; }
	RC_ADV_ACCESSOR(step_index, int)
	RC_ADV_ACCESSOR(progress, double)
	RC_ADV_ACCESSOR(position, godot::Vector2)
	RC_ADV_ACCESSOR(direction, godot::Vector2)
	RC_ADV_ACCESSOR(finished, bool)
#undef RC_ADV_ACCESSOR
};

class ArrowAdvance : public godot::RefCounted {
	GDCLASS(ArrowAdvance, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static godot::Ref<ArrowAdvanceResult> advance(const godot::Array &steps, int step_index, double progress, const godot::Vector2 &pos,
			double distance, const godot::Rect2 &bounds);
	// the arrow's direction at the start of a step (ArrowAnimator.start_flight)
	static godot::Vector2 start_direction(const godot::Ref<TraceStep> &step);
};

class SurfaceGeom : public godot::RefCounted {
	GDCLASS(SurfaceGeom, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static godot::Dictionary line_sample(const godot::Vector2 &start, const godot::Vector2 &end, double t);
	static godot::Dictionary arc_sample(const godot::Vector2 &center, double radius, double start_angle, double span, bool clockwise, double t);
	static godot::PackedVector2Array chevron_vertices(const godot::Vector2 &tip, const godot::Vector2 &direction, double size);
};

// a TraceStep's geometry for the display kernels
rc::arc::StepGeom step_geom_of(const godot::Ref<TraceStep> &s);
