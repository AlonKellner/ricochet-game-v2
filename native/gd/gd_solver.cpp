#include "gd_solver.hpp"

#include "rc/error.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdio>

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
const rc::CN<N> *cn_of(const Ref<NCN> &c) {
	return c.is_null() ? nullptr : std::get_if<rc::CN<N>>(&c->value);
}
template <class N>
const N *num_ref(const Ref<NNum> &n) {
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
		Ref<NBeamSolverEvent> e = events[i];
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
	for (const rc::Event<N> &e : ev) {
		a.push_back(NBeamSolverEvent::wrap(EventV(e)));
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

// ---------------------------------------------------------------- NHPBeam

void NHPBeam::_bind_methods() {
	ClassDB::bind_static_method("NHPBeam", D_METHOD("make", "p", "tau", "kappa"), &NHPBeam::make);
	ClassDB::bind_method(D_METHOD("get_p"), &NHPBeam::get_p);
	ClassDB::bind_method(D_METHOD("get_tau"), &NHPBeam::get_tau);
	ClassDB::bind_method(D_METHOD("get_kappa"), &NHPBeam::get_kappa);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "p", godot::PROPERTY_HINT_RESOURCE_TYPE, "NCN"), "", "get_p");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "tau", godot::PROPERTY_HINT_RESOURCE_TYPE, "NCN"), "", "get_tau");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "kappa", godot::PROPERTY_HINT_RESOURCE_TYPE, "NNum"), "", "get_kappa");
	ClassDB::bind_method(D_METHOD("at", "t"), &NHPBeam::at);
	ClassDB::bind_method(D_METHOD("at_infinity"), &NHPBeam::at_infinity);
	ClassDB::bind_method(D_METHOD("tangent_at", "t"), &NHPBeam::tangent_at);
	ClassDB::bind_method(D_METHOD("param_of", "z"), &NHPBeam::param_of);
}

Ref<NHPBeam> NHPBeam::wrap(BeamV b) {
	Ref<NHPBeam> o;
	o.instantiate();
	o->beam = std::move(b);
	return o;
}

Ref<NHPBeam> NHPBeam::make(const Ref<NCN> &p, const Ref<NCN> &tau, const Ref<NNum> &kappa) {
	if (p.is_null() || tau.is_null() || kappa.is_null()) {
		rc::report_error("NHPBeam.make: null argument");
		return Ref<NHPBeam>();
	}
	return std::visit([&](const auto &pp) -> Ref<NHPBeam> {
		using N = std::decay_t<decltype(pp.re)>;
		const rc::CN<N> *tt = cn_of<N>(tau);
		const N *kk = num_ref<N>(kappa);
		if (tt == nullptr || kk == nullptr) {
			tier_error("NHPBeam.make");
			return Ref<NHPBeam>();
		}
		return wrap(std::make_shared<const rc::Beam<N>>(rc::Beam<N>{pp, *tt, *kk}));
	}, p->value);
}

Ref<NCN> NHPBeam::get_p() const {
	if (null_beam(beam)) {
		return Ref<NCN>();
	}
	return std::visit([](const auto &b) { return NCN::wrap(b->p); }, beam);
}
Ref<NCN> NHPBeam::get_tau() const {
	if (null_beam(beam)) {
		return Ref<NCN>();
	}
	return std::visit([](const auto &b) { return NCN::wrap(b->tau); }, beam);
}
Ref<NNum> NHPBeam::get_kappa() const {
	if (null_beam(beam)) {
		return Ref<NNum>();
	}
	return std::visit([](const auto &b) { return NNum::wrap(b->kappa); }, beam);
}
Ref<NCN> NHPBeam::at(const Ref<NNum> &t) const {
	if (null_beam(beam) || t.is_null()) {
		return Ref<NCN>();
	}
	return std::visit([&](const auto &b) -> Ref<NCN> {
		using N = std::decay_t<decltype(b->kappa)>;
		const N *tt = num_ref<N>(t);
		if (tt == nullptr) {
			tier_error("NHPBeam.at");
			return Ref<NCN>();
		}
		if (tt->is_inf() && b->kappa.is_zero()) {
			return Ref<NCN>(); // a line has no point at infinity
		}
		return NCN::wrap(b->at(*tt));
	}, beam);
}
Variant NHPBeam::at_infinity() const {
	if (null_beam(beam)) {
		return Variant();
	}
	return std::visit([](const auto &b) -> Variant {
		const auto z = b->at_infinity();
		return z ? Variant(NCN::wrap(*z)) : Variant();
	}, beam);
}
Ref<NCN> NHPBeam::tangent_at(const Ref<NNum> &t) const {
	if (null_beam(beam) || t.is_null()) {
		return Ref<NCN>();
	}
	return std::visit([&](const auto &b) -> Ref<NCN> {
		using N = std::decay_t<decltype(b->kappa)>;
		const N *tt = num_ref<N>(t);
		if (tt == nullptr) {
			tier_error("NHPBeam.tangent_at");
			return Ref<NCN>();
		}
		return NCN::wrap(b->tangent_at(*tt));
	}, beam);
}
Ref<NNum> NHPBeam::param_of(const Ref<NCN> &z) const {
	if (null_beam(beam) || z.is_null()) {
		return Ref<NNum>();
	}
	return std::visit([&](const auto &b) -> Ref<NNum> {
		using N = std::decay_t<decltype(b->kappa)>;
		const rc::CN<N> *zz = cn_of<N>(z);
		if (zz == nullptr) {
			tier_error("NHPBeam.param_of");
			return Ref<NNum>();
		}
		return NNum::wrap(b->param_of(*zz));
	}, beam);
}

// ---------------------------------------------------------------- NBeamSolverPlanItem

void NBeamSolverPlanItem::_bind_methods() {
	ClassDB::bind_static_method("NBeamSolverPlanItem", D_METHOD("make", "sid", "left", "hint"), &NBeamSolverPlanItem::make, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("get_surface_id"), &NBeamSolverPlanItem::get_surface_id);
	ClassDB::bind_method(D_METHOD("set_surface_id", "v"), &NBeamSolverPlanItem::set_surface_id);
	ClassDB::bind_method(D_METHOD("get_left"), &NBeamSolverPlanItem::get_left);
	ClassDB::bind_method(D_METHOD("set_left", "v"), &NBeamSolverPlanItem::set_left);
	ClassDB::bind_method(D_METHOD("get_hint"), &NBeamSolverPlanItem::get_hint);
	ClassDB::bind_method(D_METHOD("set_hint", "v"), &NBeamSolverPlanItem::set_hint);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "surface_id"), "set_surface_id", "get_surface_id");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "left"), "set_left", "get_left");
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "hint", godot::PROPERTY_HINT_NONE, "", godot::PROPERTY_USAGE_DEFAULT | godot::PROPERTY_USAGE_NIL_IS_VARIANT), "set_hint", "get_hint");
}

Ref<NBeamSolverPlanItem> NBeamSolverPlanItem::make(int sid, bool p_left, const Variant &p_hint) {
	Ref<NBeamSolverPlanItem> o;
	o.instantiate();
	o->surface_id = sid;
	o->left = p_left;
	o->hint = p_hint;
	return o;
}

// ---------------------------------------------------------------- NBeamSolverEvent

#define RC_EV_GETTER(name, type, expr)                            \
	type NBeamSolverEvent::get_##name() const {                   \
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
RC_EV_GETTER(anchored, bool, e.anchored)
RC_EV_GETTER(beam_view, int, e.beam_view)
#undef RC_EV_GETTER

Ref<NBeamSolverEvent> NBeamSolverEvent::wrap(EventV e) {
	Ref<NBeamSolverEvent> o;
	o.instantiate();
	o->event = std::move(e);
	return o;
}
String NBeamSolverEvent::get_kind() const {
	return visit([](const auto &e) { return String(rc::event_kind_name(e.kind)); });
}
Variant NBeamSolverEvent::get_point() const {
	return visit([](const auto &e) -> Variant { return e.point ? Variant(NCN::wrap(*e.point)) : Variant(); });
}
Ref<NHPBeam> NBeamSolverEvent::get_beam() const {
	return visit([](const auto &e) -> Ref<NHPBeam> { return e.beam ? NHPBeam::wrap(BeamV(e.beam)) : Ref<NHPBeam>(); });
}
Variant NBeamSolverEvent::get_t_from() const {
	return visit([](const auto &e) -> Variant { return e.t_from ? Variant(NNum::wrap(NumV(*e.t_from))) : Variant(); });
}
Variant NBeamSolverEvent::get_t_to() const {
	return visit([](const auto &e) -> Variant { return e.t_to ? Variant(NNum::wrap(NumV(*e.t_to))) : Variant(); });
}
Dictionary NBeamSolverEvent::get_flags() const {
	return visit([](const auto &e) { return flags_to_dict(e.flags); });
}
String NBeamSolverEvent::describe() const {
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

void NBeamSolverEvent::_bind_methods() {
#define RC_EV_PROP(name, vtype)                                                                 \
	ClassDB::bind_method(D_METHOD("get_" #name), &NBeamSolverEvent::get_##name);                \
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
	RC_EV_PROP(flags, DICTIONARY)
	RC_EV_PROP(anchored, BOOL)
	RC_EV_PROP(beam_view, INT)
#undef RC_EV_PROP
#define RC_EV_NULLABLE(name)                                                                                           \
	ClassDB::bind_method(D_METHOD("get_" #name), &NBeamSolverEvent::get_##name);                                       \
	ADD_PROPERTY(PropertyInfo(Variant::NIL, #name, godot::PROPERTY_HINT_NONE, "",                                      \
			godot::PROPERTY_USAGE_DEFAULT | godot::PROPERTY_USAGE_NIL_IS_VARIANT), "", "get_" #name);
	RC_EV_NULLABLE(point)
	RC_EV_NULLABLE(t_from)
	RC_EV_NULLABLE(t_to)
#undef RC_EV_NULLABLE
	ClassDB::bind_method(D_METHOD("get_beam"), &NBeamSolverEvent::get_beam);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "beam", godot::PROPERTY_HINT_RESOURCE_TYPE, "NHPBeam"), "", "get_beam");
	ClassDB::bind_method(D_METHOD("describe"), &NBeamSolverEvent::describe);
}

// ---------------------------------------------------------------- NBeamSolver

void NBeamSolver::_bind_methods() {
	ClassDB::bind_static_method("NBeamSolver", D_METHOD("trace_direction", "ctx", "scene", "player", "dir", "plan", "cursor", "mode", "max_events", "flags"),
			&NBeamSolver::trace_direction, DEFVAL(MAX_HITS), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("NBeamSolver", D_METHOD("planned", "ctx", "scene", "player", "cursor", "plan", "max_events", "flags"),
			&NBeamSolver::planned, DEFVAL(MAX_HITS), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("NBeamSolver", D_METHOD("physical", "ctx", "scene", "player", "cursor", "plan", "max_events", "flags"),
			&NBeamSolver::physical, DEFVAL(MAX_HITS), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("NBeamSolver", D_METHOD("plan_effects", "scene", "plan", "flags"), &NBeamSolver::plan_effects, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("NBeamSolver", D_METHOD("plan_flags", "scene", "plan", "flags"), &NBeamSolver::plan_flags, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("NBeamSolver", D_METHOD("apply_state", "flags", "change"), &NBeamSolver::apply_state);
	ClassDB::bind_static_method("NBeamSolver", D_METHOD("fixes_cursor", "scene", "cursor", "plan", "flags"), &NBeamSolver::fixes_cursor, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("NBeamSolver", D_METHOD("targets_of", "scene", "events"), &NBeamSolver::targets_of);
	ClassDB::bind_static_method("NBeamSolver", D_METHOD("map_kind", "surf", "etype"), &NBeamSolver::map_kind);
	BIND_ENUM_CONSTANT(PHYSICAL);
	BIND_ENUM_CONSTANT(PLANNED);
	BIND_CONSTANT(MAX_HITS);
}

namespace {

// Runs `f(scene, player, cursor)` at the scene's tier; the ctx (when given) must agree.
template <class F>
Array with_solver(const char *where, const Ref<NNumCtx> &ctx, const Ref<NBeamScene> &scene, const Ref<NCN> &player,
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
			const Ref<NCN> c = cursor;
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

Array NBeamSolver::trace_direction(const Ref<NNumCtx> &ctx, const Ref<NBeamScene> &scene, const Ref<NCN> &player, const Ref<NCN> &dir,
		const Array &plan, const Variant &cursor, int mode, int max_events, const Dictionary &flags) {
	const std::vector<rc::PlanItem> items = plan_from_items(plan);
	const rc::Flags f = flags_from_dict(flags);
	return with_solver("NBeamSolver.trace_direction", ctx, scene, player, cursor, [&](auto &solver, const auto &pl, const auto &cur) -> Array {
		using N = std::decay_t<decltype(pl.re)>;
		const rc::CN<N> *dd = cn_of<N>(dir);
		if (dd == nullptr) {
			tier_error("NBeamSolver.trace_direction");
			return Array();
		}
		rc::Walk<N> w = solver.run_direction(pl, *dd, items, cur, mode_of(mode), max_events, f);
		return events_to_array(w.events);
	});
}

Array NBeamSolver::planned(const Ref<NNumCtx> &ctx, const Ref<NBeamScene> &scene, const Ref<NCN> &player, const Ref<NCN> &cursor,
		const Array &plan, int max_events, const Dictionary &flags) {
	const std::vector<rc::PlanItem> items = plan_from_items(plan);
	const rc::Flags f = flags_from_dict(flags);
	return with_solver("NBeamSolver.planned", ctx, scene, player, Variant(cursor), [&](auto &solver, const auto &pl, const auto &cur) -> Array {
		if (!cur) {
			rc::report_error("NBeamSolver.planned: null cursor");
			return Array();
		}
		return events_to_array(solver.planned(pl, *cur, items, max_events, f));
	});
}

Array NBeamSolver::physical(const Ref<NNumCtx> &ctx, const Ref<NBeamScene> &scene, const Ref<NCN> &player, const Ref<NCN> &cursor,
		const Array &plan, int max_events, const Dictionary &flags) {
	const std::vector<rc::PlanItem> items = plan_from_items(plan);
	const rc::Flags f = flags_from_dict(flags);
	return with_solver("NBeamSolver.physical", ctx, scene, player, Variant(cursor), [&](auto &solver, const auto &pl, const auto &cur) -> Array {
		if (!cur) {
			rc::report_error("NBeamSolver.physical: null cursor");
			return Array();
		}
		return events_to_array(solver.physical(pl, *cur, items, max_events, f));
	});
}

Array NBeamSolver::plan_effects(const Ref<NBeamScene> &scene, const Array &plan, const Dictionary &flags) {
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

Dictionary NBeamSolver::plan_flags(const Ref<NBeamScene> &scene, const Array &plan, const Dictionary &flags) {
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

Dictionary NBeamSolver::apply_state(const Dictionary &flags, const Dictionary &change) {
	Dictionary out = flags.duplicate();
	const Variant key = change["key"];
	if (String(change["op"]) == String("toggle")) {
		out[key] = !bool(flags.get(key, false));
	} else {
		out[key] = change["value"];
	}
	return out;
}

bool NBeamSolver::fixes_cursor(const Ref<NBeamScene> &scene, const Ref<NCN> &cursor, const Array &plan, const Dictionary &flags) {
	if (scene.is_null() || cursor.is_null()) {
		return false;
	}
	const std::vector<rc::PlanItem> items = plan_from_items(plan);
	const rc::Flags f = flags_from_dict(flags);
	return std::visit([&](const auto &sc) -> bool {
		using N = typename scene_num<std::decay_t<decltype(sc)>>::type;
		const rc::CN<N> *cc = cn_of<N>(cursor);
		if (sc == nullptr || cc == nullptr) {
			tier_error("NBeamSolver.fixes_cursor");
			return false;
		}
		return rc::Solver<N>(*sc).fixes_cursor(*cc, items, f);
	}, scene->scene);
}

Array NBeamSolver::targets_of(const Ref<NBeamScene> &scene, const Array &events) {
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

String NBeamSolver::map_kind(const Ref<NBeamSceneSurf> &surf, const String &etype) {
	if (surf.is_valid() && etype == String("reflection") && !surf->get_line()) {
		return "inversion";
	}
	return etype;
}

// ---------------------------------------------------------------- NFastSolverScene / NFastSolverOut

void NFastSolverScene::_bind_methods() {
	ClassDB::bind_static_method("NFastSolverScene", D_METHOD("from_defs", "defs"), &NFastSolverScene::from_defs);
	ClassDB::bind_method(D_METHOD("get_scene"), &NFastSolverScene::get_scene);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "scene", godot::PROPERTY_HINT_RESOURCE_TYPE, "NBeamScene"), "", "get_scene");
}

Ref<NFastSolverScene> NFastSolverScene::from_defs(const Array &defs) {
	Ref<NFastSolverScene> s;
	s.instantiate();
	s->scene = NBeamScene::from_defs(NNumCtx::dual(), defs);
	return s;
}

void NFastSolverOut::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_events"), &NFastSolverOut::get_events);
	ClassDB::bind_method(D_METHOD("get_gain"), &NFastSolverOut::get_gain);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "events"), "", "get_events");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "gain"), "", "get_gain");
}

// ---------------------------------------------------------------- NFastSolver

namespace {

const rc::Scene<rc::Dual> *dual_scene(const Ref<NFastSolverScene> &sc) {
	if (sc.is_null() || sc->scene.is_null()) {
		return nullptr;
	}
	const auto *p = std::get_if<std::shared_ptr<rc::Scene<rc::Dual>>>(&sc->scene->scene);
	return p == nullptr ? nullptr : p->get();
}

Ref<NFastSolverOut> out_of(const rc::Walk<rc::Dual> *w) {
	Ref<NFastSolverOut> o;
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
		rc::report_error("NFastSolver: a raw beam has 10 components");
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

void NFastSolver::_bind_methods() {
	ClassDB::bind_static_method("NFastSolver", D_METHOD("trace_direction", "sc", "player", "dir", "plan", "cursor", "mode", "max_events", "flags"),
			&NFastSolver::trace_direction, DEFVAL(rc::MAX_HITS), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("NFastSolver", D_METHOD("planned", "sc", "player", "cursor", "plan", "max_events", "flags"),
			&NFastSolver::planned, DEFVAL(rc::MAX_HITS), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("NFastSolver", D_METHOD("physical", "sc", "player", "cursor", "plan", "max_events", "flags"),
			&NFastSolver::physical, DEFVAL(rc::MAX_HITS), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("NFastSolver", D_METHOD("beam", "px", "py", "tx", "ty", "k"), &NFastSolver::beam);
	ClassDB::bind_static_method("NFastSolver", D_METHOD("seeded", "b"), &NFastSolver::seeded);
	ClassDB::bind_static_method("NFastSolver", D_METHOD("at", "b", "t", "dt"), &NFastSolver::at);
	ClassDB::bind_static_method("NFastSolver", D_METHOD("at_view", "b", "t"), &NFastSolver::at_view);
	ClassDB::bind_static_method("NFastSolver", D_METHOD("tangent_at", "b", "t", "dt"), &NFastSolver::tangent_at);
	ClassDB::bind_static_method("NFastSolver", D_METHOD("param_of", "b", "zx", "zy"), &NFastSolver::param_of);
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

Ref<NFastSolverOut> NFastSolver::trace_direction(const Ref<NFastSolverScene> &sc, const PackedFloat64Array &player, const PackedFloat64Array &dir,
		const Array &plan, const Variant &cursor, int mode, int max_events, const Dictionary &flags) {
	const rc::Scene<rc::Dual> *s = dual_scene(sc);
	const std::optional<rc::CN<rc::Dual>> pl = dual_pair(player);
	const std::optional<rc::CN<rc::Dual>> dd = dual_pair(dir);
	if (s == nullptr || !pl || !dd) {
		rc::report_error("NFastSolver.trace_direction: bad scene, player or dir");
		return out_of(nullptr);
	}
	rc::Solver<rc::Dual> solver(*s, sc->cache.get());
	rc::Walk<rc::Dual> w = solver.run_direction(*pl, *dd, plan_from_items(plan), dual_pair(cursor), mode_of(mode), max_events, flags_from_dict(flags));
	return out_of(&w);
}

Ref<NFastSolverOut> NFastSolver::planned(const Ref<NFastSolverScene> &sc, const PackedFloat64Array &player, const PackedFloat64Array &cursor,
		const Array &plan, int max_events, const Dictionary &flags) {
	const rc::Scene<rc::Dual> *s = dual_scene(sc);
	const std::optional<rc::CN<rc::Dual>> pl = dual_pair(player);
	const std::optional<rc::CN<rc::Dual>> cu = dual_pair(cursor);
	if (s == nullptr || !pl || !cu) {
		rc::report_error("NFastSolver.planned: bad scene, player or cursor");
		return out_of(nullptr);
	}
	rc::Solver<rc::Dual> solver(*s, sc->cache.get());
	const std::optional<rc::Walk<rc::Dual>> w = solver.run(*pl, *cu, plan_from_items(plan), rc::Mode::Planned, max_events, flags_from_dict(flags));
	return out_of(w ? &*w : nullptr);
}

Ref<NFastSolverOut> NFastSolver::physical(const Ref<NFastSolverScene> &sc, const PackedFloat64Array &player, const PackedFloat64Array &cursor,
		const Array &plan, int max_events, const Dictionary &flags) {
	const rc::Scene<rc::Dual> *s = dual_scene(sc);
	const std::optional<rc::CN<rc::Dual>> pl = dual_pair(player);
	const std::optional<rc::CN<rc::Dual>> cu = dual_pair(cursor);
	if (s == nullptr || !pl || !cu) {
		rc::report_error("NFastSolver.physical: bad scene, player or cursor");
		return out_of(nullptr);
	}
	rc::Solver<rc::Dual> solver(*s, sc->cache.get());
	const std::optional<rc::Walk<rc::Dual>> w = solver.run(*pl, *cu, plan_from_items(plan), rc::Mode::Physical, max_events, flags_from_dict(flags));
	return out_of(w ? &*w : nullptr);
}

PackedFloat64Array NFastSolver::beam(double px, double py, double tx, double ty, double k) {
	PackedFloat64Array o;
	for (double v : {px, py, tx, ty, k, 0.0, 0.0, 0.0, 0.0, 0.0}) {
		o.push_back(v);
	}
	return o;
}

PackedFloat64Array NFastSolver::seeded(const PackedFloat64Array &b) {
	if (!raw_ok(b)) {
		return PackedFloat64Array();
	}
	return beam(b[PX], b[PY], b[TX], b[TY], b[K]).slice(0, 7) + PackedFloat64Array({-b[TY], b[TX], 0.0});
}

PackedFloat64Array NFastSolver::at(const PackedFloat64Array &b, double t, double dt) {
	if (!raw_ok(b)) {
		return PackedFloat64Array();
	}
	if (t == rc::INF_D) {
		return antipode_raw(b);
	}
	return pt4(beam_of(b).at(rc::Dual{t, dt}));
}

PackedFloat64Array NFastSolver::at_view(const PackedFloat64Array &b, double t) {
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

PackedFloat64Array NFastSolver::tangent_at(const PackedFloat64Array &b, double t, double dt) {
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

double NFastSolver::param_of(const PackedFloat64Array &b, double zx, double zy) {
	if (!raw_ok(b)) {
		return 0.0;
	}
	return beam_of(b).param_of(rc::CN<rc::Dual>{rc::Dual{zx, 0.0}, rc::Dual{zy, 0.0}}).v;
}
