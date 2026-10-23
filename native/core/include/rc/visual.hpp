// Visual and testing kernels (Stage 97): the ports of GeneralizedCircle /
// Segment (carriers, sides, click distance), VisualConverter (display
// snapping, clipping to the bounds, arc parameters), ArrowAnimator.advance and
// SurfaceNode's sampling math. Every Vector2 operation is float32 exactly as
// Godot computes it (V2 / Rect2f); every GDScript scalar is a double, and a
// Vector2 component enters scalar arithmetic as a double (trace_types.hpp).
#pragma once

#include "rc/arc_geom.hpp"
#include "rc/num.hpp"
#include "rc/trace_types.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

namespace rc {
namespace arc {

constexpr double TAU = 6.283185307179586;

// GeneralizedCircle.center(): a Vector2 (float32) from the float64 quotients.
inline V2 center_of(const GeneralizedCircle &g) { return V2::of(g.center_x(), g.center_y()); }

inline GeneralizedCircle from_line(double b, double c, double d) { return GeneralizedCircle{0.0, b, c, d}; }
inline GeneralizedCircle from_circle(const V2 &center, double radius) {
	const double cx = center.x;
	const double cy = center.y;
	return GeneralizedCircle{1.0, -2.0 * cx, -2.0 * cy, cx * cx + cy * cy - radius * radius};
}
inline bool same_circle(const GeneralizedCircle &a, const GeneralizedCircle &b) {
	return a.a == b.a && a.b == b.b && a.c == b.c && a.d == b.d;
}

// Segment: start -> end through via (via at infinity: a full line), with its
// carrier derived on demand (Segment.get_carrier) or given (full_from_carrier).
struct SegGeom {
	V2 s;
	V2 e;
	V2 v;
	bool full = false;
	mutable std::optional<GeneralizedCircle> carrier;

	static SegGeom make(const V2 &p_s, const V2 &p_e, const V2 &p_v, bool p_full = false) {
		SegGeom g;
		g.s = p_s;
		g.e = p_e;
		g.v = p_v;
		g.full = p_full;
		return g;
	}

	const GeneralizedCircle &get_carrier() const {
		if (!carrier) {
			carrier = derive_carrier(s, e, v);
		}
		return *carrier;
	}
	bool is_line() const { return get_carrier().is_line(); }

	// Segment._compute_winding: -(end - start) x grad(via) in float32
	double winding() const {
		const GeneralizedCircle &g = get_carrier();
		const V2 traversal = e - s;
		const V2 grad = V2::of(2.0 * g.a * double(v.x) + g.b, 2.0 * g.a * double(v.y) + g.c);
		return -double(traversal.cross(grad));
	}

	// Segment.determine_side
	Side determine_side(const V2 &point) const {
		const double f_val = get_carrier().evaluate(point.x, point.y);
		const double w = winding();
		if (w >= 0.0) {
			return f_val > 0.0 ? Side::Left : Side::Right;
		}
		return f_val > 0.0 ? Side::Right : Side::Left;
	}

	// Segment.full_from_carrier
	static SegGeom full_from_carrier(const GeneralizedCircle &g) {
		SegGeom seg;
		if (g.is_line()) {
			if (std::fabs(g.c) > std::fabs(g.b)) {
				seg.s = V2::of(0.0, -g.d / g.c);
				seg.e = V2::of(1.0, -(g.d + g.b) / g.c);
			} else {
				seg.s = V2::of(-g.d / g.b, 0.0);
				seg.e = V2::of(-(g.d + g.c) / g.b, 1.0);
			}
			seg.v = V2::inf();
		} else {
			const V2 ctr = center_of(g);
			const double r = g.radius();
			const V2 p1 = ctr + V2::of(r, 0);
			const V2 p2 = ctr + V2::of(-0.5 * r, 0.866 * r);
			const V2 p3 = ctr + V2::of(-0.5 * r, -0.866 * r);
			seg.s = p1;
			seg.e = p3;
			seg.v = p2;
		}
		seg.full = true;
		seg.carrier = g;
		return seg;
	}

	// ClickDetector._distance_to_segment
	double distance_to(const V2 &point) const {
		if (is_line()) {
			const V2 a = s;
			const V2 b = e;
			const V2 ab = b - a;
			const V2 ap = point - a;
			const float ab_len_sq = ab.length_squared();
			if (ab_len_sq == 0.0f) {
				return point.distance_to(a);
			}
			const double t = std::clamp(double(ab.dot(ap)) / double(ab_len_sq), 0.0, 1.0);
			const V2 nearest = a + ab * float(t);
			return point.distance_to(nearest);
		}
		const GeneralizedCircle &g = get_carrier();
		const V2 ctr = center_of(g);
		const double r = g.radius();
		if (full) {
			return std::fabs(double(point.distance_to(ctr)) - r);
		}
		const double sa = (s - ctr).angle();
		const double pa = (point - ctr).angle();
		const double cross = (s - ctr).cross(v - ctr);
		bool clockwise = cross < 0.0;
		const double ea = (e - ctr).angle();
		double span = clockwise ? fposmod(sa - ea, TAU) : fposmod(ea - sa, TAU);
		const double va = (v - ctr).angle();
		const double via_diff = clockwise ? fposmod(sa - va, TAU) : fposmod(va - sa, TAU);
		if (via_diff > span) {
			clockwise = !clockwise;
			span = TAU - span;
		}
		const double point_diff = clockwise ? fposmod(sa - pa, TAU) : fposmod(pa - sa, TAU);
		if (point_diff <= span) {
			return std::fabs(double(point.distance_to(ctr)) - r);
		}
		return gd_minf(point.distance_to(s), point.distance_to(e));
	}
};

// VisualConverter.arc_params
struct ArcParams {
	V2 center;
	double radius = 0.0;
	double start_angle = 0.0;
	double end_angle = 0.0;
	bool clockwise = false;
	int point_count = 2;
	double span = 0.0;
	bool line = true; // the carrier is a line: the zero params
};

inline ArcParams arc_params(const V2 &start, const V2 &via, const V2 &end_v) {
	ArcParams p;
	const GeneralizedCircle g = derive_carrier(start, end_v, via);
	if (g.is_line()) {
		return p;
	}
	p.line = false;
	const V2 ctr = center_of(g);
	p.center = ctr;
	p.radius = g.radius();
	const double sa = (start - ctr).angle();
	const double ea = (end_v - ctr).angle();
	const double cross = (start - ctr).cross(via - ctr);
	bool clockwise = cross < 0.0;
	double span;
	if (clockwise) {
		span = sa - ea;
		if (span < 0.0) {
			span += TAU;
		}
	} else {
		span = ea - sa;
		if (span < 0.0) {
			span += TAU;
		}
	}
	const double via_angle = (via - ctr).angle();
	const double via_diff = clockwise ? fposmod(sa - via_angle, TAU) : fposmod(via_angle - sa, TAU);
	if (via_diff > span) {
		clockwise = !clockwise;
		span = TAU - span;
	}
	p.point_count = std::max(4, int(POINTS_PER_FULL_CIRCLE * span / TAU));
	p.start_angle = sa;
	p.end_angle = sa + span * (clockwise ? -1.0 : 1.0);
	p.clockwise = clockwise;
	p.span = span;
	return p;
}

// Stage 134: two drawn points within tol px (the merge by segments)
inline bool near_pt(const V2 &a, const V2 &b, double tol = 0.01) {
	if (a == b) {
		return true;
	}
	return std::isfinite(a.x) && std::isfinite(b.x) && std::hypot(double(a.x) - double(b.x), double(a.y) - double(b.y)) <= tol;
}

// where p lies on a drawn step, as the fraction of the way along it (nullopt: off it)
inline std::optional<double> fraction_on(const TraceStep &st, const V2 &p, double tol = 0.01) {
	if (!std::isfinite(st.start.x) || !std::isfinite(st.end.x) || !std::isfinite(p.x)) {
		return std::nullopt;
	}
	if (near_pt(p, st.end, tol)) {
		return 1.0;
	}
	if (near_pt(p, st.start, tol)) {
		return 0.0;
	}
	if (st.is_arc_step && is_arc(st.start, st.via, st.end)) {
		const ArcParams a = arc_params(st.start, st.via, st.end);
		const double dx = double(p.x) - double(a.center.x);
		const double dy = double(p.y) - double(a.center.y);
		if (std::abs(std::hypot(dx, dy) - a.radius) > tol || !(a.span > 0.0)) {
			return std::nullopt;
		}
		const double ang = std::atan2(dy, dx);
		const double d = a.clockwise ? fposmod(a.start_angle - ang, TAU) : fposmod(ang - a.start_angle, TAU);
		return d <= a.span ? std::optional<double>(d / a.span) : std::nullopt;
	}
	const double dx = double(st.end.x) - double(st.start.x);
	const double dy = double(st.end.y) - double(st.start.y);
	const double l2 = dx * dx + dy * dy;
	if (!(l2 > 0.0)) {
		return std::nullopt;
	}
	const double px = double(p.x) - double(st.start.x);
	const double py = double(p.y) - double(st.start.y);
	const double f = (px * dx + py * dy) / l2;
	if (std::abs(px * dy - py * dx) / std::sqrt(l2) > tol || f < 0.0 || f > 1.0) {
		return std::nullopt;
	}
	return f;
}

// VisualConverter.project_onto_carrier
inline V2 project_onto_carrier(const V2 &point, const GeneralizedCircle &g) {
	if (g.is_line()) {
		const double denom = g.b * g.b + g.c * g.c;
		const double f = g.evaluate(point.x, point.y);
		return V2::of(double(point.x) - f * g.b / denom, double(point.y) - f * g.c / denom);
	}
	const V2 center = center_of(g);
	const double r = g.radius();
	const V2 diff = point - center;
	if (diff == V2{}) {
		return center + V2::of(r, 0);
	}
	return center + (float(r) * diff) / diff.length();
}

// VisualConverter._clip_to_bounds
inline V2 clip_to_bounds(const V2 &origin, const V2 &dir, const Rect2f &bounds) {
	double min_t = std::numeric_limits<double>::infinity();
	const V2 end = bounds.end();
	if (dir.x > 0.0f) {
		min_t = gd_minf(min_t, (double(end.x) - double(origin.x)) / double(dir.x));
	} else if (dir.x < 0.0f) {
		min_t = gd_minf(min_t, (double(bounds.position.x) - double(origin.x)) / double(dir.x));
	}
	if (dir.y > 0.0f) {
		min_t = gd_minf(min_t, (double(end.y) - double(origin.y)) / double(dir.y));
	} else if (dir.y < 0.0f) {
		min_t = gd_minf(min_t, (double(bounds.position.y) - double(origin.y)) / double(dir.y));
	}
	if (std::isinf(min_t) || min_t < 0.0) {
		min_t = 100.0;
	}
	return origin + dir * float(min_t);
}

// ---------------------------------------------------------------- display

// The geometry of a trace step the display kernels read.
struct StepGeom {
	V2 start;
	V2 end;
	V2 via;
	bool is_arc_step = false;
	bool has_hit = false;
	int surface_id = -1;
	bool has_ray = false;
	V2 ray_dir; // ray.direction.to_normalized()
};

// VisualConverter.compute_display_end: carrier_of(surface_id) is the hit
// surface's carrier (nullopt: unknown surface).
inline V2 display_end(const StepGeom &step, const std::function<std::optional<GeneralizedCircle>(int)> &carrier_of) {
	if (step.surface_id < 0) {
		return step.end;
	}
	const std::optional<GeneralizedCircle> g = carrier_of(step.surface_id);
	if (!g) {
		return step.end;
	}
	const V2 projected = project_onto_carrier(step.end, *g);
	if (step.end.distance_to(projected) > 5.0f) {
		return step.end;
	}
	return projected;
}

// One output step of prepare_for_display, in terms of its source step.
struct Piece {
	enum Kind { Identity, Snapped, Clipped };
	Kind kind = Identity;
	int src = 0;      // index of the source step
	V2 start;
	V2 end;
	V2 via;
	bool keep_hit = false; // the piece carries the source hit (else no hit)
	bool first = true;     // the first piece of a split keeps after_portal
	bool is_arc_step = false;
};

struct DisplayPlan {
	std::vector<Piece> pieces;
	int cursor_index = -1;
};

// VisualConverter.prepare_for_display: which pieces to emit and their geometry.
inline DisplayPlan prepare_for_display(const std::vector<StepGeom> &steps, int cursor_index, const Rect2f &bounds,
		const std::function<std::optional<GeneralizedCircle>(int)> &carrier_of) {
	DisplayPlan out;
	out.cursor_index = cursor_index;
	const Rect2f grown = bounds.grow(1.0f);
	int cursor_shift = 0;
	for (size_t i = 0; i < steps.size(); i++) {
		const StepGeom &step = steps[i];
		const int si = int(i);
		const bool end_inf = step.end.is_inf();
		const bool start_inf = step.start.is_inf();
		V2 step_via = step.via;
		bool via_inf = std::isinf(step_via.x) && std::isinf(step_via.y);
		if ((end_inf || start_inf) && via_inf && step.has_ray) {
			const V2 dir = step.ray_dir;
			if (end_inf && !start_inf) {
				step_via = dir;
			} else if (start_inf && !end_inf) {
				step_via = -dir;
			}
			via_inf = std::isinf(step_via.x) && std::isinf(step_via.y);
		}
		auto piece = [&](Piece::Kind kind, V2 p_start, V2 p_end, bool keep_hit, V2 via, bool first) {
			out.pieces.push_back(Piece{kind, si, p_start, p_end, via, keep_hit, first, false});
		};
		if (!via_inf && !end_inf && !start_inf) {
			const V2 de = display_end(step, carrier_of);
			if (de == step.end || de == step.start) {
				out.pieces.push_back(Piece{Piece::Identity, si, step.start, step.end, step.via, step.has_hit, true, step.is_arc_step});
			} else {
				out.pieces.push_back(Piece{Piece::Snapped, si, step.start, de, step.via, step.has_hit, true, step.is_arc_step});
			}
			continue;
		}
		// a half of a wrap that is zero-length on the bounds' edge is not drawn:
		// the cursor index counts drawn steps (B3, Stage 110)
		if (end_inf && !start_inf && !via_inf) {
			const V2 clip = clip_to_bounds(step.start, step_via.normalized(), bounds);
			if (clip != step.start || !step.has_hit) {
				piece(Piece::Clipped, step.start, clip, step.has_hit, (step.start + clip) / 2.0f, true);
			} else if (si < cursor_index) {
				cursor_shift -= 1;
			}
			continue;
		}
		if (start_inf && !end_inf && !via_inf) {
			const V2 clip = clip_to_bounds(step.end, step_via.normalized(), bounds);
			if (clip != step.end || !step.has_hit) {
				piece(Piece::Clipped, clip, step.end, step.has_hit, (clip + step.end) / 2.0f, true);
			} else if (si < cursor_index) {
				cursor_shift -= 1;
			}
			continue;
		}
		const bool start_in = grown.has_point(step.start);
		const bool end_in = grown.has_point(step.end);
		if (step.has_hit || (start_in && end_in)) {
			const V2 vis_dir = (step.end - step.start).normalized();
			if (vis_dir == V2{}) {
				piece(Piece::Clipped, step.start, step.end, step.has_hit, (step.start + step.end) / 2.0f, true);
				continue;
			}
			const V2 esc = clip_to_bounds(step.start, -vis_dir, bounds);
			const V2 ret = clip_to_bounds(step.end, vis_dir, bounds);
			const V2 via_out = step.start != esc ? (step.start + esc) / 2.0f : step.start;
			const V2 via_in = ret != step.end ? (ret + step.end) / 2.0f : step.end;
			piece(Piece::Clipped, step.start, esc, false, via_out, true);
			piece(Piece::Clipped, ret, step.end, step.has_hit, via_in, false);
			if (si < cursor_index) {
				cursor_shift += 1;
			}
		} else if (start_in) {
			const V2 vis_dir = (step.end - step.start).normalized();
			const V2 clip = clip_to_bounds(step.start, vis_dir, bounds);
			piece(Piece::Clipped, step.start, clip, step.has_hit, (step.start + clip) / 2.0f, true);
		} else if (end_in) {
			const V2 vis_dir = (step.end - step.start).normalized();
			const V2 clip = clip_to_bounds(step.end, -vis_dir, bounds);
			piece(Piece::Clipped, clip, step.end, step.has_hit, (clip + step.end) / 2.0f, true);
		} else {
			piece(Piece::Clipped, step.start, step.end, step.has_hit, (step.start + step.end) / 2.0f, true);
		}
	}
	if (cursor_index >= 0) {
		out.cursor_index = cursor_index + cursor_shift;
	}
	return out;
}

// ---------------------------------------------------------------- arrow flight

struct AdvanceResult {
	int step_index = 0;
	double progress = 0.0;
	V2 position;
	V2 direction = V2{1.0f, 0.0f};
	bool finished = false;
};

// ArrowAnimator._is_arc
inline bool step_is_arc(const StepGeom &s) { return s.is_arc_step && is_arc(s.start, s.via, s.end); }
// ArrowAnimator._arc_length
inline double arc_length(const StepGeom &s) {
	const ArcParams p = arc_params(s.start, s.via, s.end);
	return p.radius * p.span;
}
struct ArcPoint {
	V2 position;
	V2 direction;
};
// ArrowAnimator._arc_interpolate
inline ArcPoint arc_interpolate(const StepGeom &s, double t) {
	const ArcParams p = arc_params(s.start, s.via, s.end);
	const V2 ctr = p.center;
	const double radius = p.radius;
	const double sa = (s.start - ctr).angle();
	const bool cw = p.clockwise;
	const double angle = sa + t * p.span * (cw ? -1.0 : 1.0);
	const V2 pos = ctr + float(radius) * V2::of(std::cos(angle), std::sin(angle));
	const V2 rv = pos - ctr;
	const V2 tangent = cw ? V2{rv.y, -rv.x}.normalized() : V2{-rv.y, rv.x}.normalized();
	return ArcPoint{pos, tangent};
}
// ArrowAnimator._arc_reentry
inline double arc_reentry(const StepGeom &s, double t_now, const Rect2f &rect) {
	const ArcParams p = arc_params(s.start, s.via, s.end);
	const V2 ctr = p.center;
	const double radius = p.radius;
	const double span = p.span;
	if (span <= 0.0) {
		return -1.0;
	}
	const double sa = (s.start - ctr).angle();
	const double dirn = p.clockwise ? -1.0 : 1.0;
	std::vector<V2> pts;
	const V2 end = rect.end();
	for (double x : {double(rect.position.x), double(end.x)}) {
		const double h2 = radius * radius - (x - double(ctr.x)) * (x - double(ctr.x));
		if (h2 >= 0.0) {
			for (double y : {double(ctr.y) - std::sqrt(h2), double(ctr.y) + std::sqrt(h2)}) {
				if (y >= double(rect.position.y) && y <= double(end.y)) {
					pts.push_back(V2::of(x, y));
				}
			}
		}
	}
	for (double y : {double(rect.position.y), double(end.y)}) {
		const double h2 = radius * radius - (y - double(ctr.y)) * (y - double(ctr.y));
		if (h2 >= 0.0) {
			for (double x : {double(ctr.x) - std::sqrt(h2), double(ctr.x) + std::sqrt(h2)}) {
				if (x >= double(rect.position.x) && x <= double(end.x)) {
					pts.push_back(V2::of(x, y));
				}
			}
		}
	}
	double best = -1.0;
	for (const V2 &q : pts) {
		const double d = fposmod((double((q - ctr).angle()) - sa) * dirn, TAU);
		const double t = d / span;
		if (t > t_now && t <= 1.0 && (best < 0.0 || t < best)) {
			best = t;
		}
	}
	return best;
}

inline void update_direction(AdvanceResult &r, const std::vector<StepGeom> &steps) {
	if (r.step_index < int(steps.size())) {
		const StepGeom &s = steps[size_t(r.step_index)];
		if (step_is_arc(s)) {
			const double len = arc_length(s);
			const double t = len > 0.0 ? r.progress / len : 0.0;
			r.direction = arc_interpolate(s, t).direction;
		} else {
			r.direction = (s.end - s.start).normalized();
		}
	}
}

// ArrowAnimator.advance
inline AdvanceResult advance(const std::vector<StepGeom> &steps, int step_index, double progress, const V2 &pos, double distance,
		const Rect2f &bounds) {
	const Rect2f tolerant = bounds.grow(2.0f);
	AdvanceResult r;
	r.step_index = step_index;
	r.progress = progress;
	r.position = pos;
	r.direction = V2{1.0f, 0.0f};
	const int n = int(steps.size());
	while (distance > 0.0 && r.step_index < n) {
		const StepGeom &step = steps[size_t(r.step_index)];
		const bool arc = step_is_arc(step);
		const double step_length = arc ? arc_length(step) : double(step.start.distance_to(step.end));
		const double remaining = step_length - r.progress;
		if (distance < remaining) {
			r.progress += distance;
			const double t = step_length > 0.0 ? r.progress / step_length : 1.0;
			if (arc) {
				const ArcPoint ip = arc_interpolate(step, t);
				r.position = ip.position;
				r.direction = ip.direction;
			} else {
				r.position = step.start.lerp(step.end, float(t));
				r.direction = (step.end - step.start).normalized();
			}
			distance = 0.0;
		} else {
			distance -= remaining;
			r.position = step.end;
			r.step_index += 1;
			r.progress = 0.0;
		}
		if (!tolerant.has_point(r.position)) {
			if (r.progress > 0.0 && r.step_index < n) {
				const StepGeom &cur = steps[size_t(r.step_index)];
				bool resumed = false;
				if (step_is_arc(cur)) {
					const double len = arc_length(cur);
					const double t_re = arc_reentry(cur, r.progress / len, bounds);
					if (t_re >= 0.0) {
						r.progress = t_re * len;
						r.position = arc_interpolate(cur, t_re).position;
						resumed = true;
					}
				}
				if (!resumed) {
					r.position = cur.end;
					r.step_index += 1;
					r.progress = 0.0;
				}
			}
			while (!tolerant.has_point(r.position) && r.step_index < n) {
				if (tolerant.has_point(steps[size_t(r.step_index)].start)) {
					r.position = steps[size_t(r.step_index)].start;
					r.progress = 0.0;
					break;
				}
				r.position = steps[size_t(r.step_index)].end;
				r.step_index += 1;
				r.progress = 0.0;
			}
		}
		update_direction(r, steps);
	}
	r.finished = r.step_index >= n;
	return r;
}

// ---------------------------------------------------------------- surface drawing samples

struct LineSample {
	V2 position;
	V2 normal;
};
// SurfaceNode.line_sample
inline LineSample line_sample(const V2 &start, const V2 &end, double t) {
	const V2 pos = start.lerp(end, float(t));
	const V2 seg_dir = (end - start).normalized();
	return LineSample{pos, V2{-seg_dir.y, seg_dir.x}};
}
struct ArcSample {
	V2 position;
	V2 outward;
};
// SurfaceNode.arc_sample
inline ArcSample arc_sample(const V2 &center, double radius, double start_angle, double span, bool clockwise, double t) {
	const double angle = start_angle + t * span * (clockwise ? -1.0 : 1.0);
	const V2 pos = center + V2::of(std::cos(angle), std::sin(angle)) * float(radius);
	return ArcSample{pos, (pos - center).normalized()};
}
// SurfaceNode.chevron_vertices: tip, base_left, base_right
inline std::array<V2, 3> chevron_vertices(const V2 &tip, const V2 &direction, double size) {
	const V2 dir = direction.normalized();
	const V2 base_left = tip - dir.rotated(0.5f) * float(size);
	const V2 base_right = tip - dir.rotated(-0.5f) * float(size);
	return {tip, base_left, base_right};
}

// Stage 116: dashed strokes (VisualConverter.dash_arc / dash_line; the
// drawing's dashes, as segment pairs [a0, b0, a1, b1, ...]). The arc's points
// are float64 on the float32 center, rounded to float32; the line's dashes run
// along the direction over the capped length (as the GDScript original).
// Stage 130: phase (px) moves the pattern forward along the stroke (the path
// preview's dashes run the way the arrow flies); 0 is the original pattern.
inline double dash_start(double phase, double period) {
	const double m = std::fmod(phase, period);
	return (m < 0.0 ? m + period : m) - period;
}

inline std::vector<V2> dash_arc(const V2 &center, double radius, double start_angle, double end_angle, int point_count, double dash_on,
		double dash_off, double max_len, double phase = 0.0) {
	std::vector<V2> pts;
	const double span = end_angle - start_angle;
	const double total_len = std::min(radius * std::abs(span), max_len);
	if (total_len == 0.0 || !(dash_on > 0.0) || !(dash_off >= 0.0)) {
		return pts;
	}
	const double cx = center.x;
	const double cy = center.y;
	if (phase != 0.0) {
		auto dash = [&](double d0, double d1) {
			const double a0 = start_angle + (d0 / total_len) * span;
			const double a1 = start_angle + (d1 / total_len) * span;
			const int seg_points = std::max(2, int(point_count * (d1 - d0) / total_len));
			V2 prev = V2::of(cx + std::cos(a0) * radius, cy + std::sin(a0) * radius);
			for (int k = 1; k < seg_points; k++) {
				const double th = a0 + (k / (seg_points - 1.0)) * (a1 - a0);
				const V2 q = V2::of(cx + std::cos(th) * radius, cy + std::sin(th) * radius);
				pts.push_back(prev);
				pts.push_back(q);
				prev = q;
			}
		};
		for (double s0 = dash_start(phase, dash_on + dash_off); s0 < total_len; s0 += dash_on + dash_off) {
			const double d0 = std::max(s0, 0.0), d1 = std::min(s0 + dash_on, total_len);
			if (d1 > d0) {
				dash(d0, d1);
			}
		}
		return pts;
	}
	double drawn = 0.0;
	bool on = true;
	while (drawn < total_len) {
		const double seg_len = std::min(on ? dash_on : dash_off, total_len - drawn);
		if (on) {
			const double a0 = start_angle + (drawn / total_len) * span;
			const double a1 = start_angle + ((drawn + seg_len) / total_len) * span;
			const int seg_points = std::max(2, int(point_count * seg_len / total_len));
			V2 prev = V2::of(cx + std::cos(a0) * radius, cy + std::sin(a0) * radius);
			for (int k = 1; k < seg_points; k++) {
				const double th = a0 + (k / (seg_points - 1.0)) * (a1 - a0);
				const V2 q = V2::of(cx + std::cos(th) * radius, cy + std::sin(th) * radius);
				pts.push_back(prev);
				pts.push_back(q);
				prev = q;
			}
		}
		drawn += seg_len;
		on = !on;
	}
	return pts;
}

inline std::vector<V2> dash_line(const V2 &from, const V2 &to, double dash_on, double dash_off, double max_len, double phase = 0.0) {
	std::vector<V2> pts;
	const double dx = double(to.x) - double(from.x);
	const double dy = double(to.y) - double(from.y);
	const double total_len = std::min(std::sqrt(dx * dx + dy * dy), max_len);
	if (total_len == 0.0 || !(dash_on > 0.0) || !(dash_off >= 0.0)) {
		return pts;
	}
	const double ux = dx / total_len;
	const double uy = dy / total_len;
	if (phase != 0.0) {
		for (double s0 = dash_start(phase, dash_on + dash_off); s0 < total_len; s0 += dash_on + dash_off) {
			const double d0 = std::max(s0, 0.0), d1 = std::min(s0 + dash_on, total_len);
			if (d1 > d0) {
				pts.push_back(V2::of(from.x + ux * d0, from.y + uy * d0));
				pts.push_back(V2::of(from.x + ux * d1, from.y + uy * d1));
			}
		}
		return pts;
	}
	double drawn = 0.0;
	bool on = true;
	while (drawn < total_len) {
		const double seg_len = std::min(on ? dash_on : dash_off, total_len - drawn);
		if (on) {
			pts.push_back(V2::of(from.x + ux * drawn, from.y + uy * drawn));
			pts.push_back(V2::of(from.x + ux * (drawn + seg_len), from.y + uy * (drawn + seg_len)));
		}
		drawn += seg_len;
		on = !on;
	}
	return pts;
}

} // namespace arc
} // namespace rc
