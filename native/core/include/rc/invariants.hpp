// The invariant checker's kernel (Stage 98): the port of
// scripts/testing/invariant_checker.gd's checks as functions of trace data
// (display paths, merged typed steps, the ladder's events, the scene's
// surfaces) instead of a live PathRenderer. A violation is a GDScript format
// string and its arguments; the binding renders it with Godot's own `%`
// formatting, so the messages equal the GDScript checker's. float32 Vector2
// semantics as in rc/visual.hpp.
#pragma once

#include "rc/beam_effects.hpp"
#include "rc/beam_geom.hpp"
#include "rc/exact.hpp"
#include "rc/ladder.hpp"
#include "rc/scene.hpp"
#include "rc/solver.hpp"
#include "rc/trace_types.hpp"
#include "rc/tracer.hpp"
#include "rc/visual.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rc {
namespace inv {

// ---------------------------------------------------------------- violations

using PairList = std::vector<std::pair<int, bool>>; // [[surface_id, left], ...]
using Arg = std::variant<int64_t, double, V2, std::string, bool, PairList>;

struct Violation {
	std::string fmt;       // the GDScript format string ("%s", "%d", "%.3f")
	std::vector<Arg> args; // its arguments, in order
};
using Violations = std::vector<Violation>;

namespace detail {
inline Violation v(std::string fmt) { return Violation{std::move(fmt), {}}; }
template <class... A>
Violation v(std::string fmt, A &&...a) {
	return Violation{std::move(fmt), {Arg(std::forward<A>(a))...}};
}
inline Arg I(int64_t x) { return Arg(x); }
inline Arg F(double x) { return Arg(x); }
inline Arg S(std::string x) { return Arg(std::move(x)); }
} // namespace detail

// ---------------------------------------------------------------- inputs

// A game surface as the checker sees it: its segment's points, carrier and
// whether a trace may not cross it (a transformative side or player_solid).
struct SurfaceInfo {
	int id = 0;
	V2 s;
	V2 e;
	V2 v;
	bool full = false;
	arc::GeneralizedCircle carrier;
	bool has_effect = false;
	bool line() const { return carrier.is_line(); }
};


// A beam-solver event at any tier, as the GDScript checker reads it (float64 values).
struct EvInfo {
	EventKind kind = EventKind::Hit;
	int surface_id = -1;
	bool left = false, applied = false, terminal = false, popped = false, projective = false, on_segment = false;
	bool has_point = false;
	double fx = 0, fy = 0;
	double margin = INF_D;
	bool has_t_from = false, has_t_to = false;
	double t_from = 0, t_to = 0;
	int beam_view = -1;
	const void *beam_ptr = nullptr;
	bool has_beam = false;
	double px = 0, py = 0, tx = 0, ty = 0, kappa = 0;
	bool beam_identity_equal(const EvInfo &o) const {
		return beam_view >= 0 || o.beam_view >= 0 ? beam_view == o.beam_view : beam_ptr == o.beam_ptr;
	}
};

template <Num N>
EvInfo ev_info(const Event<N> &e) {
	EvInfo i;
	i.kind = e.kind;
	i.surface_id = e.surface_id;
	i.left = e.left;
	i.applied = e.applied;
	i.terminal = e.terminal;
	i.popped = e.popped;
	i.projective = e.projective;
	i.on_segment = e.on_segment;
	if (e.point) {
		i.has_point = true;
		i.fx = e.point->fx();
		i.fy = e.point->fy();
	}
	i.margin = e.margin;
	if (e.t_from) {
		i.has_t_from = true;
		i.t_from = e.t_from->to_float();
	}
	if (e.t_to) {
		i.has_t_to = true;
		i.t_to = e.t_to->to_float();
	}
	i.beam_view = e.beam_view;
	i.beam_ptr = e.beam.get();
	if (e.beam) {
		i.has_beam = true;
		i.px = e.beam->p.fx();
		i.py = e.beam->p.fy();
		i.tx = e.beam->tau.fx();
		i.ty = e.beam->tau.fy();
		i.kappa = e.beam->kappa.to_float();
	}
	return i;
}

inline std::vector<EvInfo> ev_infos(const Climb &c) {
	std::vector<EvInfo> out;
	if (c.limbs == 0) {
		for (const auto &e : c.f64_events) {
			out.push_back(ev_info(e));
		}
	} else {
		for (const auto &e : c.big_events) {
			out.push_back(ev_info(e));
		}
	}
	return out;
}

// Everything check_all reads from the renderer and the scene.
struct Input {
	V2 player;
	V2 cursor;
	bool has_renderer = true;                 // the scene has a PathRenderer
	const TracePath *physical = nullptr;      // get_traced_path() (display path), null when none
	const TracePath *planned = nullptr;       // get_planned_path()
	const std::vector<TraceStep> *typed = nullptr; // get_typed_steps() (merged); null = []
	// last_beam_result's planned / physical events (ev_infos of its climbs); null when none
	const std::vector<EvInfo> *planned_events = nullptr;
	const std::vector<EvInfo> *physical_events = nullptr;
	bool has_result() const { return planned_events != nullptr && physical_events != nullptr; }
	const std::vector<SurfaceInfo> *surfaces = nullptr; // the parent's surfaces; null = []
	const Scene<F64> *scene = nullptr;        // BeamScene.from_defs(f64, defs) for the event checks
	const std::vector<SurfaceDef> *defs = nullptr; // for DIRECTION-ONLY's reference ladder
	Flags flags;
	std::vector<PlanItem> plan;
	bool plan_applied = false;                // a plan was set on the renderer
	Rect2f bounds = Rect2f::of(0, 0, 1920, 1080);
};

inline Beam<F64> f64_beam(const EvInfo &e) {
	return Beam<F64>{{F64{e.px}, F64{e.py}}, {F64{e.tx}, F64{e.ty}}, F64{e.kappa}};
}

// ---------------------------------------------------------------- helpers (the GDScript statics)

// InvariantChecker._is_infinity_gap
inline bool is_infinity_gap(const V2 &prev_end, const V2 &curr_start) {
	const double huge = 10000.0;
	const bool prev_huge = std::fabs(double(prev_end.x)) > huge || std::fabs(double(prev_end.y)) > huge;
	const bool curr_huge = std::fabs(double(curr_start.x)) > huge || std::fabs(double(curr_start.y)) > huge;
	if (prev_huge || curr_huge) {
		return true;
	}
	const double EDGE_TOL = 2.0;
	const double MIN_GAP = 500.0;
	const double gap = prev_end.distance_to(curr_start);
	if (gap < MIN_GAP) {
		return false;
	}
	auto at_edge = [&](const V2 &p) {
		return std::fabs(double(p.x)) < EDGE_TOL || std::fabs(double(p.x) - 1920.0) < EDGE_TOL || std::fabs(double(p.y)) < EDGE_TOL ||
			std::fabs(double(p.y) - 1080.0) < EDGE_TOL;
	};
	return at_edge(prev_end) || at_edge(curr_start);
}

// InvariantChecker._is_at_bounds
inline bool is_at_bounds(const V2 &p, const Rect2f &bounds) {
	const V2 end = bounds.end();
	return double(p.x) <= double(bounds.position.x) + 2.0 || double(p.x) >= double(end.x) - 2.0 || double(p.y) <= double(bounds.position.y) + 2.0 ||
		double(p.y) >= double(end.y) - 2.0;
}

// InvariantChecker._geometric_carrier_dist
inline double geometric_carrier_dist(const V2 &point, const arc::GeneralizedCircle &g) {
	const double f = g.evaluate(point.x, point.y);
	const double gx = 2.0 * g.a * double(point.x) + g.b;
	const double gy = 2.0 * g.a * double(point.y) + g.c;
	const double grad = std::sqrt(gx * gx + gy * gy);
	if (grad < 1e-10) {
		return INF_D;
	}
	return std::fabs(f) / grad;
}

// InvariantChecker._point_to_segment_dist
inline double point_to_segment_dist(const V2 &p, const V2 &a, const V2 &b) {
	const V2 ab = b - a;
	const double len_sq = ab.length_squared();
	if (len_sq == 0.0) {
		return p.distance_to(a);
	}
	const double t = std::clamp(double((p - a).dot(ab)) / len_sq, 0.0, 1.0);
	return p.distance_to(a + float(t) * ab);
}

// InvariantChecker._segment_intersection: the crossing strictly inside both segments
inline std::optional<V2> segment_intersection(const V2 &a1, const V2 &a2, const V2 &b1, const V2 &b2) {
	const V2 d1 = a2 - a1;
	const V2 d2 = b2 - b1;
	const double cross = d1.cross(d2);
	if (std::fabs(cross) < 1e-10) {
		return std::nullopt;
	}
	const V2 d = b1 - a1;
	const double t = double(d.cross(d2)) / cross;
	const double u = double(d.cross(d1)) / cross;
	if (t > 0.01 && t < 0.99 && u > 0.01 && u < 0.99) {
		return a1 + d1 * float(t);
	}
	return std::nullopt;
}

struct GeomSegment {
	V2 start;
	V2 end;
};

// InvariantChecker._segments_collinear
inline bool segments_collinear(const GeomSegment &a, const GeomSegment &b, double tol = 1.0) {
	if (double(a.end.distance_to(b.start)) > tol) {
		return false;
	}
	const V2 dir_a = a.end - a.start;
	const V2 dir_b = b.end - b.start;
	if (double(dir_a.length_squared()) < 0.001 || double(dir_b.length_squared()) < 0.001) {
		return true;
	}
	const double cross = dir_a.normalized().cross(dir_b.normalized());
	const double dot = dir_a.normalized().dot(dir_b.normalized());
	return std::fabs(cross) < 0.01 && dot > 0.0;
}

inline std::vector<GeomSegment> merge_collinear_stages(const std::vector<GeomSegment> &segments) {
	if (segments.size() <= 1) {
		return segments;
	}
	std::vector<GeomSegment> merged{segments[0]};
	for (size_t i = 1; i < segments.size(); i++) {
		if (segments_collinear(merged.back(), segments[i])) {
			merged.back().end = segments[i].end;
		} else {
			merged.push_back(segments[i]);
		}
	}
	return merged;
}

// InvariantChecker._extract_trace_geometry: one segment per frame, collinear stages merged
inline std::vector<GeomSegment> extract_trace_geometry(const TracePath *path) {
	std::vector<GeomSegment> segments;
	if (path == nullptr || path->steps.empty()) {
		return segments;
	}
	V2 current_start = path->steps[0].start;
	V2 current_end = path->steps[0].end;
	int current_fid = path->steps[0].frame_id;
	for (size_t i = 1; i < path->steps.size(); i++) {
		const TraceStep &step = path->steps[i];
		if (step.frame_id == current_fid) {
			current_end = step.end;
		} else {
			segments.push_back(GeomSegment{current_start, current_end});
			current_start = step.start;
			current_end = step.end;
			current_fid = step.frame_id;
		}
	}
	segments.push_back(GeomSegment{current_start, current_end});
	return merge_collinear_stages(segments);
}

inline double tol_of(double x, double y) { return 1e-6 * gd_maxf(1.0, V2::of(x, y).length()); }

inline bool is_aligned_type(StepType t) { return t == StepType::Aligned || t == StepType::AlignedPostPlanned; }
inline bool is_solid_type(StepType t) { return t == StepType::Aligned || t == StepType::DivergedPlanned; }

inline const SurfaceInfo *surface_by_id(const std::vector<SurfaceInfo> *surfaces, int id) {
	if (surfaces == nullptr || id < 0) {
		return nullptr;
	}
	for (const SurfaceInfo &s : *surfaces) {
		if (s.id == id) {
			return &s;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------- the checks

using namespace detail;

inline Violations check_UX7(const Input &in) {
	Violations out;
	if (!in.has_renderer) {
		return out;
	}
	const bool has_line = in.typed != nullptr && !in.typed->empty();
	if (in.player != in.cursor) {
		if (!has_line) {
			out.push_back(v("UX7: No line when cursor != player"));
		} else {
			const V2 line_from = (*in.typed)[0].start;
			if (line_from != in.player) {
				out.push_back(v("UX7: Line start %s != player %s", line_from, in.player));
			}
			const bool has_plan = !in.plan.empty();
			if (!has_plan) {
				const V2 expected_dir = (in.cursor - in.player).normalized();
				const V2 actual_dir = ((*in.typed)[0].end - (*in.typed)[0].start).normalized();
				if (double(expected_dir.dot(actual_dir)) < 0.99) {
					out.push_back(v("UX7: Direction %s not toward cursor %s", actual_dir, expected_dir));
				}
			}
		}
	} else if (has_line) {
		out.push_back(v("UX7: Line present when cursor == player"));
	}
	return out;
}

inline Violations check_PREVIEW_NOGAPS(const Input &in) {
	Violations out;
	if (!in.has_renderer || in.player == in.cursor || in.physical == nullptr) {
		return out;
	}
	const auto &steps = in.physical->steps;
	for (size_t i = 1; i < steps.size(); i++) {
		const TraceStep &prev = steps[i - 1];
		const TraceStep &curr = steps[i];
		if (!prev.hit || !curr.hit) {
			continue;
		}
		const double gap = prev.end.distance_to(curr.start);
		const double tol = 1.5 + 0.001 * double(i);
		if (gap > tol) {
			if (is_infinity_gap(prev.end, curr.start) || curr.after_portal) {
				continue;
			}
			out.push_back(v("NOGAPS: Gap between step %d end=%s and step %d start=%s", I(int64_t(i) - 1), prev.end, I(int64_t(i)), curr.start));
		}
	}
	return out;
}

inline Violations check_S16(const Input &in) {
	Violations out;
	if (!in.has_renderer || in.player == in.cursor || in.physical == nullptr) {
		return out;
	}
	const auto &steps = in.physical->steps;
	for (size_t i = 0; i < steps.size(); i++) {
		if (std::isnan(steps[i].start.x) || std::isnan(steps[i].start.y)) {
			out.push_back(v("S16: NaN in step %d start", I(int64_t(i))));
		}
		if (std::isnan(steps[i].end.x) || std::isnan(steps[i].end.y)) {
			out.push_back(v("S16: NaN in step %d end", I(int64_t(i))));
		}
	}
	return out;
}

inline Violations check_GREEN_FROM_PLAYER(const Input &in) {
	Violations out;
	if (!in.has_renderer || in.player == in.cursor || in.typed == nullptr || in.typed->empty()) {
		return out;
	}
	const TraceStep &first = (*in.typed)[0];
	if (!is_aligned_type(first.type)) {
		out.push_back(v("GREEN-FROM-PLAYER: First step type=%d, expected green", I(int(first.type))));
	}
	return out;
}

inline Violations check_ORIGIN_NOT_REHIT(const Input &in) {
	Violations out;
	if (!in.has_renderer || in.player == in.cursor || in.physical == nullptr) {
		return out;
	}
	const auto &steps = in.physical->steps;
	for (size_t i = 0; i < steps.size(); i++) {
		const TraceStep &step = steps[i];
		if (step.start == step.end && step.hit && step.hit->t > 0.0) {
			out.push_back(v("ORIGIN-NOT-REHIT: Zero-length step %d at %s", I(int64_t(i)), step.start));
		}
	}
	return out;
}

inline Violations check_SINGLE_DIVERGENCE(const Input &in) {
	Violations out;
	if (!in.has_renderer || in.player == in.cursor || in.typed == nullptr) {
		return out;
	}
	bool diverged = false;
	for (size_t i = 0; i < in.typed->size(); i++) {
		const bool aligned = is_aligned_type((*in.typed)[i].type);
		if (!aligned) {
			diverged = true;
		} else if (diverged) {
			out.push_back(v("SINGLE-DIVERGENCE: Re-convergence at step %d after divergence", I(int64_t(i))));
		}
	}
	return out;
}

inline Violations check_NO_POST_CURSOR_DIVERGENCE(const Input &in) {
	Violations out;
	if (!in.has_renderer || in.player == in.cursor || in.typed == nullptr || in.typed->empty()) {
		return out;
	}
	bool all_pre_cursor_aligned = true;
	bool past_cursor = false;
	for (size_t i = 0; i < in.typed->size(); i++) {
		const StepType t = (*in.typed)[i].type;
		if (t == StepType::Aligned) {
			continue;
		}
		if (t == StepType::AlignedPostPlanned) {
			past_cursor = true;
			continue;
		}
		if (!past_cursor) {
			all_pre_cursor_aligned = false;
			break;
		}
		if (all_pre_cursor_aligned) {
			out.push_back(v("NO-POST-CURSOR-DIVERGENCE: step %d type=%d diverges after fully-aligned path to cursor", I(int64_t(i)), I(int(t))));
			break;
		}
	}
	return out;
}

inline Violations check_PHYSICAL_PREVIEW_MATCH(const Input &in) {
	Violations out;
	if (!in.has_renderer || in.player == in.cursor || in.typed == nullptr || in.typed->empty() || in.physical == nullptr) {
		return out;
	}
	std::vector<const TraceStep *> non_red;
	for (const TraceStep &ms : *in.typed) {
		if (ms.type == StepType::Aligned || ms.type == StepType::AlignedPostPlanned || ms.type == StepType::DivergedPhysical) {
			non_red.push_back(&ms);
		}
	}
	if (non_red.size() != in.physical->steps.size()) {
		out.push_back(v("PHYSICAL-PREVIEW-MATCH: non-red count=%d != physical count=%d", I(int64_t(non_red.size())), I(int64_t(in.physical->steps.size()))));
		return out;
	}
	bool portal_seen = false;
	for (size_t i = 0; i < non_red.size(); i++) {
		const TraceStep &ms = *non_red[i];
		const TraceStep &ps = in.physical->steps[i];
		if (ps.after_portal || ms.after_portal) {
			portal_seen = true;
		}
		if (portal_seen) {
			continue;
		}
		if (double(ms.start.distance_to(ps.start)) > 0.01) {
			out.push_back(v("PHYSICAL-PREVIEW-MATCH: step %d start mismatch: preview=%s physical=%s", I(int64_t(i)), ms.start, ps.start));
		}
		if (double(ms.end.distance_to(ps.end)) > 0.01) {
			out.push_back(v("PHYSICAL-PREVIEW-MATCH: step %d end mismatch: preview=%s physical=%s", I(int64_t(i)), ms.end, ps.end));
		}
	}
	return out;
}

inline Violations check_PHYSICAL_CONTINUITY(const Input &in) {
	Violations out;
	if (!in.has_renderer || in.player == in.cursor || in.physical == nullptr || in.physical->steps.size() < 2) {
		return out;
	}
	const auto &steps = in.physical->steps;
	for (size_t i = 1; i < steps.size(); i++) {
		const TraceStep &prev = steps[i - 1];
		const TraceStep &curr = steps[i];
		if (!prev.hit) {
			continue;
		}
		const double gap = prev.end.distance_to(curr.start);
		const double tol = 1.5 + 0.001 * double(i);
		if (gap > tol) {
			if (is_infinity_gap(prev.end, curr.start) || curr.after_portal) {
				continue;
			}
			out.push_back(v("PHYSICAL-CONTINUITY: gap between step %d end=%s and step %d start=%s", I(int64_t(i) - 1), prev.end, I(int64_t(i)), curr.start));
		}
	}
	return out;
}

inline Violations check_SOLID_PATH_TO_CURSOR(const Input &in) {
	Violations out;
	if (!in.has_renderer || in.player == in.cursor || in.typed == nullptr || in.typed->empty()) {
		return out;
	}
	std::vector<const TraceStep *> solid;
	for (const TraceStep &ms : *in.typed) {
		if (is_solid_type(ms.type)) {
			solid.push_back(&ms);
		}
	}
	if (solid.empty()) {
		return out;
	}
	if (solid[0]->start != in.player) {
		out.push_back(v("SOLID-PATH-TO-CURSOR: first solid step starts at %s, not player %s", solid[0]->start, in.player));
	}
	const Rect2f bounds = Rect2f::of(0, 0, 1920, 1080);
	const V2 bend = bounds.end();
	for (size_t i = 1; i < solid.size(); i++) {
		const TraceStep &prev = *solid[i - 1];
		const TraceStep &curr = *solid[i];
		if (double(prev.end.distance_to(curr.start)) > 0.05 && !curr.after_portal) {
			const bool at_bounds = double(prev.end.x) <= double(bounds.position.x) + 1.0 || double(prev.end.x) >= double(bend.x) - 1.0 ||
				double(prev.end.y) <= double(bounds.position.y) + 1.0 || double(prev.end.y) >= double(bend.y) - 1.0;
			if (!at_bounds) {
				out.push_back(v("SOLID-PATH-TO-CURSOR: gap between solid step %d end=%s and step %d start=%s", I(int64_t(i) - 1), prev.end, I(int64_t(i)), curr.start));
			}
		}
	}
	return out;
}

// the named display traces, in the GDScript Dictionary's order (physical, planned)
inline std::vector<std::pair<std::string, const TracePath *>> named_traces(const Input &in) {
	std::vector<std::pair<std::string, const TracePath *>> out;
	if (in.has_renderer) {
		if (in.physical != nullptr) {
			out.emplace_back("physical", in.physical);
		}
		if (in.planned != nullptr) {
			out.emplace_back("planned", in.planned);
		}
	}
	return out;
}

// the events of the trace the name refers to (the planned ones for a name containing "plan")
inline const std::vector<EvInfo> &events_of(const Input &in, const std::string &trace_name) {
	const bool plan = trace_name.find("plan") != std::string::npos; // trace_name.to_lower().contains("plan")
	return plan ? *in.planned_events : *in.physical_events;
}

inline bool cut_by_max_hits(const Input &in, const std::string &trace_name) {
	if (!in.has_result()) {
		return false;
	}
	const std::vector<EvInfo> &ev = events_of(in, trace_name);
	return !ev.empty() && ev.back().kind == EventKind::Max;
}

inline Violations check_TRACE_ENDS_AT_SURFACE_OR_BOUNDS(const Input &in) {
	Violations out;
	if (!in.has_renderer || in.player == in.cursor) {
		return out;
	}
	const Rect2f bounds = Rect2f::of(0, 0, 1920, 1080);
	const V2 bend = bounds.end();
	for (const auto &[name, path] : named_traces(in)) {
		if (path->steps.empty()) {
			continue;
		}
		const V2 end_pos = path->steps.back().end;
		bool valid = cut_by_max_hits(in, name);
		if (double(end_pos.distance_to(in.player)) < 2.0) {
			valid = true;
		}
		if (double(end_pos.x) <= double(bounds.position.x) + 2.0 || double(end_pos.x) >= double(bend.x) - 2.0) {
			valid = true;
		}
		if (double(end_pos.y) <= double(bounds.position.y) + 2.0 || double(end_pos.y) >= double(bend.y) - 2.0) {
			valid = true;
		}
		if (in.surfaces != nullptr) {
			for (const SurfaceInfo &s : *in.surfaces) {
				if (point_to_segment_dist(end_pos, s.s, s.e) < 2.0) {
					valid = true;
					break;
				}
			}
			if (!valid) {
				for (const SurfaceInfo &s : *in.surfaces) {
					const arc::GeneralizedCircle &g = s.carrier;
					const double eval_val = g.evaluate(end_pos.x, end_pos.y);
					// B4: the GDScript formula (not the carrier's gradient at the point); reproduced
					const double grad_mag = std::sqrt(g.b * g.b + g.c * g.c + 4.0 * g.a * g.a * (double(end_pos.x) * double(end_pos.x) + double(end_pos.y) * double(end_pos.y)));
					const double dist_to_carrier = std::fabs(eval_val) / gd_maxf(grad_mag, 1e-10);
					if (dist_to_carrier < 2.0) {
						valid = true;
						break;
					}
				}
			}
		}
		if (!valid) {
			out.push_back(v("TRACE-ENDS: %s trace ends mid-air at %s", S(name), end_pos));
		}
	}
	return out;
}

inline Violations check_plan_effects_applied_events(const std::vector<EvInfo> &events, const std::vector<PlanItem> &plan) {
	Violations out;
	PairList applied;
	for (const EvInfo &e : events) {
		if (e.kind == EventKind::Cursor) {
			break;
		}
		if (e.kind == EventKind::Hit && e.applied && !e.terminal && !e.projective) {
			applied.emplace_back(e.surface_id, e.left);
		}
	}
	PairList want;
	for (const PlanItem &item : plan) {
		want.emplace_back(item.surface_id, item.left);
	}
	if (applied != want) {
		out.push_back(v("PLAN-EFFECTS: applied %s, plan %s", applied, want));
	}
	return out;
}

inline Violations check_PLAN_EFFECTS_APPLIED(const Input &in) {
	if (!in.has_result() || in.player == in.cursor || in.plan.empty() || in.scene == nullptr) {
		return {};
	}
	const std::vector<EvInfo> &planned = *in.planned_events;
	bool reached = false;
	for (const EvInfo &e : planned) {
		if (e.kind == EventKind::Cursor) {
			reached = true;
		}
	}
	if (!reached) {
		return {};
	}
	const std::vector<EffectType> effs = Solver<F64>(*in.scene).plan_effects(in.plan, in.flags);
	std::vector<PlanItem> items;
	for (size_t j = 0; j < in.plan.size(); j++) {
		if (is_transformative(effs[j])) {
			items.push_back(in.plan[j]);
		}
	}
	return check_plan_effects_applied_events(planned, items);
}

inline Violations check_PHYSICS_COMPLIANCE(const Input &in) {
	Violations out;
	if (!in.has_renderer || in.player == in.cursor || in.physical == nullptr || in.physical->steps.empty() || in.surfaces == nullptr || in.surfaces->empty()) {
		return out;
	}
	const auto &steps = in.physical->steps;
	size_t check_limit = in.physical->cursor_index >= 0 ? size_t(in.physical->cursor_index) : steps.size();
	check_limit = std::min(check_limit, steps.size());
	bool portal_seen = false;
	for (size_t i = 0; i < check_limit; i++) {
		const TraceStep &step = steps[i];
		if (step.after_portal) {
			portal_seen = true;
		}
		if (portal_seen || step.is_arc_step || step.start == step.end || step.frame_id != IDENTITY_FRAME) {
			continue;
		}
		for (const SurfaceInfo &s : *in.surfaces) {
			if (!s.has_effect || !s.line()) {
				continue;
			}
			const std::optional<V2> ix = segment_intersection(step.start, step.end, s.s, s.e);
			if (!ix) {
				continue;
			}
			const V2 pt = *ix;
			if (double(pt.distance_to(step.start)) < 1.0 || double(pt.distance_to(step.end)) < 1.0) {
				continue;
			}
			if (double(pt.distance_to(s.s)) < 1.0 || double(pt.distance_to(s.e)) < 1.0) {
				continue;
			}
			out.push_back(v("PHYSICS-COMPLIANCE: step %d crosses surface %d at %s", I(int64_t(i)), I(s.id), pt));
		}
	}
	return out;
}

inline Violations carrier_violations(const std::vector<EvInfo> &events, const Scene<F64> &scene) {
	Violations out;
	for (size_t i = 0; i < events.size(); i++) {
		const EvInfo &e = events[i];
		if (e.kind != EventKind::Hit || !e.has_point) {
			continue;
		}
		const Surf<F64> *s = scene.find(e.surface_id);
		if (s == nullptr) {
			continue;
		}
		const double x = e.fx;
		const double y = e.fy;
		double d;
		if (s->line) {
			const double dx = s->ex - s->sx;
			const double dy = s->ey - s->sy;
			d = std::fabs(dx * (y - s->sy) - dy * (x - s->sx)) / std::sqrt(dx * dx + dy * dy);
		} else {
			d = std::fabs(double(V2::of(x - s->cx, y - s->cy).length()) - s->r);
		}
		if (d > tol_of(x, y)) {
			out.push_back(v("HITPOINT-ON-CARRIER: event %d s=%d off its carrier by %s", I(int64_t(i)), I(e.surface_id), F(d)));
		}
	}
	return out;
}

inline Violations check_HITPOINT_ON_CARRIER(const Input &in) {
	if (!in.has_result() || in.player == in.cursor || in.scene == nullptr) {
		return {};
	}
	Violations out = carrier_violations(*in.planned_events, *in.scene);
	const Violations ph = carrier_violations(*in.physical_events, *in.scene);
	out.insert(out.end(), ph.begin(), ph.end());
	return out;
}

inline Violations check_ON_SEGMENT_CONSISTENCY(const Input &in) {
	Violations out;
	if (!in.has_result() || in.player == in.cursor || in.scene == nullptr) {
		return out;
	}
	for (const std::vector<EvInfo> *evp : {in.planned_events, in.physical_events}) {
		const std::vector<EvInfo> &events = *evp;
		for (size_t i = 0; i < events.size(); i++) {
			const EvInfo &e = events[i];
			if (e.kind != EventKind::Hit || !e.has_point) {
				continue;
			}
			const Surf<F64> *s = in.scene->find(e.surface_id);
			if (s == nullptr) {
				continue;
			}
			const CN<F64> p{F64{e.fx}, F64{e.fy}};
			if (geom::on_segment(s->seg, p) == e.on_segment) {
				continue;
			}
			const double near = gd_minf(V2::of(e.fx - s->sx, e.fy - s->sy).length(), V2::of(e.fx - s->ex, e.fy - s->ey).length());
			if (near > 1e-9 * gd_maxf(1.0, V2::of(e.fx, e.fy).length())) {
				out.push_back(v("ON-SEGMENT-CONSISTENCY: event %d s=%d stored=%s recomputed=%s at (%s, %s)", I(int64_t(i)), I(e.surface_id), e.on_segment,
					!e.on_segment, F(e.fx), F(e.fy)));
			}
		}
	}
	return out;
}

inline Violations check_VIA_ON_ARC(const Input &in) {
	Violations out;
	if (!in.has_renderer || in.player == in.cursor) {
		return out;
	}
	for (const auto &[name, path] : named_traces(in)) {
		for (size_t i = 0; i < path->steps.size(); i++) {
			const TraceStep &step = path->steps[i];
			if (!step.is_arc_step || !arc::is_arc(step.start, step.via, step.end)) {
				continue;
			}
			const arc::ArcParams p = arc::arc_params(step.start, step.via, step.end);
			const double via_angle = (step.via - p.center).angle();
			const double diff = p.clockwise ? fposmod(p.start_angle - via_angle, arc::TAU) : fposmod(via_angle - p.start_angle, arc::TAU);
			if (diff > p.span + 0.01) {
				out.push_back(v("VIA_ON_ARC: %s step %d via not on drawn arc (diff=%.3f, span=%.3f)", S(name), I(int64_t(i)), F(diff), F(p.span)));
			}
		}
	}
	return out;
}

inline Violations check_VISUAL_ON_PHYSICAL_CARRIER(const Input &in) {
	Violations out;
	if (!in.has_renderer || in.player == in.cursor || in.surfaces == nullptr || in.surfaces->empty()) {
		return out;
	}
	const Rect2f bounds = Rect2f::of(0, 0, 1920, 1080);
	for (const auto &[name, path] : named_traces(in)) {
		for (size_t i = 0; i < path->steps.size(); i++) {
			const TraceStep &step = path->steps[i];
			if (!step.hit) {
				continue;
			}
			const SurfaceInfo *hit_surf = surface_by_id(in.surfaces, step.hit->surface_id);
			if (hit_surf == nullptr) {
				continue; // step.hit.segment == null
			}
			const V2 end_pos = step.end;
			if (end_pos.is_inf() || is_at_bounds(end_pos, bounds)) {
				continue;
			}
			bool on_carrier = false;
			for (const SurfaceInfo &s : *in.surfaces) {
				if (geometric_carrier_dist(end_pos, s.carrier) < 5.0) {
					on_carrier = true;
					break;
				}
			}
			if (!on_carrier) {
				if (!step.hit_on_segment) {
					continue;
				}
				if (step.frame_id != IDENTITY_FRAME) {
					const double norm_err = geometric_carrier_dist(step.hit->point, hit_surf->carrier);
					if (norm_err < 1.0) {
						continue;
					}
				}
				out.push_back(v("VISUAL-ON-CARRIER: %s step %d visual endpoint %s not on any physical carrier", S(name), I(int64_t(i)), end_pos));
			}
		}
	}
	return out;
}

inline Violations check_DIRECTION_ONLY(const Input &in) {
	Violations out;
	if (in.plan.empty() || !in.has_result() || in.player == in.cursor || in.scene == nullptr || in.defs == nullptr) {
		return out;
	}
	const Scene<F64> &sc = *in.scene;
	const Solver<F64> solver(sc);
	std::optional<CN<F64>> X = CN<F64>{F64{double(in.cursor.x)}, F64{double(in.cursor.y)}};
	double scale = gd_maxf(1.0, in.cursor.length());
	const std::vector<EffectType> effs = solver.plan_effects(in.plan, in.flags);
	for (int j = int(in.plan.size()) - 1; j >= 0; j--) {
		const Surf<F64> *surf = sc.find(in.plan[size_t(j)].surface_id);
		const EffectType etype = effs[size_t(j)];
		if (is_projective(etype)) {
			return out;
		}
		if (!is_transformative(etype) || surf == nullptr) {
			continue;
		}
		X = effects::map_point(X, *surf, true);
		if (!X) {
			return out;
		}
		scale = gd_maxf(scale, V2::of(X->fx(), X->fy()).length());
	}
	if (solver.fixes_cursor(CN<F64>{F64{double(in.cursor.x)}, F64{double(in.cursor.y)}}, in.plan, in.flags)) {
		X = CN<F64>{F64{double(in.cursor.x)}, F64{double(in.cursor.y)}};
	}
	const V2 img = V2::of(X->fx(), X->fy());
	if (!(std::isfinite(img.x) && std::isfinite(img.y)) || img == in.player) {
		return out;
	}
	// the reference: the physical ladder aimed at the image, with its float64 sensitivities
	LadderContext ctx(*in.defs);
	Ladder ladder(ctx);
	const LadderResult ref = ladder.solve_physical(double(in.player.x), double(in.player.y), X->fx(), X->fy(), {}, MAX_HITS, TRACE_TOL, in.flags);
	// ErrorEstimate.physical_sensitivity's events (the float64 walk) and gains
	Solver<Dual> fast(ctx.dual_scene(), &ctx.dual_cache());
	const std::optional<Walk<Dual>> wq = fast.run(CN<Dual>{Dual{double(in.player.x), 0.0}, Dual{double(in.player.y), 0.0}},
		CN<Dual>{Dual{X->fx(), 0.0}, Dual{X->fy(), 0.0}}, {}, Mode::Physical, MAX_HITS, in.flags);
	std::vector<double> hit_gain;
	if (wq) {
		const estimate::Sensitivity sens = estimate::collect(wq->events, wq->gains);
		for (size_t k = 0; k < wq->events.size(); k++) {
			if (wq->events[k].kind == EventKind::Hit) {
				hit_gain.push_back(sens.gain[k]);
			}
		}
	}
	const double delta = 64.0 * exact::U * scale / gd_maxf(img.distance_to(in.player), 1e-300);
	std::vector<EvInfo> a;
	for (const EvInfo &e : *in.physical_events) {
		if (e.kind == EventKind::Hit) {
			a.push_back(e);
		}
	}
	std::vector<EvInfo> b;
	for (const EvInfo &e : ev_infos(ref.physical)) {
		if (e.kind == EventKind::Hit) {
			b.push_back(e);
		}
	}
	const int n = int(std::min(a.size(), b.size())) - 1; // the event budget differs by the cursor
	for (int i = 0; i < std::max(n, 0); i++) {
		const double move = size_t(i) < hit_gain.size() ? hit_gain[size_t(i)] * delta : INF_D;
		if (move > 0.005) {
			break;
		}
		if (b[size_t(i)].margin <= move) {
			break;
		}
		if (a[size_t(i)].surface_id != b[size_t(i)].surface_id || a[size_t(i)].left != b[size_t(i)].left) {
			out.push_back(v("DIRECTION-ONLY: hit %d s=%d vs s=%d without plan", I(i), I(a[size_t(i)].surface_id), I(b[size_t(i)].surface_id)));
			return out;
		}
		const double d = V2::of(a[size_t(i)].fx - b[size_t(i)].fx, a[size_t(i)].fy - b[size_t(i)].fy).length();
		if (d > 0.05) {
			out.push_back(v("DIRECTION-ONLY: hit %d off by %s px", I(i), F(d)));
			return out;
		}
	}
	return out;
}

// InvariantChecker.check_effect_law
inline Violations check_effect_law(const std::vector<EvInfo> &events, const Scene<F64> &scene) {
	Violations out;
	for (size_t i = 0; i + 1 < events.size(); i++) {
		const EvInfo &e = events[i];
		if (e.kind != EventKind::Hit || !e.applied || e.terminal || e.popped || !e.has_point || !e.has_beam) {
			continue;
		}
		const Surf<F64> *s = scene.find(e.surface_id);
		if (s == nullptr) {
			continue;
		}
		const Beam<F64> b = f64_beam(e);
		const F64 t{e.t_to};
		const CN<F64> h{F64{e.fx}, F64{e.fy}};
		const Beam<F64> law = e.projective ? effects::project(b, t, *s, e.left, &h) : effects::apply(b, t, *s, &h);
		const Beam<F64> nb = f64_beam(events[i + 1]);
		const F64 u = nb.param_of(law.p);
		const CN<F64> q = nb.at(u);
		const double tol = tol_of(law.p.fx(), law.p.fy());
		const double dp = V2::of(q.fx() - law.p.fx(), q.fy() - law.p.fy()).length();
		const CN<F64> tg = nb.tangent_at(u);
		const double dt = V2::of(tg.fx() - law.tau.fx(), tg.fy() - law.tau.fy()).length();
		const double dk = std::fabs(nb.kappa.to_float() - law.kappa.to_float());
		if (dp > tol || dt > 1e-6 || dk > 1e-6 * gd_maxf(1e-3, std::fabs(law.kappa.to_float()))) {
			out.push_back(v("EFFECT-LAW: event %d s=%d: next beam off the law (point %s, tangent %s, curvature %s)", I(int64_t(i)), I(e.surface_id), F(dp), F(dt), F(dk)));
		}
	}
	return out;
}

// InvariantChecker.check_event_on_beam
inline Violations check_event_on_beam(const std::vector<EvInfo> &events, const Scene<F64> &scene) {
	Violations out;
	for (size_t i = 0; i < events.size(); i++) {
		const EvInfo &e = events[i];
		if (!e.has_point || !e.has_t_to || !e.has_beam) {
			continue;
		}
		double qx, qy;
		fast::at_view(e.px, e.py, e.tx, e.ty, e.kappa, e.t_to, qx, qy);
		const double d = V2::of(qx - e.fx, qy - e.fy).length();
		if (d > tol_of(e.fx, e.fy)) {
			out.push_back(v("EVENT-ON-BEAM: event %d (%s) off its beam by %s", I(int64_t(i)), S(event_kind_name(e.kind)), F(d)));
		}
	}
	const Violations cv = carrier_violations(events, scene);
	out.insert(out.end(), cv.begin(), cv.end());
	return out;
}

// InvariantChecker.check_beam_order
inline Violations check_beam_order(const std::vector<EvInfo> &events) {
	Violations out;
	for (size_t i = 1; i < events.size(); i++) {
		const EvInfo &a = events[i - 1];
		const EvInfo &b = events[i];
		if (!a.beam_identity_equal(b) || !a.has_t_to || !b.has_t_from) {
			continue;
		}
		if (a.t_to != b.t_from) {
			out.push_back(v("BEAM-ORDER: event %d starts at t=%s, event %d ended at t=%s", I(int64_t(i)), F(b.t_from), I(int64_t(i) - 1), F(a.t_to)));
		}
	}
	return out;
}

inline Violations check_EFFECT_LAW(const Input &in) {
	if (!in.has_result() || in.player == in.cursor || in.scene == nullptr) {
		return {};
	}
	Violations out = check_effect_law(*in.planned_events, *in.scene);
	const Violations ph = check_effect_law(*in.physical_events, *in.scene);
	out.insert(out.end(), ph.begin(), ph.end());
	return out;
}
inline Violations check_EVENT_ON_BEAM(const Input &in) {
	if (!in.has_result() || in.player == in.cursor || in.scene == nullptr) {
		return {};
	}
	Violations out = check_event_on_beam(*in.planned_events, *in.scene);
	const Violations ph = check_event_on_beam(*in.physical_events, *in.scene);
	out.insert(out.end(), ph.begin(), ph.end());
	return out;
}
inline Violations check_BEAM_ORDER(const Input &in) {
	if (!in.has_result() || in.player == in.cursor) {
		return {};
	}
	Violations out = check_beam_order(*in.planned_events);
	const Violations ph = check_beam_order(*in.physical_events);
	out.insert(out.end(), ph.begin(), ph.end());
	return out;
}

// InvariantChecker.check_all, in its order
inline Violations check_all(const Input &in) {
	Violations out;
	auto add = [&](Violations vs) { out.insert(out.end(), vs.begin(), vs.end()); };
	add(check_UX7(in));
	add(check_PREVIEW_NOGAPS(in));
	add(check_S16(in));
	add(check_GREEN_FROM_PLAYER(in));
	add(check_ORIGIN_NOT_REHIT(in));
	add(check_SINGLE_DIVERGENCE(in));
	add(check_NO_POST_CURSOR_DIVERGENCE(in));
	add(check_PHYSICAL_PREVIEW_MATCH(in));
	add(check_PHYSICAL_CONTINUITY(in));
	add(check_SOLID_PATH_TO_CURSOR(in));
	add(check_TRACE_ENDS_AT_SURFACE_OR_BOUNDS(in));
	if (in.plan_applied) {
		add(check_PLAN_EFFECTS_APPLIED(in));
	}
	add(check_PHYSICS_COMPLIANCE(in));
	add(check_HITPOINT_ON_CARRIER(in));
	add(check_ON_SEGMENT_CONSISTENCY(in));
	add(check_EFFECT_LAW(in));
	add(check_EVENT_ON_BEAM(in));
	add(check_BEAM_ORDER(in));
	add(check_VIA_ON_ARC(in));
	add(check_VISUAL_ON_PHYSICAL_CARRIER(in));
	if (in.plan_applied) {
		add(check_DIRECTION_ONLY(in));
	}
	return out;
}

} // namespace inv
} // namespace rc
