#include "gd_visual.hpp"

#include "rc/error.hpp"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>

using godot::Array;
using godot::ClassDB;
using godot::D_METHOD;
using godot::Dictionary;
using godot::PackedVector2Array;
using godot::PropertyInfo;
using godot::Rect2;
using godot::Ref;
using godot::Variant;
using godot::Vector2;

namespace {

PropertyInfo obj_prop(const char *name, const char *cls) {
	return PropertyInfo(Variant::OBJECT, name, godot::PROPERTY_HINT_RESOURCE_TYPE, cls);
}

Vector2 coords_of(const Ref<Point> &p) {
	return p.is_valid() ? p->coords : Vector2();
}

} // namespace

rc::arc::StepGeom step_geom_of(const Ref<TraceStep> &s) {
	rc::arc::StepGeom g;
	if (s.is_null()) {
		return g;
	}
	g.start = to_v2(s->start);
	g.end = to_v2(s->end);
	g.via = to_v2(s->via);
	g.is_arc_step = s->is_arc_step;
	g.has_hit = s->hit.is_valid();
	g.surface_id = s->surface_id;
	if (s->ray.is_valid()) {
		g.has_ray = true;
		g.ray_dir = to_v2(s->ray->direction.is_valid() ? s->ray->direction->to_normalized() : Vector2());
	}
	return g;
}

// ---------------------------------------------------------------- GeneralizedCircle

void GeneralizedCircle::_bind_methods() {
	ClassDB::bind_static_method("GeneralizedCircle", D_METHOD("make", "a", "b", "c", "d"), &GeneralizedCircle::make);
	ClassDB::bind_static_method("GeneralizedCircle", D_METHOD("from_line", "b", "c", "d"), &GeneralizedCircle::from_line);
	ClassDB::bind_static_method("GeneralizedCircle", D_METHOD("from_circle", "center", "radius"), &GeneralizedCircle::from_circle);
#define RC_GC_PROP(name)                                                            \
	ClassDB::bind_method(D_METHOD("get_" #name), &GeneralizedCircle::get_##name);   \
	ClassDB::bind_method(D_METHOD("set_" #name, "v"), &GeneralizedCircle::set_##name); \
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, #name), "set_" #name, "get_" #name);
	RC_GC_PROP(a)
	RC_GC_PROP(b)
	RC_GC_PROP(c)
	RC_GC_PROP(d)
#undef RC_GC_PROP
	ClassDB::bind_method(D_METHOD("is_line"), &GeneralizedCircle::is_line);
	ClassDB::bind_method(D_METHOD("center"), &GeneralizedCircle::center);
	ClassDB::bind_method(D_METHOD("radius"), &GeneralizedCircle::radius);
	ClassDB::bind_method(D_METHOD("same_circle", "other"), &GeneralizedCircle::same_circle);
	ClassDB::bind_method(D_METHOD("evaluate", "point"), &GeneralizedCircle::evaluate);
}

Ref<GeneralizedCircle> GeneralizedCircle::wrap(const rc::arc::GeneralizedCircle &c) {
	Ref<GeneralizedCircle> o;
	o.instantiate();
	o->g = c;
	return o;
}
Ref<GeneralizedCircle> GeneralizedCircle::make(double a, double b, double c, double d) {
	return wrap(rc::arc::GeneralizedCircle{a, b, c, d});
}
Ref<GeneralizedCircle> GeneralizedCircle::from_line(double b, double c, double d) {
	return wrap(rc::arc::from_line(b, c, d));
}
Ref<GeneralizedCircle> GeneralizedCircle::from_circle(const Vector2 &center, double radius) {
	return wrap(rc::arc::from_circle(to_v2(center), radius));
}
Vector2 GeneralizedCircle::center() const {
	if (g.a == 0.0) {
		rc::report_error("GeneralizedCircle.center() called on a line (a == 0)");
	}
	return to_vec(rc::arc::center_of(g));
}
double GeneralizedCircle::radius() const {
	if (g.a == 0.0) {
		rc::report_error("GeneralizedCircle.radius() called on a line (a == 0)");
	}
	return g.radius();
}
bool GeneralizedCircle::same_circle(const Ref<GeneralizedCircle> &other) const {
	if (other.is_null()) {
		return false;
	}
	return other.ptr() == this || rc::arc::same_circle(g, other->g);
}

// ---------------------------------------------------------------- Segment

void Segment::_bind_methods() {
	ClassDB::bind_static_method("Segment", D_METHOD("make", "start", "end", "via"), &Segment::make);
	ClassDB::bind_static_method("Segment", D_METHOD("from_coords", "s", "e", "v"), &Segment::from_coords);
	ClassDB::bind_static_method("Segment", D_METHOD("full_from_carrier", "carrier"), &Segment::full_from_carrier);
	ClassDB::bind_static_method("Segment", D_METHOD("straight", "s", "e", "v"), &Segment::straight);
	ClassDB::bind_static_method("Segment", D_METHOD("with_carrier", "s", "e", "v", "carrier"), &Segment::with_carrier);
	ClassDB::bind_static_method("Segment", D_METHOD("derive_carrier", "start", "end", "via"), &Segment::derive_carrier);
	ClassDB::bind_method(D_METHOD("get_start"), &Segment::get_start);
	ClassDB::bind_method(D_METHOD("set_start", "p"), &Segment::set_start);
	ClassDB::bind_method(D_METHOD("get_end"), &Segment::get_end);
	ClassDB::bind_method(D_METHOD("set_end", "p"), &Segment::set_end);
	ClassDB::bind_method(D_METHOD("get_via"), &Segment::get_via);
	ClassDB::bind_method(D_METHOD("set_via", "p"), &Segment::set_via);
	ClassDB::bind_method(D_METHOD("get_full"), &Segment::get_full);
	ClassDB::bind_method(D_METHOD("set_full", "v"), &Segment::set_full);
	ADD_PROPERTY(obj_prop("start", "Point"), "set_start", "get_start");
	ADD_PROPERTY(obj_prop("end", "Point"), "set_end", "get_end");
	ADD_PROPERTY(obj_prop("via", "Point"), "set_via", "get_via");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "full"), "set_full", "get_full");
	ClassDB::bind_method(D_METHOD("get_carrier"), &Segment::get_carrier);
	ClassDB::bind_method(D_METHOD("is_line"), &Segment::is_line);
	ClassDB::bind_method(D_METHOD("determine_side", "point"), &Segment::determine_side);
	ClassDB::bind_method(D_METHOD("winding"), &Segment::winding);
	ClassDB::bind_method(D_METHOD("distance_to", "point"), &Segment::distance_to);
}

Ref<Segment> Segment::make(const Ref<Point> &p_start, const Ref<Point> &p_end, const Ref<Point> &p_via) {
	Ref<Segment> s;
	s.instantiate();
	s->start = p_start;
	s->end = p_end;
	s->via = p_via;
	return s;
}
Ref<Segment> Segment::from_coords(const Vector2 &s, const Vector2 &e, const Vector2 &v) {
	return make(Point::at(s), Point::at(e), Point::at(v));
}
Ref<Segment> Segment::full_from_carrier(const Ref<GeneralizedCircle> &carrier) {
	if (carrier.is_null()) {
		rc::report_error("Segment.full_from_carrier: null carrier");
		return Ref<Segment>();
	}
	const rc::arc::SegGeom g = rc::arc::SegGeom::full_from_carrier(carrier->g);
	Ref<Segment> s = make(Point::at(to_vec(g.s)), Point::at(to_vec(g.e)), Point::at(to_vec(g.v)));
	s->full = true;
	s->carrier = carrier; // the same object (Segment.full_from_carrier keeps it)
	return s;
}
// Stage 107: a line segment whose carrier is exactly the line through s and
// e (v is kept as the segment's data point: display, labels).
Ref<Segment> Segment::straight(const Vector2 &s, const Vector2 &e, const Vector2 &v) {
	Ref<Segment> seg = make(Point::at(s), Point::at(e), Point::at(v));
	seg->carrier = GeneralizedCircle::wrap(rc::arc::straight_carrier(to_v2(s), to_v2(e)));
	return seg;
}
// Stage 108: a piece -- its span (s, e, v) on its design surface's carrier,
// kept exactly (the rounded crossing ends are not exactly on it).
Ref<Segment> Segment::with_carrier(const Vector2 &s, const Vector2 &e, const Vector2 &v, const Ref<GeneralizedCircle> &carrier) {
	Ref<Segment> seg = make(Point::at(s), Point::at(e), Point::at(v));
	seg->carrier = carrier;
	return seg;
}
Ref<GeneralizedCircle> Segment::derive_carrier(const Vector2 &s, const Vector2 &e, const Vector2 &v) {
	return GeneralizedCircle::wrap(rc::arc::derive_carrier(to_v2(s), to_v2(e), to_v2(v)));
}
rc::arc::SegGeom Segment::geom() const {
	rc::arc::SegGeom g = rc::arc::SegGeom::make(to_v2(coords_of(start)), to_v2(coords_of(end)), to_v2(coords_of(via)), full);
	if (carrier.is_valid()) {
		g.carrier = carrier->g;
	}
	return g;
}
Ref<GeneralizedCircle> Segment::get_carrier() {
	if (carrier.is_null()) {
		carrier = GeneralizedCircle::wrap(rc::arc::derive_carrier(to_v2(coords_of(start)), to_v2(coords_of(end)), to_v2(coords_of(via))));
	}
	return carrier;
}
int Segment::determine_side(const Vector2 &point) {
	get_carrier();
	return int(geom().determine_side(to_v2(point)));
}
double Segment::winding() {
	get_carrier();
	return geom().winding();
}
double Segment::distance_to(const Vector2 &point) {
	get_carrier();
	return geom().distance_to(to_v2(point));
}

// ---------------------------------------------------------------- VisualConverter

void VisualConverter::_bind_methods() {
	ClassDB::bind_static_method("VisualConverter", D_METHOD("MAX_ARC_RADIUS"), &VisualConverter::MAX_ARC_RADIUS);
	ClassDB::bind_static_method("VisualConverter", D_METHOD("DEFAULT_BOUNDS"), &VisualConverter::DEFAULT_BOUNDS);
	ClassDB::bind_static_method("VisualConverter", D_METHOD("compute_display_end", "step", "surfaces"), &VisualConverter::compute_display_end, DEFVAL(Array()));
	ClassDB::bind_static_method("VisualConverter", D_METHOD("project_onto_carrier", "point", "carrier"), &VisualConverter::project_onto_carrier);
	ClassDB::bind_static_method("VisualConverter", D_METHOD("prepare_for_display", "path", "bounds", "surfaces"), &VisualConverter::prepare_for_display, DEFVAL(Array()));
	ClassDB::bind_static_method("VisualConverter", D_METHOD("clip_to_bounds", "origin", "dir", "bounds"), &VisualConverter::clip_to_bounds);
	ClassDB::bind_static_method("VisualConverter", D_METHOD("is_arc", "start", "via", "end_v"), &VisualConverter::is_arc);
	ClassDB::bind_static_method("VisualConverter", D_METHOD("arc_params", "start", "via", "end_v"), &VisualConverter::arc_params);
	BIND_CONSTANT(POINTS_PER_FULL_CIRCLE);
}

namespace {

// The carrier of the game surface with `surface_id` among `surfaces` (duck-typed: id, segment).
std::function<std::optional<rc::arc::GeneralizedCircle>(int)> carrier_lookup(const Array &surfaces) {
	return [surfaces](int surface_id) -> std::optional<rc::arc::GeneralizedCircle> {
		for (int64_t i = 0; i < surfaces.size(); i++) {
			godot::Object *s = surfaces[i];
			if (s == nullptr || int(int64_t(s->get("id"))) != surface_id) {
				continue;
			}
			const Ref<Segment> seg = s->get("segment");
			if (seg.is_null()) {
				return std::nullopt;
			}
			return seg->get_carrier()->g;
		}
		return std::nullopt;
	};
}

// VisualConverter._piece
Ref<TraceStep> piece_of(const Ref<TraceStep> &step, const Vector2 &p_start, const Vector2 &p_end, const Ref<TraceHit> &hit, const Vector2 &via, bool first) {
	Ref<TraceStep> piece = TraceStep::make(p_start, p_end, step->frame_id, hit, step->ray, via, false);
	piece->type = step->type;
	piece->est_error = step->est_error;
	piece->precision_bits = step->precision_bits;
	piece->after_portal = first ? step->after_portal : false;
	if (hit.is_valid()) {
		piece->surface_id = step->surface_id;
		piece->hit_side = step->hit_side;
		piece->hit_on_segment = step->hit_on_segment;
	}
	return piece;
}

} // namespace

Vector2 VisualConverter::compute_display_end(const Ref<TraceStep> &step, const Array &surfaces) {
	if (step.is_null()) {
		return Vector2();
	}
	return to_vec(rc::arc::display_end(step_geom_of(step), carrier_lookup(surfaces)));
}

Vector2 VisualConverter::project_onto_carrier(const Vector2 &point, const Ref<GeneralizedCircle> &carrier) {
	if (carrier.is_null()) {
		rc::report_error("VisualConverter.project_onto_carrier: null carrier");
		return point;
	}
	return to_vec(rc::arc::project_onto_carrier(to_v2(point), carrier->g));
}

Ref<TracePath> VisualConverter::prepare_for_display(const Ref<TracePath> &path, const Rect2 &bounds, const Array &surfaces) {
	Ref<TracePath> result;
	result.instantiate();
	if (path.is_null()) {
		return result;
	}
	std::vector<rc::arc::StepGeom> geoms;
	std::vector<Ref<TraceStep>> steps;
	for (int64_t i = 0; i < path->steps.size(); i++) {
		const Ref<TraceStep> s = path->steps[i];
		steps.push_back(s);
		geoms.push_back(step_geom_of(s));
	}
	const rc::arc::DisplayPlan plan = rc::arc::prepare_for_display(geoms, path->cursor_index, to_rect(bounds), carrier_lookup(surfaces));
	result->targets_hit = path->targets_hit; // the same Dictionary (shared, as in GDScript)
	result->cursor_index = plan.cursor_index;
	for (const rc::arc::Piece &p : plan.pieces) {
		const Ref<TraceStep> &src = steps[size_t(p.src)];
		switch (p.kind) {
			case rc::arc::Piece::Identity:
				result->steps.push_back(src);
				break;
			case rc::arc::Piece::Snapped: {
				// VisualConverter._snap_step
				Ref<TraceStep> snapped = TraceStep::make(src->start, to_vec(p.end), src->frame_id, src->hit, src->ray, src->via, src->is_arc_step);
				snapped->type = src->type;
				snapped->copy_meta_from(src);
				result->steps.push_back(snapped);
				break;
			}
			case rc::arc::Piece::Clipped:
				result->steps.push_back(piece_of(src, to_vec(p.start), to_vec(p.end), p.keep_hit ? src->hit : Ref<TraceHit>(), to_vec(p.via), p.first));
				break;
		}
	}
	return result;
}

Vector2 VisualConverter::clip_to_bounds(const Vector2 &origin, const Vector2 &dir, const Rect2 &bounds) {
	return to_vec(rc::arc::clip_to_bounds(to_v2(origin), to_v2(dir), to_rect(bounds)));
}

bool VisualConverter::is_arc(const Vector2 &start, const Vector2 &via, const Vector2 &end_v) {
	return rc::arc::is_arc(to_v2(start), to_v2(via), to_v2(end_v));
}

Dictionary VisualConverter::arc_params(const Vector2 &start, const Vector2 &via, const Vector2 &end_v) {
	const rc::arc::ArcParams p = rc::arc::arc_params(to_v2(start), to_v2(via), to_v2(end_v));
	Dictionary d;
	d["center"] = to_vec(p.center);
	d["radius"] = p.radius;
	d["start_angle"] = p.start_angle;
	d["end_angle"] = p.end_angle;
	d["clockwise"] = p.clockwise;
	d["point_count"] = p.point_count;
	d["span"] = p.span;
	return d;
}

// ---------------------------------------------------------------- StepTreeMerge

void StepTreeMerge::_bind_methods() {
	ClassDB::bind_static_method("StepTreeMerge", D_METHOD("classify_physical", "path"), &StepTreeMerge::classify_physical);
	ClassDB::bind_static_method("StepTreeMerge", D_METHOD("merge", "planned_steps", "physical_steps", "cursor_index"), &StepTreeMerge::merge);
}

Array StepTreeMerge::classify_physical(const Ref<TracePath> &path) {
	Array result;
	if (path.is_null()) {
		return result;
	}
	int ci = path->cursor_index;
	if (ci < 0) {
		ci = int(path->steps.size());
	}
	for (int64_t idx = 0; idx < path->steps.size(); idx++) {
		const Ref<TraceStep> s = path->steps[idx];
		const int type = idx < ci ? int(rc::StepType::Aligned) : int(rc::StepType::AlignedPostPlanned);
		result.push_back(s->with_type(type));
	}
	return result;
}

Array StepTreeMerge::merge(const Array &planned_steps, const Array &physical_steps, int cursor_index) {
	Array merged;
	bool diverged = false;
	const int64_t max_len = std::max(planned_steps.size(), physical_steps.size());
	for (int64_t idx = 0; idx < max_len; idx++) {
		const Ref<TraceStep> p = idx < planned_steps.size() ? Ref<TraceStep>(planned_steps[idx]) : Ref<TraceStep>();
		const Ref<TraceStep> r = idx < physical_steps.size() ? Ref<TraceStep>(physical_steps[idx]) : Ref<TraceStep>();
		const bool past_cursor = idx >= cursor_index;
		if (!diverged && p.is_valid() && r.is_valid() && p->frame_id == r->frame_id) {
			merged.push_back(p->with_type(past_cursor ? int(rc::StepType::AlignedPostPlanned) : int(rc::StepType::Aligned)));
		} else {
			diverged = true;
			if (p.is_valid()) {
				merged.push_back(p->with_type(past_cursor ? int(rc::StepType::DivergedPostPlanned) : int(rc::StepType::DivergedPlanned)));
			}
			if (r.is_valid()) {
				merged.push_back(r->with_type(int(rc::StepType::DivergedPhysical)));
			}
		}
	}
	return merged;
}

// ---------------------------------------------------------------- ArrowAdvance

void ArrowAdvanceResult::_bind_methods() {
#define RC_ADV_PROP(name, vtype)                                                           \
	ClassDB::bind_method(D_METHOD("get_" #name), &ArrowAdvanceResult::get_##name);         \
	ClassDB::bind_method(D_METHOD("set_" #name, "v"), &ArrowAdvanceResult::set_##name);    \
	ADD_PROPERTY(PropertyInfo(Variant::vtype, #name), "set_" #name, "get_" #name);
	RC_ADV_PROP(step_index, INT)
	RC_ADV_PROP(progress, FLOAT)
	RC_ADV_PROP(position, VECTOR2)
	RC_ADV_PROP(direction, VECTOR2)
	RC_ADV_PROP(finished, BOOL)
#undef RC_ADV_PROP
}

void ArrowAdvance::_bind_methods() {
	ClassDB::bind_static_method("ArrowAdvance", D_METHOD("advance", "steps", "step_index", "progress", "pos", "distance", "bounds"), &ArrowAdvance::advance);
	ClassDB::bind_static_method("ArrowAdvance", D_METHOD("start_direction", "step"), &ArrowAdvance::start_direction);
}

Vector2 ArrowAdvance::start_direction(const Ref<TraceStep> &step) {
	const rc::arc::StepGeom g = step_geom_of(step);
	if (rc::arc::step_is_arc(g)) {
		return to_vec(rc::arc::arc_interpolate(g, 0.0).direction);
	}
	return to_vec((g.end - g.start).normalized());
}

Ref<ArrowAdvanceResult> ArrowAdvance::advance(const Array &steps, int step_index, double progress, const Vector2 &pos, double distance, const Rect2 &bounds) {
	std::vector<rc::arc::StepGeom> geoms;
	for (int64_t i = 0; i < steps.size(); i++) {
		geoms.push_back(step_geom_of(Ref<TraceStep>(steps[i])));
	}
	const rc::arc::AdvanceResult r = rc::arc::advance(geoms, step_index, progress, to_v2(pos), distance, to_rect(bounds));
	Ref<ArrowAdvanceResult> o;
	o.instantiate();
	o->step_index = r.step_index;
	o->progress = r.progress;
	o->position = to_vec(r.position);
	o->direction = to_vec(r.direction);
	o->finished = r.finished;
	return o;
}

// ---------------------------------------------------------------- SurfaceGeom

void SurfaceGeom::_bind_methods() {
	ClassDB::bind_static_method("SurfaceGeom", D_METHOD("line_sample", "start", "end", "t"), &SurfaceGeom::line_sample);
	ClassDB::bind_static_method("SurfaceGeom", D_METHOD("arc_sample", "center", "radius", "start_angle", "span", "clockwise", "t"), &SurfaceGeom::arc_sample);
	ClassDB::bind_static_method("SurfaceGeom", D_METHOD("chevron_vertices", "tip", "direction", "size"), &SurfaceGeom::chevron_vertices);
}

Dictionary SurfaceGeom::line_sample(const Vector2 &start, const Vector2 &end, double t) {
	const rc::arc::LineSample s = rc::arc::line_sample(to_v2(start), to_v2(end), t);
	Dictionary d;
	d["position"] = to_vec(s.position);
	d["normal"] = to_vec(s.normal);
	return d;
}

Dictionary SurfaceGeom::arc_sample(const Vector2 &center, double radius, double start_angle, double span, bool clockwise, double t) {
	const rc::arc::ArcSample s = rc::arc::arc_sample(to_v2(center), radius, start_angle, span, clockwise, t);
	Dictionary d;
	d["position"] = to_vec(s.position);
	d["outward"] = to_vec(s.outward);
	return d;
}

PackedVector2Array SurfaceGeom::chevron_vertices(const Vector2 &tip, const Vector2 &direction, double size) {
	const auto v = rc::arc::chevron_vertices(to_v2(tip), to_v2(direction), size);
	PackedVector2Array out;
	for (const rc::V2 &p : v) {
		out.push_back(to_vec(p));
	}
	return out;
}
