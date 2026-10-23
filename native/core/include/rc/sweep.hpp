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
	// (Stage 131: the piece that ends where the step does -- for a step to infinity, the one toward it)
	piece.significant = step.significant && (p_end == step.end || !std::isfinite(step.end.x));
	piece.to_infinity = step.to_infinity && (p_end == step.end || !std::isfinite(step.end.x));
	piece.after_infinity = first ? step.after_infinity : false;
	piece.teleports = step.teleports && p_end == step.end;
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

// Stage 134 (GAME_SPEC 14.5): the merge by segments. The walks compute
// different candidates (a planned walk waiting on its entries computes nothing
// else), so their steps no longer pair by index: each path is cut into segments
// -- the runs of steps between significant events (and frame changes) -- and
// the segments pair by index. A pair alike (one frame, one end, both ending at a
// significant event or neither) is drawn once, from the planned path. At the
// first pair not alike the walks run together from the segment's start to the
// nearer of their two ends, the divergence point; past it each walk is drawn
// on its own. The non-significant events of both walks are dots, coloured by
// the walk that computed them.

// An event dot: at the end of a drawn step (unshifted), on merged step `step`;
// from: 1 the planned walk, 2 the physical one, 3 both.
struct EventDot {
	V2 at;
	int step = -1;
	int from = 0;
};

struct Merged {
	std::vector<TraceStep> steps;
	std::vector<EventDot> dots;
};

// the segments of a path: [begin, end) step ranges, cut after a significant
// step and where the frame changes
inline std::vector<std::pair<size_t, size_t>> segments_of(const std::vector<TraceStep> &steps) {
	std::vector<std::pair<size_t, size_t>> out;
	size_t begin = 0;
	for (size_t i = 0; i < steps.size(); i++) {
		if (steps[i].significant || i + 1 == steps.size() || steps[i + 1].frame_id != steps[i].frame_id) {
			out.emplace_back(begin, i + 1);
			begin = i + 1;
		}
	}
	return out;
}

using arc::fraction_on;
using arc::near_pt;

// a step cut at p (on it): the part before (no event at its end) and the part after
inline std::pair<TraceStep, TraceStep> split_step(const TraceStep &st, const V2 &p) {
	V2 via0 = (st.start + p) / 2.0f;
	V2 via1 = (p + st.end) / 2.0f;
	const bool curved = st.is_arc_step && arc::is_arc(st.start, st.via, st.end);
	if (curved) {
		const arc::ArcParams a = arc::arc_params(st.start, st.via, st.end);
		const double ang = std::atan2(double(p.y) - double(a.center.y), double(p.x) - double(a.center.x));
		const double sgn = a.clockwise ? -1.0 : 1.0;
		const double d = a.clockwise ? fposmod(a.start_angle - ang, arc::TAU) : fposmod(ang - a.start_angle, arc::TAU);
		auto at = [&](double off) {
			const double th = a.start_angle + sgn * off;
			return V2::of(double(a.center.x) + a.radius * std::cos(th), double(a.center.y) + a.radius * std::sin(th));
		};
		via0 = at(d / 2.0);
		via1 = at((d + a.span) / 2.0);
	}
	TraceStep first = TraceStep::make(st.start, p, st.frame_id, std::nullopt, st.ray, via0, curved);
	first.type = st.type;
	first.copy_meta_from(st);
	first.surface_id = -1;
	first.hit_on_segment = false;
	first.significant = false;
	first.to_infinity = false;
	first.teleports = false;
	TraceStep second = TraceStep::make(p, st.end, st.frame_id, st.hit, st.ray, via1, curved);
	second.type = st.type;
	second.copy_meta_from(st);
	second.after_portal = false;
	second.after_infinity = false;
	return {first, second};
}

// Stage 134: the merge by segments (the user: "the paths are still separated by
// significant events"); pl_ci / ph_ci: the paths' cursor indices (their sizes
// when none)
inline Merged merge_segments(const std::vector<TraceStep> &planned, const std::vector<TraceStep> &physical, int pl_ci, int ph_ci) {
	Merged m;
	const auto spl = segments_of(planned);
	const auto sph = segments_of(physical);
	auto typed_pl = [&](const TraceStep &st, size_t idx, bool aligned) {
		const bool past = int(idx) >= pl_ci;
		return st.with_type(aligned ? (past ? StepType::AlignedPostPlanned : StepType::Aligned)
									: (past ? StepType::DivergedPostPlanned : StepType::DivergedPlanned));
	};
	auto typed_ph_aligned = [&](const TraceStep &st, size_t idx) {
		return st.with_type(int(idx) >= ph_ci ? StepType::AlignedPostPlanned : StepType::Aligned);
	};
	// the dots of a run drawn together: each walk's non-significant step ends there
	// (`own`: the merged steps the run drew, from `own_pl` -- the planned walk's -- or the physical)
	auto run_dots = [&](size_t merged0, const std::vector<std::pair<const TraceStep *, bool>> &ends_pl,
						const std::vector<std::pair<const TraceStep *, bool>> &ends_ph) {
		const size_t merged1 = m.steps.size();
		const size_t first_dot = m.dots.size();
		auto place = [&](const V2 &at) {
			for (size_t j = merged0; j < merged1; j++) {
				if (const std::optional<double> f = fraction_on(m.steps[j], at)) {
					return int(j);
				}
			}
			return merged1 > merged0 ? int(merged1 - 1) : -1;
		};
		for (int from : {1, 2}) {
			for (const auto &[st, dot] : from == 1 ? ends_pl : ends_ph) {
				if (!dot || st->significant || st->start == st->end) {
					continue;
				}
				bool merged = false;
				for (size_t k = first_dot; k < m.dots.size() && !merged; k++) {
					if ((m.dots[k].from & from) == 0 && near_pt(m.dots[k].at, st->end)) {
						m.dots[k].from |= from;
						merged = true;
					}
				}
				if (!merged) {
					m.dots.push_back(EventDot{st->end, place(st->end), from});
				}
			}
		}
	};
	// (and one that begins after it, where its step comes back into the bounds)
	auto same_start = [&](size_t a0, size_t b0) {
		const bool wrapped = a0 > 0 && b0 > 0 && planned[a0 - 1].to_infinity && physical[b0 - 1].to_infinity;
		return wrapped || near_pt(planned[a0].start, physical[b0].start);
	};
	size_t k = 0;
	for (; k < spl.size() && k < sph.size(); k++) {
		const auto [a0, a1] = spl[k];
		const auto [b0, b1] = sph[k];
		const TraceStep &pa = planned[a1 - 1];
		const TraceStep &pb = physical[b1 - 1];
		// (a segment that ends at a passage of infinity ends where its step leaves the
		// bounds toward it, clipped from the step's own start: both walks wrapping is one end)
		const bool wraps = pa.to_infinity && pb.to_infinity;
		const bool alike = planned[a0].frame_id == physical[b0].frame_id && same_start(a0, b0)
			&& (wraps || near_pt(pa.end, pb.end)) && pa.significant == pb.significant && (pa.significant || (a1 == planned.size()) == (b1 == physical.size()));
		if (!alike) {
			break;
		}
		const size_t m0 = m.steps.size();
		std::vector<std::pair<const TraceStep *, bool>> epl, eph;
		for (size_t i = a0; i < a1; i++) {
			m.steps.push_back(typed_pl(planned[i], i, true));
			epl.emplace_back(&planned[i], true);
		}
		for (size_t i = b0; i < b1; i++) {
			eph.emplace_back(&physical[i], true);
		}
		run_dots(m0, epl, eph);
	}
	// the divergence: the pair's common part, to the nearer of the two ends
	std::vector<TraceStep> rest_pl, rest_ph;
	std::vector<size_t> rest_pl_idx;
	size_t from_pl = k < spl.size() ? spl[k].first : planned.size();
	size_t from_ph = k < sph.size() ? sph[k].first : physical.size();
	if (k < spl.size() && k < sph.size()) {
		const auto [a0, a1] = spl[k];
		const auto [b0, b1] = sph[k];
		if (planned[a0].frame_id == physical[b0].frame_id && same_start(a0, b0)) {
			// whose end comes first: the physical end on the planned segment, or the planned end on the physical one
			auto locate = [](const std::vector<TraceStep> &steps, size_t s0, size_t s1, const V2 &p) -> std::optional<std::pair<size_t, double>> {
				for (size_t i = s0; i < s1; i++) {
					if (const std::optional<double> f = fraction_on(steps[i], p)) {
						return std::make_pair(i, *f);
					}
				}
				return std::nullopt;
			};
			const auto ph_on_pl = locate(planned, a0, a1, physical[b1 - 1].end);
			const auto pl_on_ph = ph_on_pl ? std::nullopt : locate(physical, b0, b1, planned[a1 - 1].end);
			const size_t m0 = m.steps.size();
			std::vector<std::pair<const TraceStep *, bool>> epl, eph;
			if (ph_on_pl) {
				// the physical walk's end first: the common part is its segment
				const auto [j, f] = *ph_on_pl;
				for (size_t i = b0; i < b1; i++) {
					m.steps.push_back(typed_ph_aligned(physical[i], i));
					eph.emplace_back(&physical[i], true);
				}
				for (size_t i = a0; i < j; i++) {
					epl.emplace_back(&planned[i], true);
				}
				if (f >= 1.0) {
					epl.emplace_back(&planned[j], true);
					from_pl = j + 1;
				} else if (f > 0.0) {
					auto [before, after] = split_step(planned[j], physical[b1 - 1].end);
					(void)before;
					rest_pl.push_back(after);
					rest_pl_idx.push_back(j);
					from_pl = j + 1;
				} else {
					from_pl = j;
				}
				from_ph = b1;
			} else if (pl_on_ph) {
				// the planned walk's end first: the common part is its segment
				const auto [j, f] = *pl_on_ph;
				for (size_t i = a0; i < a1; i++) {
					m.steps.push_back(typed_pl(planned[i], i, true));
					epl.emplace_back(&planned[i], true);
				}
				for (size_t i = b0; i < j; i++) {
					eph.emplace_back(&physical[i], true);
				}
				if (f >= 1.0) {
					eph.emplace_back(&physical[j], true);
					from_ph = j + 1;
				} else if (f > 0.0) {
					auto [before, after] = split_step(physical[j], planned[a1 - 1].end);
					(void)before;
					rest_ph.push_back(after);
					from_ph = j + 1;
				} else {
					from_ph = j;
				}
				from_pl = a1;
			}
			run_dots(m0, epl, eph);
		}
	}
	for (size_t i = from_pl; i < planned.size(); i++) {
		rest_pl.push_back(planned[i]);
		rest_pl_idx.push_back(i);
	}
	for (size_t i = from_ph; i < physical.size(); i++) {
		rest_ph.push_back(physical[i]);
	}
	// past the divergence each walk on its own (interleaved, as the steps were)
	const size_t n = std::max(rest_pl.size(), rest_ph.size());
	for (size_t i = 0; i < n; i++) {
		if (i < rest_pl.size()) {
			m.steps.push_back(typed_pl(rest_pl[i], rest_pl_idx[i], false));
			const TraceStep &st = rest_pl[i];
			if (!st.significant && st.start != st.end) {
				m.dots.push_back(EventDot{st.end, int(m.steps.size()) - 1, 1});
			}
		}
		if (i < rest_ph.size()) {
			m.steps.push_back(rest_ph[i].with_type(StepType::DivergedPhysical));
			const TraceStep &st = rest_ph[i];
			if (!st.significant && st.start != st.end) {
				m.dots.push_back(EventDot{st.end, int(m.steps.size()) - 1, 2});
			}
		}
	}
	return m;
}

// StepTreeMerge.merge (Stage 134: by segments, merge_segments; the steps alone)
inline std::vector<TraceStep> merge(const std::vector<TraceStep> &planned, const std::vector<TraceStep> &physical, int cursor_index, int physical_cursor = -1) {
	return merge_segments(planned, physical, cursor_index, physical_cursor >= 0 ? physical_cursor : int(physical.size())).steps;
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
		typed = merge(disp_planned.steps, disp_physical.steps, ci, disp_physical.cursor_index);
		in.physical = &disp_physical;
		in.planned = &disp_planned;
		in.typed = &typed;
		planned_events = inv::ev_infos(pair->result.planned);
		physical_events = inv::ev_infos(pair->result.physical);
		in.planned_events = &planned_events;
		in.physical_events = &physical_events;
		in.cursor_point_agrees = pair->cursor_point_agrees;
	}
	if (pin_only) {
		return visibility == nullptr || player == cursor || !pair ? inv::Violations{}
			: inv::check_VISIBILITY_EQUALS_NON_DIVERGENCE(*visibility, cursor, &disp_planned, &typed, pair->cursor_point_agrees);
	}
	in.visibility = visibility;
	return inv::check_all(in);
}

// Every combo of positions x positions under every plan; shard_n > 1 keeps the
// player indices with index % shard_n == shard_i (SweepRunner.in_shard).
// Stage 110: cursors on the planned surfaces' carriers (lines, and a portal's
// pair) beyond their segments -- where a walk departs from a planned surface
// crossed off its segment (B4) -- at S + k (E - S), k in {-1, -1/2, -1/8, 9/8,
// 3/2, 2}, kept when exact in float32 (a cursor is a float32 point) and inside
// the bounds grown by 50. Ordinary sweep cursors almost never land there.
inline std::vector<V2> carrier_extension_cursors(const std::vector<SurfaceDef> &defs, const std::vector<PlanItem> &plan, const Rect2f &bounds) {
	std::vector<int> ids;
	for (const PlanItem &item : plan) {
		if (item.is_infinity() || item.is_origin()) {
			continue;
		}
		ids.push_back(item.surface_id);
		for (const SurfaceDef &d : defs) {
			if (d.id != item.surface_id) {
				continue;
			}
			for (const std::optional<SideDef> &side : {d.left, d.right}) {
				if (side && side->pair >= 0) {
					ids.push_back(side->pair);
				}
			}
		}
	}
	std::vector<V2> out;
	const Rect2f g = bounds.grow(50.0f);
	for (int id : ids) {
		for (const SurfaceDef &d : defs) {
			if (d.id != id || !d.line || d.full) {
				continue;
			}
			for (const double k : {-1.0, -0.5, -0.125, 1.125, 1.5, 2.0}) {
				const double x = d.sx + k * (d.ex - d.sx);
				const double y = d.sy + k * (d.ey - d.sy);
				const V2 c{float(x), float(y)};
				if (double(c.x) == x && double(c.y) == y && g.has_point(c) && std::find(out.begin(), out.end(), c) == out.end()) {
					out.push_back(c);
				}
			}
		}
	}
	return out;
}

inline std::vector<Combo> run(const std::vector<SurfaceDef> &defs, const std::vector<inv::SurfaceInfo> &surfaces, const std::vector<V2> &positions,
		const std::vector<std::vector<PlanItem>> &plans, const Flags &flags, int shard_i, int shard_n, bool skip_equal, bool only_failures,
		bool emit_infinity = false, bool visibility = false, bool carrier_cursors = false) {
	std::vector<Combo> out;
	Tracer tracer;
	tracer.set_emit_infinity(emit_infinity);
	tracer.set_defs(defs);
	Scene<F64> scene = Scene<F64>::from_defs(defs, F64{1.0});
	scene.emit_infinity = emit_infinity;
	const Rect2f bounds = Rect2f::of(0, 0, 1920, 1080);
	for (size_t pli = 0; pli < plans.size(); pli++) {
		std::vector<V2> cursors = positions;
		if (carrier_cursors) {
			for (const V2 &c : carrier_extension_cursors(defs, plans[pli], bounds)) {
				if (std::find(cursors.begin(), cursors.end(), c) == cursors.end()) {
					cursors.push_back(c);
				}
			}
		}
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
			for (const V2 &cursor : cursors) {
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
