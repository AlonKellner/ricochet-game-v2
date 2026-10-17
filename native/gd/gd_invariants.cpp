#include "gd_invariants.hpp"
#include "gd_visibility.hpp"

#include "rc/error.hpp"

#include <godot_cpp/core/class_db.hpp>

using godot::Array;
using godot::ClassDB;
using godot::D_METHOD;
using godot::Dictionary;
using godot::PackedVector2Array;
using godot::Rect2;
using godot::Ref;
using godot::String;
using godot::Variant;
using godot::Vector2;
using namespace rcgd;

// ---------------------------------------------------------------- converters

std::vector<rc::inv::EvInfo> ev_infos_of(const Array &events) {
	std::vector<rc::inv::EvInfo> out;
	for (int64_t i = 0; i < events.size(); i++) {
		const Ref<BeamSolverEvent> e = events[i];
		if (e.is_valid()) {
			out.push_back(std::visit([](const auto &ev) { return rc::inv::ev_info(ev); }, e->event));
		}
	}
	return out;
}

std::vector<rc::inv::SurfaceInfo> surface_infos_of(const Array &surfaces) {
	std::vector<rc::inv::SurfaceInfo> out;
	for (int64_t i = 0; i < surfaces.size(); i++) {
		const Dictionary d = surfaces[i];
		rc::inv::SurfaceInfo info;
		info.id = int(int64_t(d.get("id", 0)));
		info.has_effect = bool(d.get("has_effect", false));
		Ref<Segment> seg = d.get("segment", Variant());
		if (seg.is_valid()) {
			const rc::arc::SegGeom g = seg->geom();
			info.s = g.s;
			info.e = g.e;
			info.v = g.v;
			info.full = g.full;
			info.carrier = seg->get_carrier()->g;
		}
		out.push_back(info);
	}
	return out;
}

rc::TracePath core_path_of(const Ref<TracePath> &path) {
	rc::TracePath p;
	if (path.is_null()) {
		return p;
	}
	for (int64_t i = 0; i < path->steps.size(); i++) {
		const Ref<TraceStep> s = path->steps[i];
		if (s.is_valid()) {
			rc::TraceStep st = s->to_rc();
			if (st.hit && s->hit.is_valid() && s->hit->segment.get_type() == Variant::NIL) {
				st.hit->surface_id = -1; // InvariantChecker: a hit without a Segment belongs to no surface
			}
			p.steps.push_back(st);
		}
	}
	p.cursor_index = path->cursor_index;
	p.hit_count = path->hit_count;
	p.truncated = path->truncated;
	const Array keys = path->targets_hit.keys();
	for (int64_t i = 0; i < keys.size(); i++) {
		p.targets_hit.push_back(int(int64_t(keys[i])));
	}
	return p;
}

namespace {

std::vector<rc::TraceStep> core_steps_of(const Array &steps) {
	std::vector<rc::TraceStep> out;
	for (int64_t i = 0; i < steps.size(); i++) {
		const Ref<TraceStep> s = steps[i];
		if (s.is_valid()) {
			out.push_back(s->to_rc());
		}
	}
	return out;
}

Variant arg_variant(const rc::inv::Arg &a) {
	if (const auto *i = std::get_if<int64_t>(&a)) {
		return *i;
	}
	if (const auto *f = std::get_if<double>(&a)) {
		return *f;
	}
	if (const auto *v = std::get_if<rc::V2>(&a)) {
		return to_vec(*v);
	}
	if (const auto *s = std::get_if<std::string>(&a)) {
		return to_gd(*s);
	}
	if (const auto *b = std::get_if<bool>(&a)) {
		return *b;
	}
	const auto &pairs = std::get<rc::inv::PairList>(a);
	Array out;
	for (const auto &p : pairs) {
		Array pair;
		pair.push_back(p.first);
		pair.push_back(p.second);
		out.push_back(pair);
	}
	return out;
}

// the F64 scene of a BeamScene wrapper (the checker's tier), or null
const rc::Scene<rc::F64> *f64_scene_of(const Ref<BeamScene> &scene) {
	if (scene.is_null()) {
		return nullptr;
	}
	const auto *p = std::get_if<std::shared_ptr<rc::Scene<rc::F64>>>(&scene->scene);
	if (p == nullptr || *p == nullptr) {
		rc::report_error("InvariantKernel: the scene must be BeamScene.from_defs(NumCtx.f64(), defs)");
		return nullptr;
	}
	return p->get();
}

} // namespace

Array render_violations(const rc::inv::Violations &vs) {
	Array out;
	for (const rc::inv::Violation &v : vs) {
		Array args;
		for (const rc::inv::Arg &a : v.args) {
			args.push_back(arg_variant(a));
		}
		out.push_back(to_gd(v.fmt) % args); // GDScript's "%" formatting
	}
	return out;
}

// ---------------------------------------------------------------- InvariantKernel

void InvariantKernel::_bind_methods() {
	ClassDB::bind_static_method("InvariantKernel", D_METHOD("check_visibility_pin", "region", "cursor", "planned", "typed", "cursor_point_agrees"),
			&InvariantKernel::check_visibility_pin, DEFVAL(true));
	ClassDB::bind_static_method("InvariantKernel",
			D_METHOD("check_all", "player", "cursor", "physical", "planned", "typed", "result", "surfaces", "defs", "flags", "plan_entries", "has_renderer", "plan_applied", "emit_infinity",
				"cursor_point_agrees"),
			&InvariantKernel::check_all, DEFVAL(false), DEFVAL(true));
	ClassDB::bind_static_method("InvariantKernel", D_METHOD("check_direction_only", "player", "cursor", "result", "defs", "flags", "plan_entries", "emit_infinity"),
			&InvariantKernel::check_direction_only, DEFVAL(false));
	ClassDB::bind_static_method("InvariantKernel", D_METHOD("check_effect_law", "events", "scene"), &InvariantKernel::check_effect_law);
	ClassDB::bind_static_method("InvariantKernel", D_METHOD("check_event_on_beam", "events", "scene"), &InvariantKernel::check_event_on_beam);
	ClassDB::bind_static_method("InvariantKernel", D_METHOD("check_beam_order", "events"), &InvariantKernel::check_beam_order);
	ClassDB::bind_static_method("InvariantKernel", D_METHOD("check_reapply_in_place", "events"), &InvariantKernel::check_reapply_in_place);
	ClassDB::bind_static_method("InvariantKernel", D_METHOD("check_plan_effects_applied_events", "events", "plan", "cursor_reached"),
			&InvariantKernel::check_plan_effects_applied_events);
	ClassDB::bind_static_method("InvariantKernel", D_METHOD("is_infinity_gap", "prev_end", "curr_start"), &InvariantKernel::is_infinity_gap);
	ClassDB::bind_static_method("InvariantKernel", D_METHOD("is_at_bounds", "p", "bounds"), &InvariantKernel::is_at_bounds);
	ClassDB::bind_static_method("InvariantKernel", D_METHOD("geometric_carrier_dist", "point", "carrier"), &InvariantKernel::geometric_carrier_dist);
	ClassDB::bind_static_method("InvariantKernel", D_METHOD("extract_trace_geometry", "path"), &InvariantKernel::extract_trace_geometry);
}

Array InvariantKernel::check_all(const Vector2 &player, const Vector2 &cursor, const Ref<TracePath> &physical, const Ref<TracePath> &planned, const Array &typed,
		const Ref<PrecisionLadderResult> &result, const Array &surfaces, const Array &defs, const Dictionary &flags, const Array &plan_entries,
		bool has_renderer, bool plan_applied, bool emit_infinity, bool cursor_point_agrees) {
	rc::inv::Input in;
	in.player = to_v2(player);
	in.cursor = to_v2(cursor);
	in.has_renderer = has_renderer;
	in.cursor_point_agrees = cursor_point_agrees;
	in.emit_infinity = emit_infinity;
	const rc::TracePath ph = core_path_of(physical);
	const rc::TracePath pl = core_path_of(planned);
	if (physical.is_valid()) {
		in.physical = &ph;
	}
	if (planned.is_valid()) {
		in.planned = &pl;
	}
	const std::vector<rc::TraceStep> ty = core_steps_of(typed);
	in.typed = &ty;
	std::vector<rc::inv::EvInfo> pev;
	std::vector<rc::inv::EvInfo> hev;
	if (result.is_valid()) {
		pev = ev_infos_of(result->planned);
		hev = ev_infos_of(result->physical);
		in.planned_events = &pev;
		in.physical_events = &hev;
	}
	const std::vector<rc::inv::SurfaceInfo> infos = surface_infos_of(surfaces);
	in.surfaces = &infos;
	const std::vector<rc::SurfaceDef> d = defs_from_array(defs);
	in.defs = &d;
	rc::Scene<rc::F64> scene = rc::Scene<rc::F64>::from_defs(d, rc::F64{1.0});
	scene.emit_infinity = emit_infinity;
	in.scene = &scene;
	in.flags = flags_from_dict(flags);
	in.plan = plan_from_items(plan_entries);
	in.plan_applied = plan_applied;
	return render_violations(rc::inv::check_all(in));
}

Array InvariantKernel::check_direction_only(const Vector2 &player, const Vector2 &cursor, const Ref<PrecisionLadderResult> &result, const Array &defs,
		const Dictionary &flags, const Array &plan_entries, bool emit_infinity) {
	rc::inv::Input in;
	in.player = to_v2(player);
	in.cursor = to_v2(cursor);
	in.emit_infinity = emit_infinity;
	std::vector<rc::inv::EvInfo> pev;
	std::vector<rc::inv::EvInfo> hev;
	if (result.is_valid()) {
		pev = ev_infos_of(result->planned);
		hev = ev_infos_of(result->physical);
		in.planned_events = &pev;
		in.physical_events = &hev;
	}
	const std::vector<rc::SurfaceDef> d = defs_from_array(defs);
	in.defs = &d;
	rc::Scene<rc::F64> scene = rc::Scene<rc::F64>::from_defs(d, rc::F64{1.0});
	scene.emit_infinity = emit_infinity;
	in.scene = &scene;
	in.flags = flags_from_dict(flags);
	in.plan = plan_from_items(plan_entries);
	in.plan_applied = !in.plan.empty();
	return render_violations(rc::inv::check_DIRECTION_ONLY(in));
}

Array InvariantKernel::check_effect_law(const Array &events, const Ref<BeamScene> &scene) {
	const rc::Scene<rc::F64> *sc = f64_scene_of(scene);
	return sc == nullptr ? Array() : render_violations(rc::inv::check_effect_law(ev_infos_of(events), *sc));
}
Array InvariantKernel::check_event_on_beam(const Array &events, const Ref<BeamScene> &scene) {
	const rc::Scene<rc::F64> *sc = f64_scene_of(scene);
	return sc == nullptr ? Array() : render_violations(rc::inv::check_event_on_beam(ev_infos_of(events), *sc));
}
Array InvariantKernel::check_beam_order(const Array &events) {
	return render_violations(rc::inv::check_beam_order(ev_infos_of(events)));
}
Array InvariantKernel::check_reapply_in_place(const Array &events) {
	return render_violations(rc::inv::check_reapply_in_place(ev_infos_of(events)));
}
Array InvariantKernel::check_plan_effects_applied_events(const Array &events, const Array &plan, bool cursor_reached) {
	(void)cursor_reached; // unused in GDScript too
	return render_violations(rc::inv::check_plan_effects_applied_events(ev_infos_of(events), plan_from_items(plan)));
}
bool InvariantKernel::is_infinity_gap(const Vector2 &prev_end, const Vector2 &curr_start) {
	return rc::inv::is_infinity_gap(to_v2(prev_end), to_v2(curr_start));
}
bool InvariantKernel::is_at_bounds(const Vector2 &p, const Rect2 &bounds) {
	return rc::inv::is_at_bounds(to_v2(p), to_rect(bounds));
}
double InvariantKernel::geometric_carrier_dist(const Vector2 &point, const Ref<GeneralizedCircle> &carrier) {
	if (carrier.is_null()) {
		rc::report_error("InvariantKernel.geometric_carrier_dist: null carrier");
		return 0.0;
	}
	return rc::inv::geometric_carrier_dist(to_v2(point), carrier->g);
}
Array InvariantKernel::extract_trace_geometry(const Ref<TracePath> &path) {
	Array out;
	const rc::TracePath p = core_path_of(path);
	for (const rc::inv::GeomSegment &g : rc::inv::extract_trace_geometry(path.is_null() ? nullptr : &p)) {
		Dictionary d;
		d["start"] = to_vec(g.start);
		d["end"] = to_vec(g.end);
		out.push_back(d);
	}
	return out;
}

Array InvariantKernel::check_visibility_pin(const Ref<RefCounted> &region, const Vector2 &cursor, const Ref<TracePath> &planned, const Array &typed,
		bool cursor_point_agrees) {
	const VisibilityRegion *r = Object::cast_to<VisibilityRegion>(region.ptr());
	if (r == nullptr || planned.is_null()) {
		return Array();
	}
	const rc::TracePath pl = core_path_of(planned);
	const std::vector<rc::TraceStep> ty = core_steps_of(typed);
	return render_violations(rc::inv::check_VISIBILITY_EQUALS_NON_DIVERGENCE(r->region, to_v2(cursor), &pl, &ty, cursor_point_agrees));
}

// ---------------------------------------------------------------- SweepKernel

void SweepKernel::_bind_methods() {
	ClassDB::bind_static_method("SweepKernel",
			D_METHOD("run", "defs", "surfaces", "positions", "plans", "flags", "shard_i", "shard_n", "skip_equal", "emit_infinity", "visibility", "carrier_cursors"),
			&SweepKernel::run, DEFVAL(false), DEFVAL(false), DEFVAL(false));
}

Array SweepKernel::run(const Array &defs, const Array &surfaces, const PackedVector2Array &positions, const Array &plans, const Dictionary &flags, int shard_i,
		int shard_n, bool skip_equal, bool emit_infinity, bool visibility, bool carrier_cursors) {
	const std::vector<rc::SurfaceDef> d = defs_from_array(defs);
	const std::vector<rc::inv::SurfaceInfo> infos = surface_infos_of(surfaces);
	std::vector<rc::V2> pos;
	for (int64_t i = 0; i < positions.size(); i++) {
		pos.push_back(to_v2(positions[i]));
	}
	std::vector<std::vector<rc::PlanItem>> pls;
	for (int64_t i = 0; i < plans.size(); i++) {
		pls.push_back(plan_from_items(plans[i]));
	}
	const std::vector<rc::sweep::Combo> combos = rc::sweep::run(d, infos, pos, pls, flags_from_dict(flags), shard_i, shard_n, skip_equal, true, emit_infinity,
		visibility, carrier_cursors);
	Array out;
	for (const rc::sweep::Combo &c : combos) {
		Dictionary e;
		e["plan_index"] = c.plan_index;
		e["player"] = to_vec(c.player);
		e["cursor"] = to_vec(c.cursor);
		e["violations"] = render_violations(c.violations);
		out.push_back(e);
	}
	return out;
}
