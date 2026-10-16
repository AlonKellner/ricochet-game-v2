// The native invariant sweep (Stage 98): for every (player, cursor, plan)
// combo, the trace pair, its display paths (VisualConverter.prepare_for_display
// on core paths), the merged typed steps (StepTreeMerge.merge) and
// inv::check_all, with no scene tree in the loop. Positions come from the
// caller (SweepRunner.build_positions stays GDScript: Godot's RNG).
#pragma once

#include "rc/invariants.hpp"
#include "rc/tracer.hpp"
#include "rc/visual.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace rc {
namespace sweep {

inline std::function<std::optional<arc::GeneralizedCircle>(int)> carrier_of(const std::vector<inv::SurfaceInfo> &surfaces) {
	return [&surfaces](int surface_id) -> std::optional<arc::GeneralizedCircle> {
		for (const inv::SurfaceInfo &s : surfaces) {
			if (s.id == surface_id) {
				return s.carrier;
			}
		}
		return std::nullopt;
	};
}

inline arc::StepGeom step_geom(const TraceStep &s) {
	arc::StepGeom g;
	g.start = s.start;
	g.end = s.end;
	g.via = s.via;
	g.is_arc_step = s.is_arc_step;
	g.has_hit = s.hit.has_value();
	g.surface_id = s.surface_id;
	if (s.ray) {
		g.has_ray = true;
		g.ray_dir = s.ray->to_normalized();
	}
	return g;
}

// VisualConverter._piece
inline TraceStep piece_of(const TraceStep &step, const V2 &p_start, const V2 &p_end, bool keep_hit, const V2 &via, bool first) {
	TraceStep piece = TraceStep::make(p_start, p_end, step.frame_id, keep_hit ? step.hit : std::nullopt, step.ray, via, false);
	piece.type = step.type;
	piece.est_error = step.est_error;
	piece.precision_bits = step.precision_bits;
	piece.after_portal = first ? step.after_portal : false;
	if (keep_hit && step.hit) {
		piece.surface_id = step.surface_id;
		piece.hit_side = step.hit_side;
		piece.hit_on_segment = step.hit_on_segment;
	}
	return piece;
}

// VisualConverter.prepare_for_display on a core path (hit_count stays 0, as in GDScript)
inline TracePath prepare_for_display(const TracePath &path, const Rect2f &bounds, const std::vector<inv::SurfaceInfo> &surfaces) {
	std::vector<arc::StepGeom> geoms;
	geoms.reserve(path.steps.size());
	for (const TraceStep &s : path.steps) {
		geoms.push_back(step_geom(s));
	}
	const arc::DisplayPlan plan = arc::prepare_for_display(geoms, path.cursor_index, bounds, carrier_of(surfaces));
	TracePath result;
	result.targets_hit = path.targets_hit;
	result.cursor_index = plan.cursor_index;
	result.truncated = path.truncated;
	for (const arc::Piece &p : plan.pieces) {
		const TraceStep &src = path.steps[size_t(p.src)];
		switch (p.kind) {
			case arc::Piece::Identity:
				result.steps.push_back(src);
				break;
			case arc::Piece::Snapped: {
				TraceStep snapped = TraceStep::make(src.start, p.end, src.frame_id, src.hit, src.ray, src.via, src.is_arc_step);
				snapped.type = src.type;
				snapped.copy_meta_from(src);
				result.steps.push_back(snapped);
				break;
			}
			case arc::Piece::Clipped:
				result.steps.push_back(piece_of(src, p.start, p.end, p.keep_hit, p.via, p.first));
				break;
		}
	}
	return result;
}

// StepTreeMerge.merge
inline std::vector<TraceStep> merge(const std::vector<TraceStep> &planned, const std::vector<TraceStep> &physical, int cursor_index) {
	std::vector<TraceStep> merged;
	bool diverged = false;
	const size_t max_len = std::max(planned.size(), physical.size());
	for (size_t idx = 0; idx < max_len; idx++) {
		const TraceStep *p = idx < planned.size() ? &planned[idx] : nullptr;
		const TraceStep *r = idx < physical.size() ? &physical[idx] : nullptr;
		const bool past_cursor = int(idx) >= cursor_index;
		if (!diverged && p != nullptr && r != nullptr && p->frame_id == r->frame_id) {
			merged.push_back(p->with_type(past_cursor ? StepType::AlignedPostPlanned : StepType::Aligned));
		} else {
			diverged = true;
			if (p != nullptr) {
				merged.push_back(p->with_type(past_cursor ? StepType::DivergedPostPlanned : StepType::DivergedPlanned));
			}
			if (r != nullptr) {
				merged.push_back(r->with_type(StepType::DivergedPhysical));
			}
		}
	}
	return merged;
}

struct Combo {
	int plan_index = 0;
	V2 player;
	V2 cursor;
	inv::Violations violations;
};

// One checked combo: what PathRenderer._compute_trace + InvariantChecker.check_all do.
// visibility: the player's region (Stage 110), pinned against this cursor;
// pin_only: that check alone (the visibility sweep).
inline inv::Violations check_combo(Tracer &tracer, const Scene<F64> &scene, const std::vector<SurfaceDef> &defs, const std::vector<inv::SurfaceInfo> &surfaces,
		const V2 &player, const V2 &cursor, const std::vector<PlanItem> &plan, const Flags &flags, const Rect2f &bounds,
		const vis::Result *visibility = nullptr, bool pin_only = false) {
	inv::Input in;
	in.player = player;
	in.cursor = cursor;
	in.flags = flags;
	in.plan = plan;
	in.plan_applied = !plan.empty();
	in.surfaces = &surfaces;
	in.scene = &scene;
	in.emit_infinity = tracer.emit_infinity();
	in.defs = &defs;
	in.bounds = bounds;
	const std::vector<TraceStep> no_steps;
	in.typed = &no_steps;
	const std::optional<TracePair> pair = player == cursor ? std::nullopt : tracer.trace_pair(player, cursor, plan, flags);
	TracePath disp_physical;
	TracePath disp_planned;
	std::vector<TraceStep> typed;
	std::vector<inv::EvInfo> planned_events;
	std::vector<inv::EvInfo> physical_events;
	if (pair) {
		disp_physical = prepare_for_display(pair->physical, bounds, surfaces);
		disp_planned = prepare_for_display(pair->planned, bounds, surfaces);
		int ci = disp_planned.cursor_index;
		if (ci < 0) {
			ci = int(disp_planned.steps.size());
		}
		typed = merge(disp_planned.steps, disp_physical.steps, ci);
		in.physical = &disp_physical;
		in.planned = &disp_planned;
		in.typed = &typed;
		planned_events = inv::ev_infos(pair->result.planned);
		physical_events = inv::ev_infos(pair->result.physical);
		in.planned_events = &planned_events;
		in.physical_events = &physical_events;
	}
	if (pin_only) {
		return visibility == nullptr || player == cursor || !pair ? inv::Violations{}
			: inv::check_VISIBILITY_EQUALS_NON_DIVERGENCE(*visibility, cursor, &disp_planned, &typed);
	}
	in.visibility = visibility;
	return inv::check_all(in);
}

// Every combo of positions x positions under every plan; shard_n > 1 keeps the
// player indices with index % shard_n == shard_i (SweepRunner.in_shard).
inline std::vector<Combo> run(const std::vector<SurfaceDef> &defs, const std::vector<inv::SurfaceInfo> &surfaces, const std::vector<V2> &positions,
		const std::vector<std::vector<PlanItem>> &plans, const Flags &flags, int shard_i, int shard_n, bool skip_equal, bool only_failures,
		bool emit_infinity = false, bool visibility = false) {
	std::vector<Combo> out;
	Tracer tracer;
	tracer.set_emit_infinity(emit_infinity);
	tracer.set_defs(defs);
	Scene<F64> scene = Scene<F64>::from_defs(defs, F64{1.0});
	scene.emit_infinity = emit_infinity;
	const Rect2f bounds = Rect2f::of(0, 0, 1920, 1080);
	for (size_t pli = 0; pli < plans.size(); pli++) {
		for (size_t pi = 0; pi < positions.size(); pi++) {
			if (shard_n > 1 && int(pi) % shard_n != shard_i) {
				continue;
			}
			// Stage 110: the visibility sweep -- the player's region, its structure
			// (reported at the player), then the pin at every cursor
			std::optional<vis::Result> region;
			if (visibility) {
				const vis::Pt P{positions[pi].x, positions[pi].y};
				const vis::Rect vb{bounds.position.x, bounds.position.y, bounds.end().x, bounds.end().y};
				region = vis::compute(tracer, P, plans[pli], flags, vb);
				inv::VisInput vin{&*region, &tracer, P, plans[pli], flags, vb};
				inv::Violations vs = inv::check_visibility(vin);
				if (!vs.empty() || !only_failures) {
					out.push_back(Combo{int(pli), positions[pi], positions[pi], std::move(vs)});
				}
			}
			for (const V2 &cursor : positions) {
				if (skip_equal && positions[pi] == cursor) {
					continue;
				}
				inv::Violations vs = check_combo(tracer, scene, defs, surfaces, positions[pi], cursor, plans[pli], flags, bounds,
					region ? &*region : nullptr, visibility);
				if (!vs.empty() || !only_failures) {
					out.push_back(Combo{int(pli), positions[pi], cursor, std::move(vs)});
				}
			}
		}
	}
	return out;
}

} // namespace sweep
} // namespace rc
