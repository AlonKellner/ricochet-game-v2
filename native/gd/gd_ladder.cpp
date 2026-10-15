#include "gd_ladder.hpp"

#include "rc/error.hpp"

#include <godot_cpp/core/class_db.hpp>

using godot::Array;
using godot::ClassDB;
using godot::D_METHOD;
using godot::Dictionary;
using godot::PropertyInfo;
using godot::Ref;
using godot::Variant;
using namespace rcgd;

Array doubles_to_array(const std::vector<double> &v) {
	Array a;
	for (double x : v) {
		a.push_back(x);
	}
	return a;
}

Array climb_events(const rc::Climb &c) {
	return c.limbs == 0 ? events_to_array(c.f64_events) : events_to_array(c.big_events);
}

namespace {

bool pair_of(const Array &a, double &x, double &y, const char *where) {
	if (a.size() < 2) {
		rc::report_error((std::string(where) + ": a point is [x, y]").c_str());
		return false;
	}
	x = double(a[0]);
	y = double(a[1]);
	return true;
}

// The Dual scene of `defs` (built), or the one `fsc` (FastSolverScene) carries.
struct DualScene {
	std::shared_ptr<rc::Scene<rc::Dual>> own;
	const rc::Scene<rc::Dual> *scene = nullptr;
	rc::SolveCache<rc::Dual> *cache = nullptr;
};

DualScene dual_scene_of(const Array &defs, const Variant &fsc) {
	DualScene d;
	if (fsc.get_type() == Variant::OBJECT) {
		const Ref<FastSolverScene> s = fsc;
		if (s.is_valid() && s->scene.is_valid()) {
			if (const auto *p = std::get_if<std::shared_ptr<rc::Scene<rc::Dual>>>(&s->scene->scene)) {
				d.scene = p->get();
				d.cache = s->cache.get();
				return d;
			}
		}
	}
	d.own = std::make_shared<rc::Scene<rc::Dual>>(rc::Scene<rc::Dual>::from_defs(defs_from_array(defs), rc::Dual{1.0, 0.0}));
	d.scene = d.own.get();
	return d;
}

Ref<ErrorEstimateSensitivity> sensitivity_of(const Array &defs, const Array &player, const Array &cursor, const Array &plan, int max_events,
		const Variant &fsc, const Dictionary &flags, rc::Mode mode, const char *where) {
	double px, py, cx, cy;
	if (!pair_of(player, px, py, where) || !pair_of(cursor, cx, cy, where)) {
		return ErrorEstimateSensitivity::wrap(rc::estimate::Sensitivity{}, Array());
	}
	const DualScene d = dual_scene_of(defs, fsc);
	rc::Solver<rc::Dual> solver(*d.scene, d.cache);
	const std::optional<rc::Walk<rc::Dual>> w = solver.run(rc::CN<rc::Dual>{rc::Dual{px, 0.0}, rc::Dual{py, 0.0}},
			rc::CN<rc::Dual>{rc::Dual{cx, 0.0}, rc::Dual{cy, 0.0}}, plan_from_items(plan), mode, max_events, flags_from_dict(flags));
	if (!w) {
		// FastSolver._out(null): no events, no gains -> an empty sensitivity
		return ErrorEstimateSensitivity::wrap(rc::estimate::collect(std::vector<rc::Event<rc::Dual>>{}, {}), Array());
	}
	return ErrorEstimateSensitivity::wrap(rc::estimate::collect(w->events, w->gains), events_to_array(w->events));
}

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

// Calls f(std::vector<Event<N>>) for the tier of the first event (F64 when empty).
template <class F>
auto with_events(const Array &events, F &&f) {
	if (events.size() > 0) {
		Ref<BeamSolverEvent> e = events[0];
		if (e.is_valid()) {
			if (std::holds_alternative<rc::Event<rc::Dual>>(e->event)) {
				return f(events_of<rc::Dual>(events));
			}
			if (std::holds_alternative<rc::Event<rc::BigFloat>>(e->event)) {
				return f(events_of<rc::BigFloat>(events));
			}
		}
	}
	return f(events_of<rc::F64>(events));
}

} // namespace

// ---------------------------------------------------------------- ErrorEstimateSensitivity

void ErrorEstimateSensitivity::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_gain"), &ErrorEstimateSensitivity::get_gain);
	ClassDB::bind_method(D_METHOD("get_margin"), &ErrorEstimateSensitivity::get_margin);
	ClassDB::bind_method(D_METHOD("get_max_gain"), &ErrorEstimateSensitivity::get_max_gain);
	ClassDB::bind_method(D_METHOD("get_unreliable_from"), &ErrorEstimateSensitivity::get_unreliable_from);
	ClassDB::bind_method(D_METHOD("get_events"), &ErrorEstimateSensitivity::get_events);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "gain"), "", "get_gain");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "margin"), "", "get_margin");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_gain"), "", "get_max_gain");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "unreliable_from"), "", "get_unreliable_from");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "events"), "", "get_events");
}

Ref<ErrorEstimateSensitivity> ErrorEstimateSensitivity::wrap(rc::estimate::Sensitivity s, Array ev) {
	Ref<ErrorEstimateSensitivity> o;
	o.instantiate();
	o->sens = std::move(s);
	o->events = std::move(ev);
	return o;
}
Array ErrorEstimateSensitivity::get_gain() const { return doubles_to_array(sens.gain); }
Array ErrorEstimateSensitivity::get_margin() const { return doubles_to_array(sens.margin); }

// ---------------------------------------------------------------- ErrorEstimate

void ErrorEstimate::_bind_methods() {
	ClassDB::bind_static_method("ErrorEstimate", D_METHOD("C"), &ErrorEstimate::C);
	ClassDB::bind_static_method("ErrorEstimate", D_METHOD("SCALE"), &ErrorEstimate::SCALE);
	ClassDB::bind_static_method("ErrorEstimate", D_METHOD("MARGIN_SAFETY"), &ErrorEstimate::MARGIN_SAFETY);
	ClassDB::bind_static_method("ErrorEstimate", D_METHOD("physical_sensitivity", "defs", "player", "cursor", "plan", "max_events", "fsc", "flags"),
			&ErrorEstimate::physical_sensitivity, DEFVAL(Variant()), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("ErrorEstimate", D_METHOD("planned_sensitivity", "defs", "player", "cursor", "plan", "max_events", "fsc", "flags"),
			&ErrorEstimate::planned_sensitivity, DEFVAL(Variant()), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("ErrorEstimate", D_METHOD("replay", "events", "defs", "fsc"), &ErrorEstimate::replay, DEFVAL(Variant()));
	ClassDB::bind_static_method("ErrorEstimate", D_METHOD("unreliable_index", "sens", "ctx_eps"), &ErrorEstimate::unreliable_index);
	ClassDB::bind_static_method("ErrorEstimate", D_METHOD("estimate", "sens", "ctx_eps"), &ErrorEstimate::estimate);
	ClassDB::bind_static_method("ErrorEstimate", D_METHOD("required_limbs", "sens", "tol", "tiers"), &ErrorEstimate::required_limbs);
}

Ref<ErrorEstimateSensitivity> ErrorEstimate::physical_sensitivity(const Array &defs, const Array &player, const Array &cursor, const Array &plan,
		int max_events, const Variant &fsc, const Dictionary &flags) {
	return sensitivity_of(defs, player, cursor, plan, max_events, fsc, flags, rc::Mode::Physical, "ErrorEstimate.physical_sensitivity");
}

Ref<ErrorEstimateSensitivity> ErrorEstimate::planned_sensitivity(const Array &defs, const Array &player, const Array &cursor, const Array &plan,
		int max_events, const Variant &fsc, const Dictionary &flags) {
	return sensitivity_of(defs, player, cursor, plan, max_events, fsc, flags, rc::Mode::Planned, "ErrorEstimate.planned_sensitivity");
}

Ref<ErrorEstimateSensitivity> ErrorEstimate::replay(const Array &events, const Array &defs, const Variant &fsc) {
	const DualScene d = dual_scene_of(defs, fsc);
	return with_events(events, [&](const auto &ev) {
		return ErrorEstimateSensitivity::wrap(rc::estimate::replay(ev, *d.scene), events);
	});
}

int ErrorEstimate::unreliable_index(const Ref<ErrorEstimateSensitivity> &sens, double ctx_eps) {
	return sens.is_null() ? -1 : rc::estimate::unreliable_index(sens->sens, ctx_eps);
}

Array ErrorEstimate::estimate(const Ref<ErrorEstimateSensitivity> &sens, double ctx_eps) {
	return sens.is_null() ? Array() : doubles_to_array(rc::estimate::estimate(sens->sens, ctx_eps));
}

int ErrorEstimate::required_limbs(const Ref<ErrorEstimateSensitivity> &sens, double tol, const Array &tiers) {
	std::vector<int> t;
	for (int64_t i = 0; i < tiers.size(); i++) {
		t.push_back(int(int64_t(tiers[i])));
	}
	if (sens.is_null() || t.empty()) {
		return 0;
	}
	return rc::estimate::required_limbs(sens->sens, tol, t);
}

// ---------------------------------------------------------------- PrecisionLadderResult

void PrecisionLadderResult::_bind_methods() {
#define RC_RES_PROP(name, vtype)                                                                      \
	ClassDB::bind_method(D_METHOD("get_" #name), &PrecisionLadderResult::get_##name);                \
	ADD_PROPERTY(PropertyInfo(Variant::vtype, #name), "", "get_" #name);
	RC_RES_PROP(planned, ARRAY)
	RC_RES_PROP(planned_err, ARRAY)
	RC_RES_PROP(planned_tier, INT)
	RC_RES_PROP(physical, ARRAY)
	RC_RES_PROP(physical_err, ARRAY)
	RC_RES_PROP(physical_tier, INT)
	RC_RES_PROP(converged, BOOL)
	RC_RES_PROP(tiers_tried, ARRAY)
	RC_RES_PROP(usec, INT)
	RC_RES_PROP(usec_planned, INT)
	RC_RES_PROP(usec_physical, INT)
	RC_RES_PROP(planned_from_physical, BOOL)
#undef RC_RES_PROP
	ClassDB::bind_method(D_METHOD("get_physical_sens"), &PrecisionLadderResult::get_physical_sens);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "physical_sens", godot::PROPERTY_HINT_RESOURCE_TYPE, "ErrorEstimateSensitivity"), "", "get_physical_sens");
}

Ref<PrecisionLadderResult> PrecisionLadderResult::wrap(const rc::LadderResult &r) {
	Ref<PrecisionLadderResult> o;
	o.instantiate();
	o->planned = climb_events(r.planned);
	o->planned_err = doubles_to_array(r.planned.err);
	o->planned_tier = r.planned.limbs;
	o->physical = climb_events(r.physical);
	o->physical_err = doubles_to_array(r.physical.err);
	o->physical_tier = r.physical.limbs;
	o->converged = r.physical.converged;
	for (int t : r.physical.tried) {
		o->tiers_tried.push_back(t);
	}
	o->usec = r.usec;
	o->usec_planned = r.usec_planned;
	o->usec_physical = r.usec_physical;
	// the float64 physical events are the accepted trace only when tier 0 was accepted
	o->physical_sens = ErrorEstimateSensitivity::wrap(r.physical_sens, r.physical.limbs == 0 ? events_to_array(r.physical.f64_events) : Array());
	o->planned_from_physical = r.planned_from_physical;
	return o;
}

// ---------------------------------------------------------------- PrecisionLadder

void PrecisionLadder::_bind_methods() {
	ClassDB::bind_static_method("PrecisionLadder", D_METHOD("tiers"), &PrecisionLadder::tiers);
	ClassDB::bind_static_method("PrecisionLadder", D_METHOD("solve", "defs", "player", "cursor", "plan", "max_planned", "max_physical", "tol", "verify", "fsc", "flags"),
			&PrecisionLadder::solve, DEFVAL(false), DEFVAL(Variant()), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("PrecisionLadder", D_METHOD("solve_physical", "defs", "player", "cursor", "plan", "max_physical", "tol", "fsc", "flags"),
			&PrecisionLadder::solve_physical, DEFVAL(Variant()), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("PrecisionLadder", D_METHOD("planned_residuals", "events", "scene"), &PrecisionLadder::planned_residuals);
	ClassDB::bind_static_method("PrecisionLadder", D_METHOD("event_distance", "a", "b"), &PrecisionLadder::event_distance);
}

Array PrecisionLadder::tiers() {
	Array a;
	for (int t : rc::TIERS) {
		a.push_back(t);
	}
	return a;
}

Ref<PrecisionLadderResult> PrecisionLadder::solve(const Array &defs, const Array &player, const Array &cursor, const Array &plan, int max_planned,
		int max_physical, double tol, bool verify, const Variant &fsc, const Dictionary &flags) {
	(void)fsc; // the ladder context builds its own float64 scene; results do not depend on caches
	double px, py, cx, cy;
	if (!pair_of(player, px, py, "PrecisionLadder.solve") || !pair_of(cursor, cx, cy, "PrecisionLadder.solve")) {
		return PrecisionLadderResult::wrap(rc::LadderResult{});
	}
	rc::LadderContext ctx(defs_from_array(defs));
	rc::Ladder ladder(ctx);
	return PrecisionLadderResult::wrap(ladder.solve(px, py, cx, cy, plan_from_items(plan), max_planned, max_physical, tol, verify, flags_from_dict(flags)));
}

Ref<PrecisionLadderResult> PrecisionLadder::solve_physical(const Array &defs, const Array &player, const Array &cursor, const Array &plan,
		int max_physical, double tol, const Variant &fsc, const Dictionary &flags) {
	(void)fsc;
	double px, py, cx, cy;
	if (!pair_of(player, px, py, "PrecisionLadder.solve_physical") || !pair_of(cursor, cx, cy, "PrecisionLadder.solve_physical")) {
		return PrecisionLadderResult::wrap(rc::LadderResult{});
	}
	rc::LadderContext ctx(defs_from_array(defs));
	rc::Ladder ladder(ctx);
	return PrecisionLadderResult::wrap(ladder.solve_physical(px, py, cx, cy, plan_from_items(plan), max_physical, tol, flags_from_dict(flags)));
}

Array PrecisionLadder::planned_residuals(const Array &events, const Ref<BeamScene> &scene) {
	if (scene.is_null()) {
		return Array();
	}
	const auto *p = std::get_if<std::shared_ptr<rc::Scene<rc::Dual>>>(&scene->scene);
	if (p == nullptr || *p == nullptr) {
		rc::report_error("PrecisionLadder.planned_residuals: the scene must be the float64 (dual) scene");
		return Array();
	}
	return doubles_to_array(rc::Ladder::planned_residuals(events_of<rc::Dual>(events), **p));
}

Array PrecisionLadder::event_distance(const Array &a, const Array &b) {
	return with_events(a, [&](const auto &ea) {
		return with_events(b, [&](const auto &eb) {
			return doubles_to_array(rc::Ladder::event_distance(ea, eb));
		});
	});
}
