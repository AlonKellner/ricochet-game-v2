#include "gd_solver.hpp"

#include "rc/beam_effects.hpp"
#include "rc/beam_geom.hpp"

#include "rc/error.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdio>
#include <map>

using godot::Array;
using godot::ClassDB;
using godot::D_METHOD;
using godot::Dictionary;
using godot::PackedFloat64Array;
using godot::PropertyInfo;
using godot::Ref;
using godot::String;
using godot::Variant;
using namespace rcgd;

namespace {

template <class T>
struct scene_num;
template <class N>
struct scene_num<std::shared_ptr<rc::Scene<N>>> {
	using type = N;
};

template <class N>
const rc::CN<N> *cn_of(const Ref<CN> &c) {
	return c.is_null() ? nullptr : std::get_if<rc::CN<N>>(&c->value);
}
template <class N>
const N *num_ref(const Ref<Num> &n) {
	return n.is_null() ? nullptr : std::get_if<N>(&n->value);
}

void tier_error(const char *where) {
	rc::report_error((std::string(where) + ": arguments of different precision tiers").c_str());
}

rc::Mode mode_of(int m) {
	return m == int(rc::Mode::Planned) ? rc::Mode::Planned : rc::Mode::Physical;
}

bool null_beam(const BeamV &bv) {
	return std::visit([](const auto &b) { return b == nullptr; }, bv);
}

template <class N>
std::vector<rc::Event<N>> events_of(const Array &events) {
	std::vector<rc::Event<N>> out;
	for (int64_t i = 0; i < events.size(); i++) {
		Ref<BeamSolverEvent> e = events[i];
		if (e.is_null()) {
			continue;
		}
		if (const auto *ev = std::get_if<rc::Event<N>>(&e->event)) {
			out.push_back(*ev);
		}
	}
	return out;
}

template <class N>
Array events_array(const std::vector<rc::Event<N>> &ev) {
	Array a;
	// one HPBeam object per beam identity (beam_view; the raw pointer when unviewed)
	std::map<std::pair<int, const void *>, Ref<HPBeam>> beams;
	for (const rc::Event<N> &e : ev) {
		Ref<BeamSolverEvent> w = BeamSolverEvent::wrap(EventV(e));
		if (e.beam) {
			const auto key = std::make_pair(e.beam_view, e.beam_view >= 0 ? nullptr : static_cast<const void *>(e.beam.get()));
			auto it = beams.find(key);
			if (it == beams.end()) {
				it = beams.emplace(key, HPBeam::wrap(BeamV(e.beam))).first;
			}
			w->beam_obj = it->second;
		}
		a.push_back(w);
	}
	return a;
}

Array names_of(const std::vector<rc::EffectType> &effs) {
	Array a;
	for (rc::EffectType e : effs) {
		a.push_back(effect_name(e));
	}
	return a;
}

// a (player / cursor) pair from a PackedFloat64Array or an Array, as FastSolver._cn
std::optional<rc::CN<rc::Dual>> dual_pair(const Variant &v) {
	if (v.get_type() == Variant::PACKED_FLOAT64_ARRAY) {
		const PackedFloat64Array a = v;
		if (a.size() >= 2) {
			return rc::CN<rc::Dual>{rc::Dual{a[0], 0.0}, rc::Dual{a[1], 0.0}};
		}
		return std::nullopt;
	}
	if (v.get_type() == Variant::ARRAY) {
		const Array a = v;
		if (a.size() >= 2) {
			return rc::CN<rc::Dual>{rc::Dual{double(a[0]), 0.0}, rc::Dual{double(a[1]), 0.0}};
		}
	}
	return std::nullopt;
}

} // namespace

Array events_to_array(const std::vector<rc::Event<rc::F64>> &ev) { return events_array(ev); }
Array events_to_array(const std::vector<rc::Event<rc::Dual>> &ev) { return events_array(ev); }
Array events_to_array(const std::vector<rc::Event<rc::BigFloat>> &ev) { return events_array(ev); }

// ---------------------------------------------------------------- HPBeam

void HPBeam::_bind_methods() {
	ClassDB::bind_static_method("HPBeam", D_METHOD("make", "p", "tau", "kappa"), &HPBeam::make);
	ClassDB::bind_method(D_METHOD("get_p"), &HPBeam::get_p);
	ClassDB::bind_method(D_METHOD("get_tau"), &HPBeam::get_tau);
	ClassDB::bind_method(D_METHOD("get_kappa"), &HPBeam::get_kappa);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "p", godot::PROPERTY_HINT_RESOURCE_TYPE, "CN", godot::PROPERTY_USAGE_NONE), "", "get_p");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "tau", godot::PROPERTY_HINT_RESOURCE_TYPE, "CN", godot::PROPERTY_USAGE_NONE), "", "get_tau");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "kappa", godot::PROPERTY_HINT_RESOURCE_TYPE, "Num", godot::PROPERTY_USAGE_NONE), "", "get_kappa");
	ClassDB::bind_method(D_METHOD("at", "t"), &HPBeam::at);
	ClassDB::bind_method(D_METHOD("at_infinity"), &HPBeam::at_infinity);
	ClassDB::bind_method(D_METHOD("tangent_at", "t"), &HPBeam::tangent_at);
	ClassDB::bind_method(D_METHOD("param_of", "z"), &HPBeam::param_of);
	ClassDB::bind_static_method("HPBeam", D_METHOD("inf_like", "x"), &HPBeam::inf_like);
	ClassDB::bind_static_method("HPBeam", D_METHOD("through", "P", "X", "delta"), &HPBeam::through);
	ClassDB::bind_static_method("HPBeam", D_METHOD("invert_point", "z", "circ"), &HPBeam::invert_point);
	ClassDB::bind_method(D_METHOD("hits", "circ", "depart_on"), &HPBeam::hits);
	ClassDB::bind_method(D_METHOD("invert_at", "t", "circ", "h_exact"), &HPBeam::invert_at, DEFVAL(Variant()));
}

Ref<HPBeam> HPBeam::wrap(BeamV b) {
	Ref<HPBeam> o;
	o.instantiate();
	o->beam = std::move(b);
	return o;
}

Ref<HPBeam> HPBeam::make(const Ref<CN> &p, const Ref<CN> &tau, const Ref<Num> &kappa) {
	if (p.is_null() || tau.is_null() || kappa.is_null()) {
		rc::report_error("HPBeam.make: null argument");
		return Ref<HPBeam>();
	}
	return std::visit([&](const auto &pp) -> Ref<HPBeam> {
		using N = std::decay_t<decltype(pp.re)>;
		const rc::CN<N> *tt = cn_of<N>(tau);
		const N *kk = num_ref<N>(kappa);
		if (tt == nullptr || kk == nullptr) {
			tier_error("HPBeam.make");
			return Ref<HPBeam>();
		}
		return wrap(std::make_shared<const rc::Beam<N>>(rc::Beam<N>{pp, *tt, *kk}));
	}, p->value);
}

Ref<CN> HPBeam::get_p() const {
	if (null_beam(beam)) {
		return Ref<CN>();
	}
	return std::visit([](const auto &b) { return CN::wrap(b->p); }, beam);
}
Ref<CN> HPBeam::get_tau() const {
	if (null_beam(beam)) {
		return Ref<CN>();
	}
	return std::visit([](const auto &b) { return CN::wrap(b->tau); }, beam);
}
Ref<Num> HPBeam::get_kappa() const {
	if (null_beam(beam)) {
		return Ref<Num>();
	}
	return std::visit([](const auto &b) { return Num::wrap(b->kappa); }, beam);
}
Ref<CN> HPBeam::at(const Ref<Num> &t) const {
	if (null_beam(beam) || t.is_null()) {
		return Ref<CN>();
	}
	return std::visit([&](const auto &b) -> Ref<CN> {
		using N = std::decay_t<decltype(b->kappa)>;
		const N *tt = num_ref<N>(t);
		if (tt == nullptr) {
			tier_error("HPBeam.at");
			return Ref<CN>();
		}
		if (tt->is_inf() && b->kappa.is_zero()) {
			return Ref<CN>(); // a line has no point at infinity
		}
		return CN::wrap(b->at(*tt));
	}, beam);
}
Variant HPBeam::at_infinity() const {
	if (null_beam(beam)) {
		return Variant();
	}
	return std::visit([](const auto &b) -> Variant {
		const auto z = b->at_infinity();
		return z ? Variant(CN::wrap(*z)) : Variant();
	}, beam);
}
Ref<CN> HPBeam::tangent_at(const Ref<Num> &t) const {
	if (null_beam(beam) || t.is_null()) {
		return Ref<CN>();
	}
	return std::visit([&](const auto &b) -> Ref<CN> {
		using N = std::decay_t<decltype(b->kappa)>;
		const N *tt = num_ref<N>(t);
		if (tt == nullptr) {
			tier_error("HPBeam.tangent_at");
			return Ref<CN>();
		}
		return CN::wrap(b->tangent_at(*tt));
	}, beam);
}
Ref<Num> HPBeam::param_of(const Ref<CN> &z) const {
	if (null_beam(beam) || z.is_null()) {
		return Ref<Num>();
	}
	return std::visit([&](const auto &b) -> Ref<Num> {
		using N = std::decay_t<decltype(b->kappa)>;
		const rc::CN<N> *zz = cn_of<N>(z);
		if (zz == nullptr) {
			tier_error("HPBeam.param_of");
			return Ref<Num>();
		}
		return Num::wrap(b->param_of(*zz));
	}, beam);
}

// ---------------------------------------------------------------- BeamSolverPlanItem

void BeamSolverPlanItem::_bind_methods() {
	ClassDB::bind_static_method("BeamSolverPlanItem", D_METHOD("make", "sid", "left", "hint"), &BeamSolverPlanItem::make, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("get_surface_id"), &BeamSolverPlanItem::get_surface_id);
	ClassDB::bind_method(D_METHOD("set_surface_id", "v"), &BeamSolverPlanItem::set_surface_id);
	ClassDB::bind_method(D_METHOD("get_left"), &BeamSolverPlanItem::get_left);
	ClassDB::bind_method(D_METHOD("set_left", "v"), &BeamSolverPlanItem::set_left);
	ClassDB::bind_method(D_METHOD("get_hint"), &BeamSolverPlanItem::get_hint);
	ClassDB::bind_method(D_METHOD("set_hint", "v"), &BeamSolverPlanItem::set_hint);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "surface_id"), "set_surface_id", "get_surface_id");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "left"), "set_left", "get_left");
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "hint", godot::PROPERTY_HINT_NONE, "", godot::PROPERTY_USAGE_DEFAULT | godot::PROPERTY_USAGE_NIL_IS_VARIANT), "set_hint", "get_hint");
	ClassDB::bind_method(D_METHOD("get_span"), &BeamSolverPlanItem::get_span);
	ClassDB::bind_method(D_METHOD("set_span", "v"), &BeamSolverPlanItem::set_span);
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "span", godot::PROPERTY_HINT_NONE, "", godot::PROPERTY_USAGE_DEFAULT | godot::PROPERTY_USAGE_NIL_IS_VARIANT), "set_span", "get_span");
}

Ref<BeamSolverPlanItem> BeamSolverPlanItem::make(int sid, bool p_left, const Variant &p_hint) {
	Ref<BeamSolverPlanItem> o;
	o.instantiate();
	o->surface_id = sid;
	o->left = p_left;
	o->hint = p_hint;
	return o;
}

// ---------------------------------------------------------------- BeamSolverEvent

#define RC_EV_GETTER(name, type, expr)                            \
	type BeamSolverEvent::get_##name() const {                   \
		return visit([](const auto &e) { return type(expr); });   \
	}

RC_EV_GETTER(surface_id, int, e.surface_id)
RC_EV_GETTER(left, bool, e.left)
RC_EV_GETTER(applied, bool, e.applied)
RC_EV_GETTER(terminal, bool, e.terminal)
RC_EV_GETTER(on_segment, bool, e.on_segment)
RC_EV_GETTER(rehit, bool, e.rehit)
RC_EV_GETTER(popped, bool, e.popped)
RC_EV_GETTER(teleport, bool, e.teleport)
RC_EV_GETTER(projective, bool, e.projective)
RC_EV_GETTER(epoch, int, e.epoch)
RC_EV_GETTER(margin, double, e.margin)
RC_EV_GETTER(seeded, bool, e.seeded)
RC_EV_GETTER(state, bool, e.state)
RC_EV_GETTER(fired, bool, e.fired)
RC_EV_GETTER(anchored, bool, e.anchored)
RC_EV_GETTER(beam_view, int, e.beam_view)
#undef RC_EV_GETTER

Ref<BeamSolverEvent> BeamSolverEvent::wrap(EventV e) {
	Ref<BeamSolverEvent> o;
	o.instantiate();
	o->event = std::move(e);
	return o;
}
String BeamSolverEvent::get_kind() const {
	return visit([](const auto &e) { return String(rc::event_kind_name(e.kind)); });
}
Variant BeamSolverEvent::get_point() const {
	return visit([](const auto &e) -> Variant { return e.point ? Variant(CN::wrap(*e.point)) : Variant(); });
}
Ref<HPBeam> BeamSolverEvent::get_beam() const {
	if (beam_obj.is_valid()) {
		return beam_obj;
	}
	beam_obj = visit([](const auto &e) -> Ref<HPBeam> { return e.beam ? HPBeam::wrap(BeamV(e.beam)) : Ref<HPBeam>(); });
	return beam_obj;
}
void BeamSolverEvent::set_beam(const Ref<HPBeam> &b) {
	std::visit([&](auto &e) {
		using BP = std::decay_t<decltype(e.beam)>;
		if (b.is_null()) {
			e.beam = nullptr;
			beam_obj = Ref<HPBeam>();
			return;
		}
		const BP *bp = std::get_if<BP>(&b->beam);
		if (bp == nullptr) {
			tier_error("BeamSolverEvent.beam");
			return;
		}
		e.beam = *bp;
		e.beam_view = -1; // a replaced beam is its own object (GDScript identity)
		beam_obj = b;
	}, event);
}
void BeamSolverEvent::set_point(const Variant &v) {
	std::visit([&](auto &e) {
		using CNT = typename std::decay_t<decltype(e.point)>::value_type;
		if (v.get_type() == Variant::NIL) {
			e.point.reset();
			return;
		}
		const Ref<CN> c = v;
		const CNT *p = c.is_null() ? nullptr : std::get_if<CNT>(&c->value);
		if (p == nullptr) {
			tier_error("BeamSolverEvent.point");
			return;
		}
		e.point = *p;
	}, event);
}
Variant BeamSolverEvent::get_t_from() const {
	return visit([](const auto &e) -> Variant { return e.t_from ? Variant(Num::wrap(NumV(*e.t_from))) : Variant(); });
}
Variant BeamSolverEvent::get_t_to() const {
	return visit([](const auto &e) -> Variant { return e.t_to ? Variant(Num::wrap(NumV(*e.t_to))) : Variant(); });
}
Dictionary BeamSolverEvent::get_flags() const {
	return visit([](const auto &e) { return flags_to_dict(e.flags); });
}
String BeamSolverEvent::describe() const {
	return visit([](const auto &e) {
		char p[96] = "";
		if (e.point) {
			std::snprintf(p, sizeof p, " (%.6f, %.6f)", e.point->fx(), e.point->fy());
		}
		char head[64];
		std::snprintf(head, sizeof head, "%s s=%d %s", rc::event_kind_name(e.kind), e.surface_id, e.left ? "L" : "R");
		String s = String(head) + (e.applied ? " applied" : "") + (e.on_segment ? "" : " off") + String(p);
		if (e.state) {
			s += " state->" + godot::UtilityFunctions::str(Variant(flags_to_dict(e.flags)));
		}
		return s;
	});
}

void BeamSolverEvent::_bind_methods() {
#define RC_EV_PROP(name, vtype)                                                                 \
	ClassDB::bind_method(D_METHOD("get_" #name), &BeamSolverEvent::get_##name);                \
	ADD_PROPERTY(PropertyInfo(Variant::vtype, #name), "", "get_" #name);
	RC_EV_PROP(kind, STRING)
	RC_EV_PROP(surface_id, INT)
	RC_EV_PROP(left, BOOL)
	RC_EV_PROP(applied, BOOL)
	RC_EV_PROP(terminal, BOOL)
	RC_EV_PROP(on_segment, BOOL)
	RC_EV_PROP(rehit, BOOL)
	RC_EV_PROP(popped, BOOL)
	RC_EV_PROP(teleport, BOOL)
	RC_EV_PROP(projective, BOOL)
	RC_EV_PROP(epoch, INT)
	RC_EV_PROP(margin, FLOAT)
	RC_EV_PROP(seeded, BOOL)
	RC_EV_PROP(state, BOOL)
	RC_EV_PROP(fired, BOOL)
	RC_EV_PROP(flags, DICTIONARY)
	RC_EV_PROP(anchored, BOOL)
	RC_EV_PROP(beam_view, INT)
#undef RC_EV_PROP
#define RC_EV_NULLABLE(name)                                                                                           \
	ClassDB::bind_method(D_METHOD("get_" #name), &BeamSolverEvent::get_##name);                                       \
	ADD_PROPERTY(PropertyInfo(Variant::NIL, #name, godot::PROPERTY_HINT_NONE, "",                                      \
			godot::PROPERTY_USAGE_DEFAULT | godot::PROPERTY_USAGE_NIL_IS_VARIANT), "", "get_" #name);
	RC_EV_NULLABLE(t_from)
	RC_EV_NULLABLE(t_to)
#undef RC_EV_NULLABLE
	ClassDB::bind_method(D_METHOD("get_point"), &BeamSolverEvent::get_point);
	ClassDB::bind_method(D_METHOD("set_point", "v"), &BeamSolverEvent::set_point);
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "point", godot::PROPERTY_HINT_NONE, "", godot::PROPERTY_USAGE_DEFAULT | godot::PROPERTY_USAGE_NIL_IS_VARIANT), "set_point", "get_point");
	ClassDB::bind_method(D_METHOD("get_beam"), &BeamSolverEvent::get_beam);
	ClassDB::bind_method(D_METHOD("set_beam", "b"), &BeamSolverEvent::set_beam);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "beam", godot::PROPERTY_HINT_RESOURCE_TYPE, "HPBeam"), "set_beam", "get_beam");
	ClassDB::bind_method(D_METHOD("describe"), &BeamSolverEvent::describe);
}

// ---------------------------------------------------------------- BeamSolver

void BeamSolver::_bind_methods() {
	ClassDB::bind_static_method("BeamSolver", D_METHOD("trace_direction", "ctx", "scene", "player", "dir", "plan", "cursor", "mode", "max_events", "flags"),
			&BeamSolver::trace_direction, DEFVAL(MAX_HITS), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("BeamSolver", D_METHOD("planned", "ctx", "scene", "player", "cursor", "plan", "max_events", "flags"),
			&BeamSolver::planned, DEFVAL(MAX_HITS), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("BeamSolver", D_METHOD("physical", "ctx", "scene", "player", "cursor", "plan", "max_events", "flags"),
			&BeamSolver::physical, DEFVAL(MAX_HITS), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("BeamSolver", D_METHOD("plan_effects", "scene", "plan", "flags"), &BeamSolver::plan_effects, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("BeamSolver", D_METHOD("plan_flags", "scene", "plan", "flags"), &BeamSolver::plan_flags, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("BeamSolver", D_METHOD("plan_pole", "scene", "plan", "flags"), &BeamSolver::plan_pole, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("BeamSolver", D_METHOD("apply_state", "flags", "change"), &BeamSolver::apply_state);
	ClassDB::bind_static_method("BeamSolver", D_METHOD("fixes_cursor", "scene", "cursor", "plan", "flags"), &BeamSolver::fixes_cursor, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("BeamSolver", D_METHOD("targets_of", "scene", "events"), &BeamSolver::targets_of);
	ClassDB::bind_static_method("BeamSolver", D_METHOD("map_kind", "surf", "etype"), &BeamSolver::map_kind);
	ClassDB::bind_static_method("BeamSolver", D_METHOD("anchor_chain", "scene", "player", "cursor", "plan", "flags"), &BeamSolver::anchor_chain, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("BeamSolver", D_METHOD("TRANSFORMATIVE"), &BeamSolver::TRANSFORMATIVE);
	ClassDB::bind_static_method("BeamSolver", D_METHOD("PROJECTIVE"), &BeamSolver::PROJECTIVE);
	BIND_ENUM_CONSTANT(PHYSICAL);
	BIND_ENUM_CONSTANT(PLANNED);
	BIND_CONSTANT(MAX_HITS);
	BIND_CONSTANT(INFINITY_ID);
}

namespace {

// Runs `f(scene, player, cursor)` at the scene's tier; the ctx (when given) must agree.
template <class F>
Array with_solver(const char *where, const Ref<NumCtx> &ctx, const Ref<BeamScene> &scene, const Ref<CN> &player,
		const Variant &cursor, F &&f) {
	if (scene.is_null()) {
		rc::report_error((std::string(where) + ": null scene").c_str());
		return Array();
	}
	if (ctx.is_valid() && ctx->limbs != scene->limbs) {
		tier_error(where);
		return Array();
	}
	return std::visit([&](const auto &sc) -> Array {
		using N = typename scene_num<std::decay_t<decltype(sc)>>::type;
		if (sc == nullptr) {
			return Array();
		}
		const rc::CN<N> *pl = cn_of<N>(player);
		if (pl == nullptr) {
			tier_error(where);
			return Array();
		}
		std::optional<rc::CN<N>> cur;
		if (cursor.get_type() == Variant::OBJECT) {
			const Ref<CN> c = cursor;
			const rc::CN<N> *cc = cn_of<N>(c);
			if (cc == nullptr) {
				tier_error(where);
				return Array();
			}
			cur = *cc;
		}
		rc::Solver<N> solver(*sc);
		return f(solver, *pl, cur);
	}, scene->scene);
}

} // namespace

Array BeamSolver::trace_direction(const Ref<NumCtx> &ctx, const Ref<BeamScene> &scene, const Ref<CN> &player, const Ref<CN> &dir,
		const Array &plan, const Variant &cursor, int mode, int max_events, const Dictionary &flags) {
	const std::vector<rc::PlanItem> items = plan_from_items(plan);
	const rc::Flags f = flags_from_dict(flags);
	return with_solver("BeamSolver.trace_direction", ctx, scene, player, cursor, [&](auto &solver, const auto &pl, const auto &cur) -> Array {
		using N = std::decay_t<decltype(pl.re)>;
		const rc::CN<N> *dd = cn_of<N>(dir);
		if (dd == nullptr) {
			tier_error("BeamSolver.trace_direction");
			return Array();
		}
		rc::Walk<N> w = solver.run_direction(pl, *dd, items, cur, mode_of(mode), max_events, f);
		return events_to_array(w.events);
	});
}

Array BeamSolver::planned(const Ref<NumCtx> &ctx, const Ref<BeamScene> &scene, const Ref<CN> &player, const Ref<CN> &cursor,
		const Array &plan, int max_events, const Dictionary &flags) {
	const std::vector<rc::PlanItem> items = plan_from_items(plan);
	const rc::Flags f = flags_from_dict(flags);
	return with_solver("BeamSolver.planned", ctx, scene, player, Variant(cursor), [&](auto &solver, const auto &pl, const auto &cur) -> Array {
		if (!cur) {
			rc::report_error("BeamSolver.planned: null cursor");
			return Array();
		}
		return events_to_array(solver.planned(pl, *cur, items, max_events, f));
	});
}

Array BeamSolver::physical(const Ref<NumCtx> &ctx, const Ref<BeamScene> &scene, const Ref<CN> &player, const Ref<CN> &cursor,
		const Array &plan, int max_events, const Dictionary &flags) {
	const std::vector<rc::PlanItem> items = plan_from_items(plan);
	const rc::Flags f = flags_from_dict(flags);
	return with_solver("BeamSolver.physical", ctx, scene, player, Variant(cursor), [&](auto &solver, const auto &pl, const auto &cur) -> Array {
		if (!cur) {
			rc::report_error("BeamSolver.physical: null cursor");
			return Array();
		}
		return events_to_array(solver.physical(pl, *cur, items, max_events, f));
	});
}

Array BeamSolver::plan_effects(const Ref<BeamScene> &scene, const Array &plan, const Dictionary &flags) {
	if (scene.is_null()) {
		return Array();
	}
	const std::vector<rc::PlanItem> items = plan_from_items(plan);
	const rc::Flags f = flags_from_dict(flags);
	return std::visit([&](const auto &sc) -> Array {
		using N = typename scene_num<std::decay_t<decltype(sc)>>::type;
		if (sc == nullptr) {
			return Array();
		}
		return names_of(rc::Solver<N>(*sc).plan_effects(items, f));
	}, scene->scene);
}

Variant BeamSolver::plan_pole(const Ref<BeamScene> &scene, const Array &plan, const Dictionary &flags) {
	if (scene.is_null()) {
		return Variant();
	}
	const std::vector<rc::PlanItem> items = plan_from_items(plan);
	const rc::Flags f = flags_from_dict(flags);
	return std::visit([&](const auto &sc) -> Variant {
		using N = typename scene_num<std::decay_t<decltype(sc)>>::type;
		if (sc == nullptr) {
			return Variant();
		}
		const std::optional<rc::CN<N>> q = rc::Solver<N>(*sc).plan_pole(items, f);
		return q ? Variant(CN::wrap(*q)) : Variant();
	}, scene->scene);
}

Dictionary BeamSolver::plan_flags(const Ref<BeamScene> &scene, const Array &plan, const Dictionary &flags) {
	if (scene.is_null()) {
		return flags;
	}
	const std::vector<rc::PlanItem> items = plan_from_items(plan);
	const rc::Flags f = flags_from_dict(flags);
	return std::visit([&](const auto &sc) -> Dictionary {
		using N = typename scene_num<std::decay_t<decltype(sc)>>::type;
		if (sc == nullptr) {
			return flags;
		}
		return flags_to_dict(rc::Solver<N>(*sc).plan_flags(items, f));
	}, scene->scene);
}

Dictionary BeamSolver::apply_state(const Dictionary &flags, const Dictionary &change) {
	Dictionary out = flags.duplicate();
	const Variant key = change["key"];
	if (String(change["op"]) == String("toggle")) {
		out[key] = !bool(flags.get(key, false));
	} else {
		out[key] = change["value"];
	}
	return out;
}

bool BeamSolver::fixes_cursor(const Ref<BeamScene> &scene, const Ref<CN> &cursor, const Array &plan, const Dictionary &flags) {
	if (scene.is_null() || cursor.is_null()) {
		return false;
	}
	const std::vector<rc::PlanItem> items = plan_from_items(plan);
	const rc::Flags f = flags_from_dict(flags);
	return std::visit([&](const auto &sc) -> bool {
		using N = typename scene_num<std::decay_t<decltype(sc)>>::type;
		const rc::CN<N> *cc = cn_of<N>(cursor);
		if (sc == nullptr || cc == nullptr) {
			tier_error("BeamSolver.fixes_cursor");
			return false;
		}
		return rc::Solver<N>(*sc).fixes_cursor(*cc, items, f);
	}, scene->scene);
}

Array BeamSolver::targets_of(const Ref<BeamScene> &scene, const Array &events) {
	if (scene.is_null()) {
		return Array();
	}
	return std::visit([&](const auto &sc) -> Array {
		using N = typename scene_num<std::decay_t<decltype(sc)>>::type;
		Array out;
		if (sc == nullptr) {
			return out;
		}
		for (int id : rc::Solver<N>(*sc).targets_of(events_of<N>(events))) {
			out.push_back(id);
		}
		return out;
	}, scene->scene);
}

String BeamSolver::map_kind(const Ref<BeamSceneSurf> &surf, const String &etype) {
	if (surf.is_valid() && etype == String("reflection") && !surf->get_line()) {
		return "inversion";
	}
	return etype;
}

// ---------------------------------------------------------------- FastSolverScene / FastSolverOut

void FastSolverScene::_bind_methods() {
	ClassDB::bind_static_method("FastSolverScene", D_METHOD("from_defs", "defs"), &FastSolverScene::from_defs);
	ClassDB::bind_method(D_METHOD("get_scene"), &FastSolverScene::get_scene);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "scene", godot::PROPERTY_HINT_RESOURCE_TYPE, "BeamScene"), "", "get_scene");
}

Ref<FastSolverScene> FastSolverScene::from_defs(const Array &defs) {
	Ref<FastSolverScene> s;
	s.instantiate();
	s->scene = BeamScene::from_defs(NumCtx::dual(), defs);
	return s;
}

void FastSolverOut::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_events"), &FastSolverOut::get_events);
	ClassDB::bind_method(D_METHOD("get_gain"), &FastSolverOut::get_gain);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "events"), "", "get_events");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "gain"), "", "get_gain");
}

// ---------------------------------------------------------------- FastSolver

namespace {

const rc::Scene<rc::Dual> *dual_scene(const Ref<FastSolverScene> &sc) {
	if (sc.is_null() || sc->scene.is_null()) {
		return nullptr;
	}
	const auto *p = std::get_if<std::shared_ptr<rc::Scene<rc::Dual>>>(&sc->scene->scene);
	return p == nullptr ? nullptr : p->get();
}

Ref<FastSolverOut> out_of(const rc::Walk<rc::Dual> *w) {
	Ref<FastSolverOut> o;
	o.instantiate();
	if (w != nullptr) {
		o->events = events_to_array(w->events);
		for (double g : w->gains) {
			o->gain.push_back(g);
		}
	}
	return o;
}

constexpr int PX = 0, PY = 1, TX = 2, TY = 3, K = 4, DPX = 5, DPY = 6, DTX = 7, DTY = 8, DK = 9;

bool raw_ok(const PackedFloat64Array &b) {
	if (b.size() < 10) {
		rc::report_error("FastSolver: a raw beam has 10 components");
		return false;
	}
	return true;
}

rc::Beam<rc::Dual> beam_of(const PackedFloat64Array &b) {
	return rc::Beam<rc::Dual>{{rc::Dual{b[PX], b[DPX]}, rc::Dual{b[PY], b[DPY]}}, {rc::Dual{b[TX], b[DTX]}, rc::Dual{b[TY], b[DTY]}}, rc::Dual{b[K], b[DK]}};
}

PackedFloat64Array pt4(const rc::CN<rc::Dual> &h) {
	PackedFloat64Array o;
	o.push_back(h.re.v);
	o.push_back(h.im.v);
	o.push_back(h.re.d);
	o.push_back(h.im.d);
	return o;
}

// FastSolver.antipode: p + 2 i tau / kappa with its derivative, that operation order.
PackedFloat64Array antipode_raw(const PackedFloat64Array &b) {
	const double k = b[K];
	const double dk = b[DK];
	PackedFloat64Array o;
	o.push_back(b[PX] + ((-b[TY]) * 2.0) / k);
	o.push_back(b[PY] + (b[TX] * 2.0) / k);
	o.push_back(b[DPX] + (-2.0) * (b[DTY] * k - b[TY] * dk) / (k * k));
	o.push_back(b[DPY] + 2.0 * (b[DTX] * k - b[TX] * dk) / (k * k));
	return o;
}

} // namespace

void FastSolver::_bind_methods() {
	ClassDB::bind_static_method("FastSolver", D_METHOD("trace_direction", "sc", "player", "dir", "plan", "cursor", "mode", "max_events", "flags"),
			&FastSolver::trace_direction, DEFVAL(rc::MAX_HITS), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("FastSolver", D_METHOD("planned", "sc", "player", "cursor", "plan", "max_events", "flags"),
			&FastSolver::planned, DEFVAL(rc::MAX_HITS), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("FastSolver", D_METHOD("physical", "sc", "player", "cursor", "plan", "max_events", "flags"),
			&FastSolver::physical, DEFVAL(rc::MAX_HITS), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("FastSolver", D_METHOD("beam", "px", "py", "tx", "ty", "k"), &FastSolver::beam);
	ClassDB::bind_static_method("FastSolver", D_METHOD("seeded", "b"), &FastSolver::seeded);
	ClassDB::bind_static_method("FastSolver", D_METHOD("at", "b", "t", "dt"), &FastSolver::at);
	ClassDB::bind_static_method("FastSolver", D_METHOD("at_view", "b", "t"), &FastSolver::at_view);
	ClassDB::bind_static_method("FastSolver", D_METHOD("tangent_at", "b", "t", "dt"), &FastSolver::tangent_at);
	ClassDB::bind_static_method("FastSolver", D_METHOD("param_of", "b", "zx", "zy"), &FastSolver::param_of);
	ClassDB::bind_static_method("FastSolver", D_METHOD("circle_hits", "b", "cx", "cy", "r2", "depart_on"), &FastSolver::circle_hits);
	ClassDB::bind_static_method("FastSolver", D_METHOD("line_hits", "b", "sx", "sy", "ex", "ey", "depart_on"), &FastSolver::line_hits);
	ClassDB::bind_static_method("FastSolver", D_METHOD("surf_hits", "b", "s", "depart_on"), &FastSolver::surf_hits);
	ClassDB::bind_static_method("FastSolver", D_METHOD("invert_circle", "b", "t", "dt", "cx", "cy", "r", "r2", "has_h", "hx", "hy"), &FastSolver::invert_circle,
			DEFVAL(false), DEFVAL(0.0), DEFVAL(0.0));
	ClassDB::bind_static_method("FastSolver", D_METHOD("reflect_line", "b", "t", "dt", "sx", "sy", "ex", "ey", "has_h", "hx", "hy"), &FastSolver::reflect_line,
			DEFVAL(false), DEFVAL(0.0), DEFVAL(0.0));
	ClassDB::bind_static_method("FastSolver", D_METHOD("portal", "b", "t", "dt", "s", "has_h", "hx", "hy"), &FastSolver::portal, DEFVAL(false), DEFVAL(0.0), DEFVAL(0.0));
	ClassDB::bind_static_method("FastSolver", D_METHOD("project", "b", "t", "dt", "s", "left", "has_h", "hx", "hy"), &FastSolver::project, DEFVAL(false), DEFVAL(0.0), DEFVAL(0.0));
	ClassDB::bind_static_method("FastSolver", D_METHOD("apply_surf", "b", "t", "dt", "s", "has_h", "hx", "hy"), &FastSolver::apply_surf, DEFVAL(false), DEFVAL(0.0), DEFVAL(0.0));
	ClassDB::bind_static_method("FastSolver", D_METHOD("on_segment", "s", "px", "py"), &FastSolver::on_segment);
	ClassDB::bind_static_method("FastSolver", D_METHOD("side_left", "s", "px", "py", "dx", "dy"), &FastSolver::side_left);
	ClassDB::bind_static_method("FastSolver", D_METHOD("endpoint_at", "s", "px", "py"), &FastSolver::endpoint_at);
	ClassDB::bind_static_method("FastSolver", D_METHOD("blocked_sides", "s", "which", "dx", "dy"), &FastSolver::blocked_sides);
	BIND_CONSTANT(PX);
	BIND_CONSTANT(PY);
	BIND_CONSTANT(TX);
	BIND_CONSTANT(TY);
	BIND_CONSTANT(K);
	BIND_CONSTANT(DPX);
	BIND_CONSTANT(DPY);
	BIND_CONSTANT(DTX);
	BIND_CONSTANT(DTY);
	BIND_CONSTANT(DK);
}

Ref<FastSolverOut> FastSolver::trace_direction(const Ref<FastSolverScene> &sc, const PackedFloat64Array &player, const PackedFloat64Array &dir,
		const Array &plan, const Variant &cursor, int mode, int max_events, const Dictionary &flags) {
	const rc::Scene<rc::Dual> *s = dual_scene(sc);
	const std::optional<rc::CN<rc::Dual>> pl = dual_pair(player);
	const std::optional<rc::CN<rc::Dual>> dd = dual_pair(dir);
	if (s == nullptr || !pl || !dd) {
		rc::report_error("FastSolver.trace_direction: bad scene, player or dir");
		return out_of(nullptr);
	}
	rc::Solver<rc::Dual> solver(*s, sc->cache.get());
	rc::Walk<rc::Dual> w = solver.run_direction(*pl, *dd, plan_from_items(plan), dual_pair(cursor), mode_of(mode), max_events, flags_from_dict(flags));
	return out_of(&w);
}

Ref<FastSolverOut> FastSolver::planned(const Ref<FastSolverScene> &sc, const PackedFloat64Array &player, const PackedFloat64Array &cursor,
		const Array &plan, int max_events, const Dictionary &flags) {
	const rc::Scene<rc::Dual> *s = dual_scene(sc);
	const std::optional<rc::CN<rc::Dual>> pl = dual_pair(player);
	const std::optional<rc::CN<rc::Dual>> cu = dual_pair(cursor);
	if (s == nullptr || !pl || !cu) {
		rc::report_error("FastSolver.planned: bad scene, player or cursor");
		return out_of(nullptr);
	}
	rc::Solver<rc::Dual> solver(*s, sc->cache.get());
	const std::optional<rc::Walk<rc::Dual>> w = solver.run(*pl, *cu, plan_from_items(plan), rc::Mode::Planned, max_events, flags_from_dict(flags));
	return out_of(w ? &*w : nullptr);
}

Ref<FastSolverOut> FastSolver::physical(const Ref<FastSolverScene> &sc, const PackedFloat64Array &player, const PackedFloat64Array &cursor,
		const Array &plan, int max_events, const Dictionary &flags) {
	const rc::Scene<rc::Dual> *s = dual_scene(sc);
	const std::optional<rc::CN<rc::Dual>> pl = dual_pair(player);
	const std::optional<rc::CN<rc::Dual>> cu = dual_pair(cursor);
	if (s == nullptr || !pl || !cu) {
		rc::report_error("FastSolver.physical: bad scene, player or cursor");
		return out_of(nullptr);
	}
	rc::Solver<rc::Dual> solver(*s, sc->cache.get());
	const std::optional<rc::Walk<rc::Dual>> w = solver.run(*pl, *cu, plan_from_items(plan), rc::Mode::Physical, max_events, flags_from_dict(flags));
	return out_of(w ? &*w : nullptr);
}

PackedFloat64Array FastSolver::beam(double px, double py, double tx, double ty, double k) {
	PackedFloat64Array o;
	for (double v : {px, py, tx, ty, k, 0.0, 0.0, 0.0, 0.0, 0.0}) {
		o.push_back(v);
	}
	return o;
}

PackedFloat64Array FastSolver::seeded(const PackedFloat64Array &b) {
	if (!raw_ok(b)) {
		return PackedFloat64Array();
	}
	return beam(b[PX], b[PY], b[TX], b[TY], b[K]).slice(0, 7) + PackedFloat64Array({-b[TY], b[TX], 0.0});
}

PackedFloat64Array FastSolver::at(const PackedFloat64Array &b, double t, double dt) {
	if (!raw_ok(b)) {
		return PackedFloat64Array();
	}
	if (t == rc::INF_D) {
		return antipode_raw(b);
	}
	return pt4(beam_of(b).at(rc::Dual{t, dt}));
}

PackedFloat64Array FastSolver::at_view(const PackedFloat64Array &b, double t) {
	if (!raw_ok(b)) {
		return PackedFloat64Array();
	}
	if (std::isinf(t)) {
		return antipode_raw(b);
	}
	const double u = b[K] * t * 0.5;
	if (std::fabs(u) <= 1.0) {
		return at(b, t, 0.0);
	}
	double x = 0.0;
	double y = 0.0;
	rc::fast::at_view(b[PX], b[PY], b[TX], b[TY], b[K], t, x, y);
	PackedFloat64Array o;
	o.push_back(x);
	o.push_back(y);
	return o;
}

PackedFloat64Array FastSolver::tangent_at(const PackedFloat64Array &b, double t, double dt) {
	if (!raw_ok(b)) {
		return PackedFloat64Array();
	}
	if (t == rc::INF_D) {
		PackedFloat64Array o;
		for (double v : {-b[TX], -b[TY], -b[DTX], -b[DTY]}) {
			o.push_back(v);
		}
		return o;
	}
	return pt4(beam_of(b).tangent_at(rc::Dual{t, dt}));
}

double FastSolver::param_of(const PackedFloat64Array &b, double zx, double zy) {
	if (!raw_ok(b)) {
		return 0.0;
	}
	return beam_of(b).param_of(rc::CN<rc::Dual>{rc::Dual{zx, 0.0}, rc::Dual{zy, 0.0}}).v;
}

// ---------------------------------------------------------------- FastSolver: roots, maps and decisions on raw beams

namespace {

PackedFloat64Array packed_of(const rc::Beam<rc::Dual> &b) {
	PackedFloat64Array o;
	for (double v : {b.p.re.v, b.p.im.v, b.tau.re.v, b.tau.im.v, b.kappa.v, b.p.re.d, b.p.im.d, b.tau.re.d, b.tau.im.d, b.kappa.d}) {
		o.push_back(v);
	}
	return o;
}

PackedFloat64Array roots_of(const std::vector<rc::Dual> &rs) {
	PackedFloat64Array o;
	for (const rc::Dual &r : rs) {
		o.push_back(r.v);
		o.push_back(r.d);
	}
	return o;
}

const rc::Surf<rc::Dual> *dual_surf(const Ref<BeamSceneSurf> &s, const char *where) {
	if (s.is_null()) {
		rc::report_error((std::string(where) + ": null surface").c_str());
		return nullptr;
	}
	const auto *sc = std::get_if<std::shared_ptr<rc::Scene<rc::Dual>>>(&s->scene);
	if (sc == nullptr || *sc == nullptr) {
		rc::report_error((std::string(where) + ": the surface must belong to a float64 (FastSolverScene) scene").c_str());
		return nullptr;
	}
	return (*sc)->find(s->surface_id);
}

// FastSolver's has_h: the hit point's values are given, its derivative is the beam's own at t.
std::optional<rc::CN<rc::Dual>> h_of(const rc::Beam<rc::Dual> &b, rc::Dual t, bool has_h, double hx, double hy) {
	if (!has_h) {
		return std::nullopt;
	}
	rc::CN<rc::Dual> h = b.at(t);
	h.re.v = hx;
	h.im.v = hy;
	return h;
}

} // namespace

PackedFloat64Array FastSolver::circle_hits(const PackedFloat64Array &b, double cx, double cy, double r2, bool depart_on) {
	if (!raw_ok(b)) {
		return PackedFloat64Array();
	}
	const rc::Circle<rc::Dual> circ{0, {rc::Dual{cx, 0.0}, rc::Dual{cy, 0.0}}, rc::Dual{0.0, 0.0}, rc::Dual{r2, 0.0}};
	return roots_of(beam_of(b).hits(circ, depart_on));
}

PackedFloat64Array FastSolver::line_hits(const PackedFloat64Array &b, double sx, double sy, double ex, double ey, bool depart_on) {
	if (!raw_ok(b)) {
		return PackedFloat64Array();
	}
	rc::Seg<rc::Dual> sg;
	sg.kind = rc::SegKind::Line;
	sg.s = {rc::Dual{sx, 0.0}, rc::Dual{sy, 0.0}};
	sg.e = {rc::Dual{ex, 0.0}, rc::Dual{ey, 0.0}};
	return roots_of(rc::geom::hits(beam_of(b), sg, depart_on));
}

PackedFloat64Array FastSolver::surf_hits(const PackedFloat64Array &b, const Ref<BeamSceneSurf> &s, bool depart_on) {
	const rc::Surf<rc::Dual> *surf = dual_surf(s, "FastSolver.surf_hits");
	if (surf == nullptr || !raw_ok(b)) {
		return PackedFloat64Array();
	}
	return roots_of(rc::geom::hits(beam_of(b), surf->seg, depart_on));
}

PackedFloat64Array FastSolver::invert_circle(const PackedFloat64Array &b, double t, double dt, double cx, double cy, double r, double r2,
		bool has_h, double hx, double hy) {
	if (!raw_ok(b)) {
		return PackedFloat64Array();
	}
	const rc::Beam<rc::Dual> bm = beam_of(b);
	const rc::Dual tt{t, dt};
	const rc::Circle<rc::Dual> circ{0, {rc::Dual{cx, 0.0}, rc::Dual{cy, 0.0}}, rc::Dual{r, 0.0}, rc::Dual{r2, 0.0}};
	const auto h = h_of(bm, tt, has_h, hx, hy);
	return packed_of(bm.invert_at(tt, circ, h ? &*h : nullptr));
}

PackedFloat64Array FastSolver::reflect_line(const PackedFloat64Array &b, double t, double dt, double sx, double sy, double ex, double ey,
		bool has_h, double hx, double hy) {
	if (!raw_ok(b)) {
		return PackedFloat64Array();
	}
	const rc::Beam<rc::Dual> bm = beam_of(b);
	const rc::Dual tt{t, dt};
	rc::Seg<rc::Dual> sg;
	sg.kind = rc::SegKind::Line;
	sg.s = {rc::Dual{sx, 0.0}, rc::Dual{sy, 0.0}};
	sg.e = {rc::Dual{ex, 0.0}, rc::Dual{ey, 0.0}};
	const auto h = h_of(bm, tt, has_h, hx, hy);
	return packed_of(rc::effects::reflect_line(bm, tt, sg, h ? &*h : nullptr));
}

PackedFloat64Array FastSolver::portal(const PackedFloat64Array &b, double t, double dt, const Ref<BeamSceneSurf> &s, bool has_h, double hx, double hy) {
	const rc::Surf<rc::Dual> *surf = dual_surf(s, "FastSolver.portal");
	if (surf == nullptr || !raw_ok(b)) {
		return PackedFloat64Array();
	}
	const rc::Beam<rc::Dual> bm = beam_of(b);
	const rc::Dual tt{t, dt};
	const auto h = h_of(bm, tt, has_h, hx, hy);
	return packed_of(rc::effects::portal(bm, tt, *surf, h ? &*h : nullptr));
}

PackedFloat64Array FastSolver::project(const PackedFloat64Array &b, double t, double dt, const Ref<BeamSceneSurf> &s, bool left, bool has_h, double hx,
		double hy) {
	const rc::Surf<rc::Dual> *surf = dual_surf(s, "FastSolver.project");
	if (surf == nullptr || !raw_ok(b)) {
		return PackedFloat64Array();
	}
	const rc::Beam<rc::Dual> bm = beam_of(b);
	const rc::Dual tt{t, dt};
	const auto h = h_of(bm, tt, has_h, hx, hy);
	return packed_of(rc::effects::project(bm, tt, *surf, left, h ? &*h : nullptr));
}

PackedFloat64Array FastSolver::apply_surf(const PackedFloat64Array &b, double t, double dt, const Ref<BeamSceneSurf> &s, bool has_h, double hx, double hy) {
	const rc::Surf<rc::Dual> *surf = dual_surf(s, "FastSolver.apply_surf");
	if (surf == nullptr || !raw_ok(b)) {
		return PackedFloat64Array();
	}
	const rc::Beam<rc::Dual> bm = beam_of(b);
	const rc::Dual tt{t, dt};
	const auto h = h_of(bm, tt, has_h, hx, hy);
	return packed_of(rc::effects::apply(bm, tt, *surf, h ? &*h : nullptr));
}

bool FastSolver::on_segment(const Ref<BeamSceneSurf> &s, double px, double py) {
	const rc::Surf<rc::Dual> *surf = dual_surf(s, "FastSolver.on_segment");
	return surf != nullptr && rc::geom::on_segment(surf->seg, rc::CN<rc::Dual>{rc::Dual{px, 0.0}, rc::Dual{py, 0.0}});
}

bool FastSolver::side_left(const Ref<BeamSceneSurf> &s, double px, double py, double dx, double dy) {
	const rc::Surf<rc::Dual> *surf = dual_surf(s, "FastSolver.side_left");
	return surf != nullptr &&
		rc::geom::side_left(surf->seg, rc::CN<rc::Dual>{rc::Dual{px, 0.0}, rc::Dual{py, 0.0}}, rc::CN<rc::Dual>{rc::Dual{dx, 0.0}, rc::Dual{dy, 0.0}});
}

int FastSolver::endpoint_at(const Ref<BeamSceneSurf> &s, double px, double py) {
	const rc::Surf<rc::Dual> *surf = dual_surf(s, "FastSolver.endpoint_at");
	return surf == nullptr ? 0 : rc::geom::endpoint_at(surf->seg, rc::CN<rc::Dual>{rc::Dual{px, 0.0}, rc::Dual{py, 0.0}});
}

Array FastSolver::blocked_sides(const Ref<BeamSceneSurf> &s, int which, double dx, double dy) {
	const rc::Surf<rc::Dual> *surf = dual_surf(s, "FastSolver.blocked_sides");
	Array a;
	if (surf == nullptr) {
		return a;
	}
	const rc::geom::Blocked bl = rc::geom::blocked_sides(surf->seg, which, rc::CN<rc::Dual>{rc::Dual{dx, 0.0}, rc::Dual{dy, 0.0}});
	a.push_back(bl.left);
	a.push_back(bl.right);
	return a;
}

// ---------------------------------------------------------------- BeamSolverAnchorChain

void BeamSolverAnchorChain::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_beams"), &BeamSolverAnchorChain::get_beams);
	ClassDB::bind_method(D_METHOD("get_hits"), &BeamSolverAnchorChain::get_hits);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "beams"), "", "get_beams");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "hits"), "", "get_hits");
}

Ref<BeamSolverAnchorChain> BeamSolver::anchor_chain(const Ref<BeamScene> &scene, const Ref<CN> &player, const Ref<CN> &cursor, const Array &plan,
		const Dictionary &flags) {
	if (scene.is_null() || player.is_null() || cursor.is_null()) {
		rc::report_error("BeamSolver.anchor_chain: null argument");
		return Ref<BeamSolverAnchorChain>();
	}
	const std::vector<rc::PlanItem> items = plan_from_items(plan);
	const rc::Flags f = flags_from_dict(flags);
	return std::visit([&](const auto &sc) -> Ref<BeamSolverAnchorChain> {
		using N = typename scene_num<std::decay_t<decltype(sc)>>::type;
		const rc::CN<N> *pl = cn_of<N>(player);
		const rc::CN<N> *cu = cn_of<N>(cursor);
		if (sc == nullptr || pl == nullptr || cu == nullptr) {
			tier_error("BeamSolver.anchor_chain");
			return Ref<BeamSolverAnchorChain>();
		}
		const std::shared_ptr<const rc::AnchorChain<N>> chain = rc::Solver<N>(*sc).anchor_chain(*pl, *cu, items, f);
		if (!chain) {
			return Ref<BeamSolverAnchorChain>();
		}
		Ref<BeamSolverAnchorChain> o;
		o.instantiate();
		for (const rc::BeamPtr<N> &b : chain->beams) {
			o->beams.push_back(HPBeam::wrap(BeamV(b)));
		}
		for (const auto &kv : chain->hits) {
			o->hits[kv.first] = CN::wrap(CNV(kv.second));
		}
		return o;
	}, scene->scene);
}
