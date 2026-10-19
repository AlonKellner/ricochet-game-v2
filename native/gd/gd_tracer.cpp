#include "gd_tracer.hpp"
#include "gd_visibility.hpp"

#include "rc/error.hpp"

#include <godot_cpp/core/class_db.hpp>

using godot::Array;
using godot::ClassDB;
using godot::D_METHOD;
using godot::Dictionary;
using godot::PropertyInfo;
using godot::Ref;
using godot::Variant;
using godot::Vector2;
using namespace rcgd;

namespace {

PropertyInfo obj_prop(const char *name, const char *cls) {
	return PropertyInfo(Variant::OBJECT, name, godot::PROPERTY_HINT_RESOURCE_TYPE, cls);
}

} // namespace

// ---------------------------------------------------------------- Point

void Point::_bind_methods() {
	ClassDB::bind_static_method("Point", D_METHOD("at", "position"), &Point::at);
	ClassDB::bind_method(D_METHOD("get_coords"), &Point::get_coords);
	ClassDB::bind_method(D_METHOD("set_coords", "v"), &Point::set_coords);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "coords"), "set_coords", "get_coords");
	ClassDB::bind_method(D_METHOD("same_position", "other"), &Point::same_position);
}

Ref<Point> Point::at(const Vector2 &position) {
	Ref<Point> p;
	p.instantiate();
	p->coords = position;
	return p;
}

// ---------------------------------------------------------------- Direction

void Direction::_bind_methods() {
	ClassDB::bind_static_method("Direction", D_METHOD("make", "start", "end"), &Direction::make);
	ClassDB::bind_static_method("Direction", D_METHOD("from_coords", "start", "end"), &Direction::from_coords);
	ClassDB::bind_method(D_METHOD("get_start"), &Direction::get_start);
	ClassDB::bind_method(D_METHOD("set_start", "v"), &Direction::set_start);
	ClassDB::bind_method(D_METHOD("get_end"), &Direction::get_end);
	ClassDB::bind_method(D_METHOD("set_end", "v"), &Direction::set_end);
	ADD_PROPERTY(obj_prop("start", "Point"), "set_start", "get_start");
	ADD_PROPERTY(obj_prop("end", "Point"), "set_end", "get_end");
	ClassDB::bind_method(D_METHOD("is_zero_length"), &Direction::is_zero_length);
	ClassDB::bind_method(D_METHOD("to_vector"), &Direction::to_vector);
	ClassDB::bind_method(D_METHOD("to_normalized"), &Direction::to_normalized);
}

Ref<Direction> Direction::make(const Ref<Point> &p_start, const Ref<Point> &p_end) {
	Ref<Direction> d;
	d.instantiate();
	d->start = p_start;
	d->end = p_end;
	return d;
}

Ref<Direction> Direction::from_coords(const Vector2 &p_start, const Vector2 &p_end) {
	return make(Point::at(p_start), Point::at(p_end));
}

Vector2 Direction::to_normalized() const {
	// Direction.to_normalized: Vector2.normalized() of the float32 vector (zero stays zero)
	return to_vec(rc::Ray{rc::V2{}, to_v2(start_coords()), to_v2(end_coords())}.to_normalized());
}

// ---------------------------------------------------------------- Ray

void Ray::_bind_methods() {
	ClassDB::bind_static_method("Ray", D_METHOD("make", "origin", "direction"), &Ray::make);
	ClassDB::bind_static_method("Ray", D_METHOD("from_coords", "origin", "direction"), &Ray::from_coords);
	ClassDB::bind_method(D_METHOD("get_origin"), &Ray::get_origin);
	ClassDB::bind_method(D_METHOD("set_origin", "v"), &Ray::set_origin);
	ClassDB::bind_method(D_METHOD("get_direction"), &Ray::get_direction);
	ClassDB::bind_method(D_METHOD("set_direction", "d"), &Ray::set_direction);
	ADD_PROPERTY(obj_prop("origin", "Point"), "set_origin", "get_origin");
	ADD_PROPERTY(obj_prop("direction", "Direction"), "set_direction", "get_direction");
}

Ref<Ray> Ray::make(const Ref<Point> &p_origin, const Ref<Direction> &p_direction) {
	Ref<Ray> r;
	r.instantiate();
	r->origin = p_origin;
	r->direction = p_direction;
	return r;
}

Ref<Ray> Ray::from_coords(const Vector2 &p_origin, const Ref<Direction> &p_direction) {
	return make(Point::at(p_origin), p_direction);
}

Ref<Ray> Ray::wrap(const rc::Ray &r) {
	return from_coords(to_vec(r.origin), Direction::from_coords(to_vec(r.dir_start), to_vec(r.dir_end)));
}

rc::Ray Ray::to_rc() const {
	rc::Ray r;
	if (origin.is_valid()) {
		r.origin = to_v2(origin->coords);
	}
	if (direction.is_valid()) {
		r.dir_start = to_v2(direction->start_coords());
		r.dir_end = to_v2(direction->end_coords());
	}
	return r;
}

// ---------------------------------------------------------------- RayCache

Ref<Ray> RayCache::get(const rc::Ray &r) {
	for (const auto &kv : rays) {
		if (kv.first.origin == r.origin && kv.first.dir_start == r.dir_start && kv.first.dir_end == r.dir_end) {
			return kv.second;
		}
	}
	rays.emplace_back(r, Ray::wrap(r));
	return rays.back().second;
}

// ---------------------------------------------------------------- TraceHit

void TraceHit::_bind_methods() {
	ClassDB::bind_static_method("TraceHit", D_METHOD("make", "t", "point", "segment", "side", "on_segment"), &TraceHit::make);
#define RC_HIT_PROP(name, vtype)                                                      \
	ClassDB::bind_method(D_METHOD("get_" #name), &TraceHit::get_##name);              \
	ClassDB::bind_method(D_METHOD("set_" #name, "v"), &TraceHit::set_##name);         \
	ADD_PROPERTY(PropertyInfo(Variant::vtype, #name), "set_" #name, "get_" #name);
	RC_HIT_PROP(t, FLOAT)
	RC_HIT_PROP(surface_id, INT)
	RC_HIT_PROP(side, INT)
	RC_HIT_PROP(on_segment, BOOL)
#undef RC_HIT_PROP
	ClassDB::bind_method(D_METHOD("get_point"), &TraceHit::get_point);
	ClassDB::bind_method(D_METHOD("set_point", "v"), &TraceHit::set_point);
	ADD_PROPERTY(obj_prop("point", "Point"), "set_point", "get_point");
	ClassDB::bind_method(D_METHOD("get_segment"), &TraceHit::get_segment);
	ClassDB::bind_method(D_METHOD("set_segment", "v"), &TraceHit::set_segment);
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "segment", godot::PROPERTY_HINT_NONE, "", godot::PROPERTY_USAGE_DEFAULT | godot::PROPERTY_USAGE_NIL_IS_VARIANT),
			"set_segment", "get_segment");
}

Ref<TraceHit> TraceHit::make(double p_t, const Vector2 &p_point, const Variant &p_segment, int p_side, bool p_on_segment) {
	Ref<TraceHit> h;
	h.instantiate();
	h->t = p_t;
	h->point_coords = p_point;
	h->has_point = true;
	h->segment = p_segment;
	h->side = p_side;
	h->on_segment = p_on_segment;
	return h;
}

Ref<Point> TraceHit::get_point() const {
	if (point.is_null() && has_point) {
		point = Point::at(point_coords);
	}
	return point;
}

void TraceHit::set_point(const Ref<Point> &v) {
	point = v;
	has_point = v.is_valid();
	if (has_point) {
		point_coords = v->coords;
	}
}

Ref<TraceHit> TraceHit::wrap(const rc::TraceHit &h, const Dictionary &segments) {
	Ref<TraceHit> o = make(h.t, to_vec(h.point), segments.get(h.surface_id, Variant()), int(h.side), h.on_segment);
	o->surface_id = h.surface_id;
	return o;
}

rc::TraceHit TraceHit::to_rc() const {
	rc::TraceHit h;
	h.t = t;
	h.point = point.is_valid() ? to_v2(point->coords) : (has_point ? to_v2(point_coords) : rc::V2{});
	h.surface_id = surface_id;
	h.side = rc::Side(side);
	h.on_segment = on_segment;
	return h;
}

// ---------------------------------------------------------------- TraceStep

void TraceStep::_bind_methods() {
	ClassDB::bind_static_method("TraceStep", D_METHOD("make", "start", "end", "frame_id", "hit", "ray", "via", "is_arc"), &TraceStep::make,
			DEFVAL(Vector2()), DEFVAL(Vector2()), DEFVAL(0), DEFVAL(Variant()), DEFVAL(Variant()), DEFVAL(Vector2()), DEFVAL(false));
#define RC_STEP_PROP(name, vtype)                                                     \
	ClassDB::bind_method(D_METHOD("get_" #name), &TraceStep::get_##name);             \
	ClassDB::bind_method(D_METHOD("set_" #name, "v"), &TraceStep::set_##name);        \
	ADD_PROPERTY(PropertyInfo(Variant::vtype, #name), "set_" #name, "get_" #name);
	RC_STEP_PROP(start, VECTOR2)
	RC_STEP_PROP(end, VECTOR2)
	RC_STEP_PROP(via, VECTOR2)
	RC_STEP_PROP(frame_id, INT)
	RC_STEP_PROP(is_arc_step, BOOL)
	RC_STEP_PROP(type, INT)
	RC_STEP_PROP(surface_id, INT)
	RC_STEP_PROP(hit_side, INT)
	RC_STEP_PROP(hit_on_segment, BOOL)
	RC_STEP_PROP(after_portal, BOOL)
	RC_STEP_PROP(ghost, BOOL)
	RC_STEP_PROP(est_error, FLOAT)
	RC_STEP_PROP(precision_bits, INT)
#undef RC_STEP_PROP
	ClassDB::bind_method(D_METHOD("get_hit"), &TraceStep::get_hit);
	ClassDB::bind_method(D_METHOD("set_hit", "v"), &TraceStep::set_hit);
	ADD_PROPERTY(obj_prop("hit", "TraceHit"), "set_hit", "get_hit");
	ClassDB::bind_method(D_METHOD("get_ray"), &TraceStep::get_ray);
	ClassDB::bind_method(D_METHOD("set_ray", "v"), &TraceStep::set_ray);
	ADD_PROPERTY(obj_prop("ray", "Ray"), "set_ray", "get_ray");
	ClassDB::bind_method(D_METHOD("with_type", "new_type"), &TraceStep::with_type);
	ClassDB::bind_method(D_METHOD("copy_meta_from", "o"), &TraceStep::copy_meta_from);
	BIND_CONSTANT(IDENTITY_FRAME);
}

Ref<TraceStep> TraceStep::make(const Vector2 &p_start, const Vector2 &p_end, int p_frame_id, const Ref<TraceHit> &p_hit, const Ref<Ray> &p_ray,
		const Vector2 &p_via, bool p_is_arc) {
	Ref<TraceStep> s;
	s.instantiate();
	s->start = p_start;
	s->end = p_end;
	s->frame_id = p_frame_id;
	s->hit = p_hit;
	s->ray = p_ray;
	// TraceStep._init: a zero via is the midpoint (float32 arithmetic)
	s->via = p_via != Vector2() ? p_via : to_vec((to_v2(p_start) + to_v2(p_end)) / 2.0f);
	s->is_arc_step = p_is_arc;
	return s;
}

Ref<TraceStep> TraceStep::wrap(const rc::TraceStep &st, const Dictionary &segments, RayCache &rays) {
	Ref<TraceStep> s;
	s.instantiate();
	s->start = to_vec(st.start);
	s->end = to_vec(st.end);
	s->via = to_vec(st.via);
	s->frame_id = st.frame_id;
	if (st.hit) {
		s->hit = TraceHit::wrap(*st.hit, segments);
	}
	if (st.ray) {
		s->ray = rays.get(*st.ray);
	}
	s->is_arc_step = st.is_arc_step;
	s->type = int(st.type);
	s->surface_id = st.surface_id;
	s->hit_side = st.hit_side;
	s->hit_on_segment = st.hit_on_segment;
	s->after_portal = st.after_portal;
	s->ghost = st.ghost;
	s->est_error = st.est_error;
	s->precision_bits = st.precision_bits;
	return s;
}

rc::TraceStep TraceStep::to_rc() const {
	rc::TraceStep st;
	st.start = to_v2(start);
	st.end = to_v2(end);
	st.via = to_v2(via);
	st.frame_id = frame_id;
	if (hit.is_valid()) {
		st.hit = hit->to_rc();
	}
	if (ray.is_valid()) {
		st.ray = ray->to_rc();
	}
	st.is_arc_step = is_arc_step;
	st.type = rc::StepType(type);
	st.surface_id = surface_id;
	st.hit_side = hit_side;
	st.hit_on_segment = hit_on_segment;
	st.after_portal = after_portal;
	st.ghost = ghost;
	st.est_error = est_error;
	st.precision_bits = precision_bits;
	return st;
}

Ref<TraceStep> TraceStep::with_type(int new_type) const {
	Ref<TraceStep> copy = make(start, end, frame_id, hit, ray, via, is_arc_step);
	copy->type = new_type;
	copy->copy_meta_from(Ref<TraceStep>(this));
	return copy;
}

void TraceStep::copy_meta_from(const Ref<TraceStep> &o) {
	if (o.is_null()) {
		return;
	}
	surface_id = o->surface_id;
	hit_side = o->hit_side;
	hit_on_segment = o->hit_on_segment;
	after_portal = o->after_portal;
	est_error = o->est_error;
	precision_bits = o->precision_bits;
	ghost = o->ghost;
}

// ---------------------------------------------------------------- TracePath

void TracePath::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_steps"), &TracePath::get_steps);
	ClassDB::bind_method(D_METHOD("set_steps", "v"), &TracePath::set_steps);
	ClassDB::bind_method(D_METHOD("get_targets_hit"), &TracePath::get_targets_hit);
	ClassDB::bind_method(D_METHOD("set_targets_hit", "v"), &TracePath::set_targets_hit);
	ClassDB::bind_method(D_METHOD("get_cursor_index"), &TracePath::get_cursor_index);
	ClassDB::bind_method(D_METHOD("set_cursor_index", "v"), &TracePath::set_cursor_index);
	ClassDB::bind_method(D_METHOD("get_hit_count"), &TracePath::get_hit_count);
	ClassDB::bind_method(D_METHOD("set_hit_count", "v"), &TracePath::set_hit_count);
	ClassDB::bind_method(D_METHOD("get_truncated"), &TracePath::get_truncated);
	ClassDB::bind_method(D_METHOD("set_truncated", "v"), &TracePath::set_truncated);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "steps"), "set_steps", "get_steps");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "targets_hit"), "set_targets_hit", "get_targets_hit");
	ClassDB::bind_method(D_METHOD("get_hits"), &TracePath::get_hits);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "hits"), "", "get_hits");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cursor_index"), "set_cursor_index", "get_cursor_index");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "hit_count"), "set_hit_count", "get_hit_count");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "truncated"), "set_truncated", "get_truncated");
	ClassDB::bind_method(D_METHOD("physical_hits"), &TracePath::physical_hits);
}

Dictionary TracePath::physical_hits() const {
	std::vector<rc::TraceStep> rs;
	rs.reserve(size_t(steps.size()));
	for (int64_t i = 0; i < steps.size(); i++) {
		const Ref<TraceStep> s = steps[i];
		rs.push_back(s.is_valid() ? s->to_rc() : rc::TraceStep{});
	}
	Dictionary result;
	for (const auto &[sid, of_surface] : rc::physical_hits(rs)) {
		Array list;
		for (const rc::PhysicalHit &h : of_surface) {
			Dictionary d;
			d["side"] = h.side;
			d["on_segment"] = h.on_segment;
			d["has_continuation"] = h.has_continuation;
			d["point"] = to_vec(h.point); // Stage 108: where (a planned piece checks it)
			list.push_back(d);
		}
		result[sid] = list;
	}
	return result;
}

Array TracePath::get_hits() const {
	if (hits_pending) {
		hits_pending = false;
		for (const rc::HitRecord &h : native_hits) {
			Dictionary d;
			d["surface_id"] = h.surface_id;
			d["side"] = int(h.side);
			d["on_segment"] = h.on_segment;
			d["fired"] = h.fired;
			d["state"] = h.state;
			d["point"] = Vector2(h.point.x, h.point.y);
			d["tangent"] = Vector2(h.tangent.x, h.tangent.y);
			d["flags_before"] = flags_to_dict(h.flags_before);
			d["step_index"] = h.step_index;
			hits.push_back(d);
		}
		native_hits.clear();
	}
	return hits;
}

Ref<TracePath> TracePath::wrap(const rc::TracePath &p, const Dictionary &segments, RayCache &rays) {
	Ref<TracePath> o;
	o.instantiate();
	for (const rc::TraceStep &s : p.steps) {
		o->steps.push_back(TraceStep::wrap(s, segments, rays));
	}
	for (int id : p.targets_hit) {
		o->targets_hit[id] = true;
	}
	o->native_hits = p.hits;
	o->hits_pending = !p.hits.empty();
	o->cursor_index = p.cursor_index;
	o->hit_count = p.hit_count;
	o->truncated = p.truncated;
	return o;
}

// ---------------------------------------------------------------- BeamTracerPair

void BeamTracerPair::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_planned"), &BeamTracerPair::get_planned);
	ClassDB::bind_method(D_METHOD("get_physical"), &BeamTracerPair::get_physical);
	ClassDB::bind_method(D_METHOD("get_result"), &BeamTracerPair::get_result);
	ClassDB::bind_method(D_METHOD("get_final_flags"), &BeamTracerPair::get_final_flags);
	ClassDB::bind_method(D_METHOD("get_cursor_point_agrees"), &BeamTracerPair::get_cursor_point_agrees);
	ADD_PROPERTY(obj_prop("planned", "TracePath"), "", "get_planned");
	ADD_PROPERTY(obj_prop("physical", "TracePath"), "", "get_physical");
	ADD_PROPERTY(obj_prop("result", "PrecisionLadderResult"), "", "get_result");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "final_flags"), "", "get_final_flags");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "cursor_point_agrees"), "", "get_cursor_point_agrees");
}

// ---------------------------------------------------------------- BeamTracer

void BeamTracer::_bind_methods() {
	ClassDB::bind_static_method("BeamTracer", D_METHOD("TOL"), &BeamTracer::TOL);
	ClassDB::bind_static_method("BeamTracer", D_METHOD("scene_supported", "defs"), &BeamTracer::scene_supported);
	ClassDB::bind_static_method("BeamTracer", D_METHOD("plan_items", "plan_entries"), &BeamTracer::plan_items);
	ClassDB::bind_method(D_METHOD("trace_pair", "defs", "player", "cursor", "plan_entries", "flags", "segments"), &BeamTracer::trace_pair,
			DEFVAL(Dictionary()), DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("plan_refuses", "defs", "plan_entries", "surface_id", "left", "flags"), &BeamTracer::plan_refuses, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("clear_cache"), &BeamTracer::clear_cache);
	ClassDB::bind_method(D_METHOD("visibility", "defs", "player", "plan_entries", "flags", "bounds"), &BeamTracer::visibility);
	ClassDB::bind_method(D_METHOD("infinity_point", "defs", "plan_entries", "flags"), &BeamTracer::infinity_point, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("set_defs_key", "key"), &BeamTracer::set_defs_key);
	ClassDB::bind_method(D_METHOD("get_defs_key"), &BeamTracer::get_defs_key);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "defs_key"), "set_defs_key", "get_defs_key");
	ClassDB::bind_method(D_METHOD("set_emit_infinity", "on"), &BeamTracer::set_emit_infinity);
	ClassDB::bind_method(D_METHOD("get_emit_infinity"), &BeamTracer::get_emit_infinity);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "emit_infinity"), "set_emit_infinity", "get_emit_infinity");
	ClassDB::bind_method(D_METHOD("to_path", "events", "errs", "limbs", "frames", "aim", "flags", "segments"), &BeamTracer::to_path,
			DEFVAL(Variant()), DEFVAL(Dictionary()), DEFVAL(Dictionary()));
}

bool BeamTracer::scene_supported(const Array &defs) {
	return rc::scene_supported(defs_from_array(defs));
}

Array BeamTracer::plan_items(const Array &plan_entries) {
	Array out;
	for (const rc::PlanItem &p : plan_from_items(plan_entries)) {
		Variant hint;
		if (p.hint) {
			hint = Vector2(p.hint->x, p.hint->y);
		}
		out.push_back(BeamSolverPlanItem::make(p.surface_id, p.left, hint));
	}
	return out;
}

Variant BeamTracer::infinity_point(const Array &defs, const Array &plan_entries, const Dictionary &flags) {
	use_defs(defs);
	const std::optional<rc::V2> q = tracer.infinity_point(plan_from_items(plan_entries), flags_from_dict(flags));
	return q ? Variant(Vector2(q->x, q->y)) : Variant();
}

void BeamTracer::use_defs(const Array &defs) {
	if (defs_key >= 0 && defs_key == converted_key) {
		return;
	}
	tracer.set_defs(defs_from_array(defs));
	converted_key = defs_key;
}

Ref<BeamTracerPair> BeamTracer::trace_pair(const Array &defs, const Vector2 &player, const Vector2 &cursor, const Array &plan_entries,
		const Dictionary &flags, const Dictionary &segments) {
	use_defs(defs);
	const std::optional<rc::TracePair> pair = tracer.trace_pair(to_v2(player), to_v2(cursor), plan_from_items(plan_entries), flags_from_dict(flags));
	if (!pair) {
		return Ref<BeamTracerPair>();
	}
	Ref<BeamTracerPair> o;
	o.instantiate();
	RayCache rays;
	o->physical = TracePath::wrap(pair->physical, segments, rays);
	if (pair->shared) {
		// one trajectory: the same step objects (BeamTracer.trace_pair duplicates the Array)
		o->planned.instantiate();
		o->planned->steps = o->physical->steps.duplicate();
		o->planned->targets_hit = o->physical->targets_hit.duplicate();
		o->planned->cursor_index = o->physical->cursor_index;
		o->planned->hit_count = o->physical->hit_count;
		o->planned->truncated = o->physical->truncated;
	} else {
		o->planned = TracePath::wrap(pair->planned, segments, rays);
	}
	o->result = PrecisionLadderResult::wrap(pair->result);
	o->final_flags = flags_to_dict(pair->final_flags);
	o->cursor_point_agrees = pair->cursor_point_agrees;
	return o;
}

Ref<godot::RefCounted> BeamTracer::visibility(const Array &defs, const Vector2 &player, const Array &plan_entries, const Dictionary &flags,
		const godot::Rect2 &bounds) {
	use_defs(defs);
	Ref<VisibilityRegion> o;
	o.instantiate();
	o->tracer = Ref<BeamTracer>(this);
	o->plan = plan_from_items(plan_entries);
	o->flags = flags_from_dict(flags);
	o->bounds = rc::vis::Rect{bounds.position.x, bounds.position.y, bounds.position.x + bounds.size.x, bounds.position.y + bounds.size.y};
	o->region = rc::vis::compute(tracer, rc::vis::Pt{player.x, player.y}, o->plan, o->flags, o->bounds);
	return o;
}

bool BeamTracer::plan_refuses(const Array &defs, const Array &plan_entries, int surface_id, bool left, const Dictionary &flags) {
	use_defs(defs);
	return tracer.plan_refuses(plan_from_items(plan_entries), surface_id, left, flags_from_dict(flags));
}

namespace {

template <class N>
std::vector<rc::Event<N>> events_of(const Array &events) {
	std::vector<rc::Event<N>> out;
	for (int64_t i = 0; i < events.size(); i++) {
		Ref<BeamSolverEvent> e = events[i];
		if (e.is_valid()) {
			if (const auto *ev = std::get_if<rc::Event<N>>(&e->event)) {
				out.push_back(*ev);
			}
		}
	}
	return out;
}

// F64 events as Dual events: the same float64 values, no derivatives.
rc::Event<rc::Dual> dual_of(const rc::Event<rc::F64> &e) {
	rc::Event<rc::Dual> d;
	d.kind = e.kind;
	d.surface_id = e.surface_id;
	d.left = e.left;
	d.applied = e.applied;
	d.terminal = e.terminal;
	d.on_segment = e.on_segment;
	d.rehit = e.rehit;
	d.popped = e.popped;
	d.teleport = e.teleport;
	d.projective = e.projective;
	d.epoch = e.epoch;
	if (e.point) {
		d.point = rc::CN<rc::Dual>{rc::Dual{e.point->re.v, 0.0}, rc::Dual{e.point->im.v, 0.0}};
	}
	if (e.beam) {
		d.beam = std::make_shared<const rc::Beam<rc::Dual>>(rc::Beam<rc::Dual>{{rc::Dual{e.beam->p.re.v, 0.0}, rc::Dual{e.beam->p.im.v, 0.0}},
			{rc::Dual{e.beam->tau.re.v, 0.0}, rc::Dual{e.beam->tau.im.v, 0.0}}, rc::Dual{e.beam->kappa.v, 0.0}});
	}
	if (e.t_from) {
		d.t_from = rc::Dual{e.t_from->v, 0.0};
	}
	if (e.t_to) {
		d.t_to = rc::Dual{e.t_to->v, 0.0};
	}
	d.margin = e.margin;
	d.seeded = e.seeded;
	d.state = e.state;
	d.fired = e.fired;
	d.flags = e.flags;
	d.anchored = e.anchored;
	d.beam_view = e.beam_view;
	return d;
}

} // namespace

Ref<TracePath> BeamTracer::to_path(const Array &events, const Array &errs, int limbs, const Dictionary &frames, const Variant &aim,
		const Dictionary &flags, const Dictionary &segments) {
	if (tracer.defs().empty()) {
		tracer.set_defs({}); // a solve context without surfaces: no targets, no hit segments
	}
	rc::Climb climb;
	climb.limbs = limbs;
	if (events.size() > 0) {
		Ref<BeamSolverEvent> e0 = events[0];
		if (e0.is_valid() && std::holds_alternative<rc::Event<rc::BigFloat>>(e0->event)) {
			climb.big_events = events_of<rc::BigFloat>(events);
			if (limbs == 0) {
				climb.limbs = int(std::get<rc::Event<rc::BigFloat>>(e0->event).beam->kappa.limbs.size());
			}
		} else if (e0.is_valid() && std::holds_alternative<rc::Event<rc::Dual>>(e0->event)) {
			climb.f64_events = events_of<rc::Dual>(events);
			climb.limbs = 0;
		} else {
			for (const rc::Event<rc::F64> &e : events_of<rc::F64>(events)) {
				climb.f64_events.push_back(dual_of(e));
			}
			climb.limbs = 0;
		}
	}
	for (int64_t i = 0; i < errs.size(); i++) {
		climb.err.push_back(double(errs[i]));
	}
	std::map<std::string, int> fm;
	const Array keys = frames.keys();
	for (int64_t i = 0; i < keys.size(); i++) {
		const Dictionary entry = frames[keys[i]];
		fm[to_std(godot::String(keys[i]))] = int(int64_t(entry["id"]));
	}
	rc::Ray ray;
	const Ref<Ray> aim_ray = aim;
	if (aim_ray.is_valid()) {
		ray = aim_ray->to_rc();
	}
	rc::TracePath path = tracer.to_path(climb, fm, ray, flags_from_dict(flags));
	if (aim_ray.is_null()) {
		for (rc::TraceStep &s : path.steps) {
			s.ray.reset();
		}
	}
	Dictionary &out_frames = const_cast<Dictionary &>(frames); // Dictionary is a shared reference type
	for (const auto &kv : fm) {
		const godot::String k = to_gd(kv.first);
		if (!out_frames.has(k)) {
			Dictionary entry;
			entry["id"] = kv.second;
			out_frames[k] = entry;
		}
	}
	RayCache rays;
	return TracePath::wrap(path, segments, rays);
}
