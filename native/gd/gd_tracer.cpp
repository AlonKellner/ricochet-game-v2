#include "gd_tracer.hpp"

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

template <class T>
PropertyInfo obj_prop(const char *name, const char *cls) {
	return PropertyInfo(Variant::OBJECT, name, godot::PROPERTY_HINT_RESOURCE_TYPE, cls);
}

} // namespace

// ---------------------------------------------------------------- NDirection

void NDirection::_bind_methods() {
	ClassDB::bind_static_method("NDirection", D_METHOD("from_coords", "start", "end"), &NDirection::from_coords);
	ClassDB::bind_method(D_METHOD("get_start"), &NDirection::get_start);
	ClassDB::bind_method(D_METHOD("set_start", "v"), &NDirection::set_start);
	ClassDB::bind_method(D_METHOD("get_end"), &NDirection::get_end);
	ClassDB::bind_method(D_METHOD("set_end", "v"), &NDirection::set_end);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "start"), "set_start", "get_start");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "end"), "set_end", "get_end");
	ClassDB::bind_method(D_METHOD("is_zero_length"), &NDirection::is_zero_length);
	ClassDB::bind_method(D_METHOD("to_vector"), &NDirection::to_vector);
	ClassDB::bind_method(D_METHOD("to_normalized"), &NDirection::to_normalized);
}

Ref<NDirection> NDirection::from_coords(const Vector2 &p_start, const Vector2 &p_end) {
	Ref<NDirection> d;
	d.instantiate();
	d->start = p_start;
	d->end = p_end;
	return d;
}

Vector2 NDirection::to_normalized() const {
	// Direction.to_normalized: Vector2.normalized() of the float32 vector (zero stays zero)
	return to_vec(rc::Ray{to_v2(Vector2()), to_v2(start), to_v2(end)}.to_normalized());
}

// ---------------------------------------------------------------- NRay

void NRay::_bind_methods() {
	ClassDB::bind_static_method("NRay", D_METHOD("from_coords", "origin", "direction"), &NRay::from_coords);
	ClassDB::bind_method(D_METHOD("get_origin"), &NRay::get_origin);
	ClassDB::bind_method(D_METHOD("set_origin", "v"), &NRay::set_origin);
	ClassDB::bind_method(D_METHOD("get_direction"), &NRay::get_direction);
	ClassDB::bind_method(D_METHOD("set_direction", "d"), &NRay::set_direction);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "origin"), "set_origin", "get_origin");
	ADD_PROPERTY(obj_prop<NDirection>("direction", "NDirection"), "set_direction", "get_direction");
}

Ref<NRay> NRay::from_coords(const Vector2 &p_origin, const Ref<NDirection> &p_direction) {
	Ref<NRay> r;
	r.instantiate();
	r->origin = p_origin;
	r->direction = p_direction;
	return r;
}

Ref<NRay> NRay::wrap(const rc::Ray &r) {
	return from_coords(to_vec(r.origin), NDirection::from_coords(to_vec(r.dir_start), to_vec(r.dir_end)));
}

rc::Ray NRay::to_rc() const {
	rc::Ray r;
	r.origin = to_v2(origin);
	if (direction.is_valid()) {
		r.dir_start = to_v2(direction->start);
		r.dir_end = to_v2(direction->end);
	}
	return r;
}

// ---------------------------------------------------------------- NTraceHit

void NTraceHit::_bind_methods() {
	ClassDB::bind_static_method("NTraceHit", D_METHOD("make", "t", "point", "surface_id", "side", "on_segment"), &NTraceHit::make);
#define RC_HIT_PROP(name, vtype)                                                      \
	ClassDB::bind_method(D_METHOD("get_" #name), &NTraceHit::get_##name);             \
	ClassDB::bind_method(D_METHOD("set_" #name, "v"), &NTraceHit::set_##name);        \
	ADD_PROPERTY(PropertyInfo(Variant::vtype, #name), "set_" #name, "get_" #name);
	RC_HIT_PROP(t, FLOAT)
	RC_HIT_PROP(point, VECTOR2)
	RC_HIT_PROP(surface_id, INT)
	RC_HIT_PROP(side, INT)
	RC_HIT_PROP(on_segment, BOOL)
#undef RC_HIT_PROP
}

Ref<NTraceHit> NTraceHit::make(double p_t, const Vector2 &p_point, int p_surface_id, int p_side, bool p_on_segment) {
	Ref<NTraceHit> h;
	h.instantiate();
	h->t = p_t;
	h->point = p_point;
	h->surface_id = p_surface_id;
	h->side = p_side;
	h->on_segment = p_on_segment;
	return h;
}

Ref<NTraceHit> NTraceHit::wrap(const rc::TraceHit &h) {
	return make(h.t, to_vec(h.point), h.surface_id, int(h.side), h.on_segment);
}

rc::TraceHit NTraceHit::to_rc() const {
	rc::TraceHit h;
	h.t = t;
	h.point = to_v2(point);
	h.surface_id = surface_id;
	h.side = rc::Side(side);
	h.on_segment = on_segment;
	return h;
}

// ---------------------------------------------------------------- NTraceStep

void NTraceStep::_bind_methods() {
	ClassDB::bind_static_method("NTraceStep", D_METHOD("make", "start", "end", "frame_id", "hit", "ray", "via", "is_arc"), &NTraceStep::make,
			DEFVAL(0), DEFVAL(Variant()), DEFVAL(Variant()), DEFVAL(Vector2()), DEFVAL(false));
#define RC_STEP_PROP(name, vtype)                                                     \
	ClassDB::bind_method(D_METHOD("get_" #name), &NTraceStep::get_##name);            \
	ClassDB::bind_method(D_METHOD("set_" #name, "v"), &NTraceStep::set_##name);       \
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
	ClassDB::bind_method(D_METHOD("get_hit"), &NTraceStep::get_hit);
	ClassDB::bind_method(D_METHOD("set_hit", "v"), &NTraceStep::set_hit);
	ADD_PROPERTY(obj_prop<NTraceHit>("hit", "NTraceHit"), "set_hit", "get_hit");
	ClassDB::bind_method(D_METHOD("get_ray"), &NTraceStep::get_ray);
	ClassDB::bind_method(D_METHOD("set_ray", "v"), &NTraceStep::set_ray);
	ADD_PROPERTY(obj_prop<NRay>("ray", "NRay"), "set_ray", "get_ray");
	ClassDB::bind_method(D_METHOD("with_type", "new_type"), &NTraceStep::with_type);
	ClassDB::bind_method(D_METHOD("copy_meta_from", "o"), &NTraceStep::copy_meta_from);
	BIND_CONSTANT(IDENTITY_FRAME);
}

Ref<NTraceStep> NTraceStep::make(const Vector2 &p_start, const Vector2 &p_end, int p_frame_id, const Ref<NTraceHit> &p_hit, const Ref<NRay> &p_ray,
		const Vector2 &p_via, bool p_is_arc) {
	Ref<NTraceStep> s;
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

Ref<NTraceStep> NTraceStep::wrap(const rc::TraceStep &st) {
	Ref<NTraceStep> s;
	s.instantiate();
	s->start = to_vec(st.start);
	s->end = to_vec(st.end);
	s->via = to_vec(st.via);
	s->frame_id = st.frame_id;
	if (st.hit) {
		s->hit = NTraceHit::wrap(*st.hit);
	}
	if (st.ray) {
		s->ray = NRay::wrap(*st.ray);
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

rc::TraceStep NTraceStep::to_rc() const {
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

Ref<NTraceStep> NTraceStep::with_type(int new_type) const {
	Ref<NTraceStep> copy = make(start, end, frame_id, hit, ray, via, is_arc_step);
	copy->type = new_type;
	copy->copy_meta_from(Ref<NTraceStep>(this));
	return copy;
}

void NTraceStep::copy_meta_from(const Ref<NTraceStep> &o) {
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

// ---------------------------------------------------------------- NTracePath

void NTracePath::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_steps"), &NTracePath::get_steps);
	ClassDB::bind_method(D_METHOD("set_steps", "v"), &NTracePath::set_steps);
	ClassDB::bind_method(D_METHOD("get_targets_hit"), &NTracePath::get_targets_hit);
	ClassDB::bind_method(D_METHOD("set_targets_hit", "v"), &NTracePath::set_targets_hit);
	ClassDB::bind_method(D_METHOD("get_cursor_index"), &NTracePath::get_cursor_index);
	ClassDB::bind_method(D_METHOD("set_cursor_index", "v"), &NTracePath::set_cursor_index);
	ClassDB::bind_method(D_METHOD("get_hit_count"), &NTracePath::get_hit_count);
	ClassDB::bind_method(D_METHOD("set_hit_count", "v"), &NTracePath::set_hit_count);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "steps"), "set_steps", "get_steps");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "targets_hit"), "set_targets_hit", "get_targets_hit");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cursor_index"), "set_cursor_index", "get_cursor_index");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "hit_count"), "set_hit_count", "get_hit_count");
}

Ref<NTracePath> NTracePath::wrap(const rc::TracePath &p) {
	Ref<NTracePath> o;
	o.instantiate();
	for (const rc::TraceStep &s : p.steps) {
		o->steps.push_back(NTraceStep::wrap(s));
	}
	for (int id : p.targets_hit) {
		o->targets_hit[id] = true;
	}
	o->cursor_index = p.cursor_index;
	o->hit_count = p.hit_count;
	return o;
}

// ---------------------------------------------------------------- NBeamTracerPair

void NBeamTracerPair::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_planned"), &NBeamTracerPair::get_planned);
	ClassDB::bind_method(D_METHOD("get_physical"), &NBeamTracerPair::get_physical);
	ClassDB::bind_method(D_METHOD("get_result"), &NBeamTracerPair::get_result);
	ClassDB::bind_method(D_METHOD("get_final_flags"), &NBeamTracerPair::get_final_flags);
	ADD_PROPERTY(obj_prop<NTracePath>("planned", "NTracePath"), "", "get_planned");
	ADD_PROPERTY(obj_prop<NTracePath>("physical", "NTracePath"), "", "get_physical");
	ADD_PROPERTY(obj_prop<NPrecisionLadderResult>("result", "NPrecisionLadderResult"), "", "get_result");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "final_flags"), "", "get_final_flags");
}

// ---------------------------------------------------------------- NBeamTracer

void NBeamTracer::_bind_methods() {
	ClassDB::bind_static_method("NBeamTracer", D_METHOD("TOL"), &NBeamTracer::TOL);
	ClassDB::bind_static_method("NBeamTracer", D_METHOD("scene_supported", "defs"), &NBeamTracer::scene_supported);
	ClassDB::bind_static_method("NBeamTracer", D_METHOD("plan_items", "plan_entries"), &NBeamTracer::plan_items);
	ClassDB::bind_method(D_METHOD("trace_pair", "defs", "player", "cursor", "plan_entries", "flags"), &NBeamTracer::trace_pair, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("plan_refuses", "defs", "plan_entries", "surface_id", "left", "flags"), &NBeamTracer::plan_refuses, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("clear_cache"), &NBeamTracer::clear_cache);
	ClassDB::bind_method(D_METHOD("to_path", "events", "errs", "limbs", "frames", "aim", "flags"), &NBeamTracer::to_path,
			DEFVAL(Variant()), DEFVAL(Dictionary()));
}

bool NBeamTracer::scene_supported(const Array &defs) {
	return rc::scene_supported(defs_from_array(defs));
}

Array NBeamTracer::plan_items(const Array &plan_entries) {
	Array out;
	for (const rc::PlanItem &p : plan_from_items(plan_entries)) {
		Variant hint;
		if (p.hint) {
			hint = Vector2(p.hint->x, p.hint->y);
		}
		out.push_back(NBeamSolverPlanItem::make(p.surface_id, p.left, hint));
	}
	return out;
}

Ref<NBeamTracerPair> NBeamTracer::trace_pair(const Array &defs, const Vector2 &player, const Vector2 &cursor, const Array &plan_entries,
		const Dictionary &flags) {
	tracer.set_defs(defs_from_array(defs));
	const std::optional<rc::TracePair> pair = tracer.trace_pair(to_v2(player), to_v2(cursor), plan_from_items(plan_entries), flags_from_dict(flags));
	if (!pair) {
		return Ref<NBeamTracerPair>();
	}
	Ref<NBeamTracerPair> o;
	o.instantiate();
	o->planned = NTracePath::wrap(pair->planned);
	o->physical = NTracePath::wrap(pair->physical);
	o->result = NPrecisionLadderResult::wrap(pair->result);
	o->final_flags = flags_to_dict(pair->final_flags);
	return o;
}

bool NBeamTracer::plan_refuses(const Array &defs, const Array &plan_entries, int surface_id, bool left, const Dictionary &flags) {
	tracer.set_defs(defs_from_array(defs));
	return tracer.plan_refuses(plan_from_items(plan_entries), surface_id, left, flags_from_dict(flags));
}

namespace {

template <class N>
std::vector<rc::Event<N>> events_of(const Array &events) {
	std::vector<rc::Event<N>> out;
	for (int64_t i = 0; i < events.size(); i++) {
		Ref<NBeamSolverEvent> e = events[i];
		if (e.is_valid()) {
			if (const auto *ev = std::get_if<rc::Event<N>>(&e->event)) {
				out.push_back(*ev);
			}
		}
	}
	return out;
}

} // namespace

Ref<NTracePath> NBeamTracer::to_path(const Array &events, const Array &errs, int limbs, const Dictionary &frames, const Variant &aim,
		const Dictionary &flags) {
	rc::Climb climb;
	climb.limbs = limbs;
	if (events.size() > 0) {
		Ref<NBeamSolverEvent> e0 = events[0];
		if (e0.is_valid() && std::holds_alternative<rc::Event<rc::BigFloat>>(e0->event)) {
			climb.big_events = events_of<rc::BigFloat>(events);
			if (limbs == 0) {
				climb.limbs = int(std::get<rc::Event<rc::BigFloat>>(e0->event).beam->kappa.limbs.size());
			}
		} else if (e0.is_valid() && std::holds_alternative<rc::Event<rc::Dual>>(e0->event)) {
			climb.f64_events = events_of<rc::Dual>(events);
			climb.limbs = 0;
		} else {
			// F64 events: the same float64 values as the Dual tier
			for (const rc::Event<rc::F64> &e : events_of<rc::F64>(events)) {
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
				d.flags = e.flags;
				d.anchored = e.anchored;
				d.beam_view = e.beam_view;
				climb.f64_events.push_back(std::move(d));
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
	const Ref<NRay> aim_ray = aim;
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
	return NTracePath::wrap(path);
}
