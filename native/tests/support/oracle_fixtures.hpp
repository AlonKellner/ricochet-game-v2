// The oracle's trace fixtures (bench/fixtures/*.json, docs/research/oracle)
// walked natively and compared event by event (test support).
#pragma once
#include "goldens.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace rc::test {

// The fixture walked at tier N (emit: Scene::emit_infinity).
template <rc::Num N>
std::vector<Event<N>> walk_oracle_fixture(const ojson &fx, const std::vector<SurfaceDef> &defs, const N &like, bool emit) {
	Scene<N> scene = Scene<N>::from_defs(defs, like);
	scene.emit_infinity = emit;
	SolveCache<N> cache;
	Solver<N> solver(scene, &cache);
	const CN<N> player{like.like(fx["player"][0].get<double>()), like.like(fx["player"][1].get<double>())};
	std::optional<CN<N>> cursor;
	if (!fx["cursor"].is_null()) {
		cursor = CN<N>{like.like(fx["cursor"][0].get<double>()), like.like(fx["cursor"][1].get<double>())};
	}
	const Mode mode = fx["mode"].get<std::string>() == "physical" ? Mode::Physical : Mode::Planned;
	const std::vector<PlanItem> plan = plan_from_json(fx["plan"]);
	const Flags flags = fx.contains("flags") && !fx["flags"].is_null() ? flags_from_json(fx["flags"]) : Flags();
	if (!fx["direction"].is_null()) {
		Walk<N> w = solver.run_direction(player, CN<N>{like.like(fx["direction"][0].get<double>()), like.like(fx["direction"][1].get<double>())},
			plan, cursor, mode, MAX_HITS, flags);
		return w.events;
	}
	std::optional<Walk<N>> w = solver.run(player, *cursor, plan, mode, MAX_HITS, flags);
	return w ? w->events : std::vector<Event<N>>{};
}

// The oracle's event record compared field by field (points within tol px:
// 1e-9 as the GUT fixture tests compare, 0.01 for the corpus).
template <rc::Num N>
std::string diff_against_oracle(const std::vector<Event<N>> &events, const ojson &expected, double tol = 1e-9) {
	if (events.size() != expected.size()) {
		std::string got;
		for (const Event<N> &e : events) {
			got += std::string(event_kind_name(e.kind)) + " ";
		}
		return "count " + std::to_string(events.size()) + " vs oracle " + std::to_string(expected.size()) + " (" + got + ")";
	}
	for (size_t i = 0; i < events.size(); i++) {
		const Event<N> &e = events[i];
		const ojson &x = expected[i];
		const std::string side = e.kind == EventKind::Hit ? (e.left ? "LEFT" : "RIGHT") : "";
		if (std::string(event_kind_name(e.kind)) != x["kind"].get<std::string>() || e.surface_id != x["surface_id"].get<int>()
				|| side != x["side"].get<std::string>() || e.applied != x["applied"].get<bool>() || e.terminal != x["terminal"].get<bool>()
				|| e.on_segment != x["on_segment"].get<bool>() || e.rehit != x["rehit"].get<bool>()) {
			return "event " + std::to_string(i) + ": " + event_kind_name(e.kind) + " s=" + std::to_string(e.surface_id) + " " + side
				+ (e.applied ? " applied" : "") + " vs oracle " + x["kind"].get<std::string>() + " s=" + std::to_string(x["surface_id"].get<int>())
				+ " " + x["side"].get<std::string>() + (x["applied"].get<bool>() ? " applied" : "");
		}
		if (x.contains("state") && e.state != x["state"].get<bool>()) {
			return "event " + std::to_string(i) + ": state";
		}
		if (x.contains("fired") && e.fired != x["fired"].get<bool>()) {
			return "event " + std::to_string(i) + ": fired";
		}
		if (x.contains("flags") && !x["flags"].is_null() && e.flags.canonical_key() != flags_from_json(x["flags"]).canonical_key()) {
			return "event " + std::to_string(i) + ": flags " + e.flags.canonical_key();
		}
		if (x.contains("x")) {
			if (!e.point) {
				return "event " + std::to_string(i) + ": no point, oracle has one";
			}
			const double dx = e.point->fx() - std::stod(x["x"].get<std::string>());
			const double dy = e.point->fy() - std::stod(x["y"].get<std::string>());
			if (std::sqrt(dx * dx + dy * dy) > tol) {
				return "event " + std::to_string(i) + ": point off by " + std::to_string(std::sqrt(dx * dx + dy * dy));
			}
		} else if (e.point && e.kind != EventKind::Hit) {
			return "event " + std::to_string(i) + ": a point where the oracle has none";
		}
	}
	return "";
}

} // namespace rc::test
