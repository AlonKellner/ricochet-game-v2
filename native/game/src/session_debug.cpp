// Stage 128: the debug dump (the GDScript game's F12 DEBUG STATE, DebugDumper,
// lost with it in Stage 119.4): the session's state as JSON -- the solver's
// inputs exactly (hex floats: a dump is replayed bit for bit), the scene, the
// plan, the preview's walks and the region. A tool: no rule reads it.
#include "rcg/session.hpp"

#include "rcg/trace_json.hpp"

#include <cstdio>

namespace rcg {
namespace {

std::string hex(double v) {
	char buf[40];
	std::snprintf(buf, sizeof buf, "%a", v);
	return buf;
}

nlohmann::json point(const V2 &p) {
	return {{"x", p.x}, {"y", p.y}, {"hex", {hex(double(p.x)), hex(double(p.y))}}};
}

template <class E>
nlohmann::json event(const E &e) {
	nlohmann::json j = {{"kind", rc::event_kind_name(e.kind)}, {"surface", e.surface_id}, {"side", e.left ? "L" : "R"}};
	if (e.point) {
		j["point"] = {e.point->fx(), e.point->fy()};
	}
	j["applied"] = e.applied;
	j["on_segment"] = e.on_segment;
	if (e.terminal) {
		j["terminal"] = true;
	}
	if (e.margin < rc::INF_D) {
		j["margin"] = e.margin;
	}
	return j;
}

nlohmann::json climb(const rc::Climb &c) {
	nlohmann::json evs = nlohmann::json::array();
	if (c.limbs == 0) {
		for (const auto &e : c.f64_events) {
			evs.push_back(event(e));
		}
	} else {
		for (const auto &e : c.big_events) {
			evs.push_back(event(e));
		}
	}
	return {{"limbs", c.limbs}, {"converged", c.converged}, {"tried", c.tried}, {"events", evs}};
}

const char *step_type(rc::StepType t) {
	switch (t) {
		case rc::StepType::Aligned: return "ALIGNED";
		case rc::StepType::AlignedPostPlanned: return "ALIGNED_POST";
		case rc::StepType::DivergedPhysical: return "DIV_PHYS";
		case rc::StepType::DivergedPlanned: return "DIV_PLAN";
		case rc::StepType::DivergedPostPlanned: return "DIV_POST";
	}
	return "?";
}

} // namespace

nlohmann::json Session::debug_dump() const {
	nlohmann::json j;
	j["level"] = level_.name;
	j["player"] = point(player_);
	j["cursor"] = point(cursor_);
	j["flying"] = flying_;
	j["switches"] = {{"infinity", infinity_on()}, {"visibility", visibility_on()}, {"display_mode", display_mode_}};
	j["flags"] = trace_json::flags(flags_);
	nlohmann::json plan = nlohmann::json::array();
	for (const PlanEntry &e : plan_) {
		nlohmann::json pe = {{"surface", e.surface_id}, {"side", e.side == Side::Left ? "L" : "R"}, {"piece", e.piece}};
		if (e.click != V2::inf()) {
			pe["click"] = point(e.click);
		}
		plan.push_back(pe);
	}
	j["plan"] = plan;
	j["plan_items"] = trace_json::plan(items()); // what the solver is given
	j["defs"] = trace_json::defs(world_->defs());

	const Preview &p = shown();
	nlohmann::json pv = {{"exact", preview_exact()}, {"climb_seconds", preview_seconds()}};
	if (p.last_result) {
		const rc::LadderResult &r = *p.last_result;
		pv["ladder"] = {{"planned", climb(r.planned)}, {"physical", climb(r.physical)}, {"planned_from_physical", r.planned_from_physical},
			{"usec", r.usec}};
	}
	pv["pair"] = trace_json::pair(p.pair);
	nlohmann::json merged = nlohmann::json::array();
	for (const rc::TraceStep &s : p.merged) {
		merged.push_back({{"start", {s.start.x, s.start.y}}, {"end", {s.end.x, s.end.y}}, {"type", step_type(s.type)}, {"frame", s.frame_id},
			{"surface", s.surface_id}, {"arc", s.is_arc_step}});
	}
	pv["merged"] = merged;
	j["preview"] = pv;

	nlohmann::json rg = {{"pending", region_pending()}, {"requested", vis_ticket_}, {"shown", vis_applied_}};
	if (region_) {
		const rc::vis::Result &r = *region_;
		size_t edges = 0;
		for (const rc::vis::Loop &l : r.loops) {
			edges += l.edges.size();
		}
		rg["player"] = {r.player.x, r.player.y};
		rg["supported"] = r.supported;
		rg["reason"] = r.reason;
		rg["split"] = r.split;
		rg["cells"] = r.cells.size();
		rg["loops"] = r.loops.size();
		rg["edges"] = edges;
		rg["whiskers"] = r.whiskers.size();
		rg["members"] = r.members.size();
		rg["casts"] = r.casts;
		rg["build_ms"] = double(r.build_usec) / 1000.0;
		rg["notes"] = r.notes;
	}
	j["region"] = rg;
	return j;
}

} // namespace rcg
