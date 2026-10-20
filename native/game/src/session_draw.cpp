// Stage 118: the session's draw list -- the port of every node's _draw
// (SurfaceNode, EdgeOverlayNode, VisibilityRenderer, SpawnPreviewNode,
// PathRenderer, ArrowAnimator, Cursor) and of the nodes drawn by their
// properties (the player's Polygon2D, the background ColorRect, the HUD
// Label), call for call. GDScript's precision is kept: Vector2 maths in
// float32, a float64 scalar rounded to float32 where it meets a vector, the
// colours' components float32.
#include "rcg/session.hpp"

#include "rcg/digest.hpp"
#include "rcg/geom.hpp"
#include "rcg/model.hpp"
#include "rcg/render.hpp"

#include <algorithm>
#include <cmath>

namespace rcg {
namespace {

using render::Call;
using render::Color;
using render::Kind;
using render::Node;

constexpr double LINE_WIDTH = 3.0;
constexpr double SIDE_OFFSET = 2.0;
constexpr double HOVER_WIDTH = 24.0; // ClickDetector.CLICK_TOLERANCE
constexpr int GRADIENT_STRIPS = 6;
constexpr double CHEVRON_SIZE = 8.0;
constexpr double INNER_CHEVRON_DIST = 2.0;
constexpr double OUTER_CHEVRON_DIST = 18.0;
constexpr double MARKER_SIZE = 5.0;
constexpr double MARKER_SPACING = 14.0;
constexpr double CROSSING_DOT = 2.5;
constexpr double DASH = 8.0;
constexpr int MAX_DASHES = 256;
constexpr double PI = 3.141592653589793;

const Color WHITE{1, 1, 1, 1};
const Color GRAY = render::hex(0xBEBEBEFF);
const Color BLUE = render::hex(0x0000FFFF);
const Color PURPLE = render::hex(0xA020F0FF);
const Color RED = render::hex(0xFF0000FF);
const Color CYAN = render::hex(0x00FFFFFF);
const Color MAGENTA = render::hex(0xFF00FFFF);
const Color GREEN = render::hex(0x00FF00FF);
const Color YELLOW = render::hex(0xFFFF00FF);

Color rgba(double r, double g, double b, double a = 1.0) { return {float(r), float(g), float(b), float(a)}; }

const Color GHOST_TOGGLER_COLOR = rgba(0.75, 0.95, 1.0);
const Color FLAG_TOGGLER_COLOR = rgba(1.0, 0.85, 0.2);
const Color PRE_TRANSFORM_COLOR = rgba(0.55, 0.45, 0.85, 0.35);
const Color ABSENT_COLOR = rgba(0.6, 0.6, 0.6, 0.35);
const Color CROSSING_COLOR = rgba(1.0, 1.0, 1.0, 0.6);
const Color TARGET_COLOR = rgba(1.0, 0.85, 0.3);
const Color SPAWN_COLOR = rgba(0.6, 0.95, 1.0);
const Color RESET_COLOR = rgba(1.0, 0.55, 0.85);
const Color HOVER_COLOR = rgba(1.0, 1.0, 1.0, 0.3);
const Color PLAN_VALID_COLOR = rgba(1.0, 1.0, 1.0, 0.15);
const Color PLAN_INVALID_COLOR = rgba(1.0, 0.0, 0.0, 0.15);
const Color POINT_COLOR = rgba(0.75, 0.85, 1.0, 0.9);
const Color GHOST_SPAWN_COLOR = rgba(0.6, 0.95, 1.0, 0.8);
const Color VANISH_COLOR = rgba(1.0, 0.35, 0.3, 0.7);
const Color FILL = rgba(1.0, 1.0, 1.0, 0.10);

V2 vs(const V2 &v, double s) { return v * float(s); } // Vector2 * float (the scalar rounded first)
V2 rot90(const V2 &v) { return V2{-v.y, v.x}; }

Call line(const V2 &a, const V2 &b, const Color &c, double width) {
	Call k;
	k.kind = Kind::Line;
	k.pts = {a, b};
	k.colors = {c};
	k.width = width;
	return k;
}
Call arc(const V2 &center, double radius, double start, double end, int count, const Color &c, double width) {
	Call k;
	k.kind = Kind::Arc;
	k.pts = {center};
	k.radius = radius;
	k.start = start;
	k.end = end;
	k.point_count = count;
	k.colors = {c};
	k.width = width;
	return k;
}
Call polygon(const std::array<V2, 3> &pts, const Color &c) {
	Call k;
	k.kind = Kind::Polygon;
	k.pts = {pts[0], pts[1], pts[2]};
	k.colors = {c, c, c};
	return k;
}
Call circle(const V2 &center, double radius, const Color &c) {
	Call k;
	k.kind = Kind::Circle;
	k.pts = {center};
	k.radius = radius;
	k.colors = {c};
	return k;
}
Call text(const V2 &pos, const std::string &s, int alignment, int size, const Color &c) {
	Call k;
	k.kind = Kind::Text;
	k.pts = {pos};
	k.text = s;
	k.alignment = alignment;
	k.font_size = size;
	k.colors = {c};
	return k;
}
Call stroke(Kind kind, std::vector<V2> pts, const Color &c, double width) {
	Call k;
	k.kind = kind;
	k.pts = std::move(pts);
	k.colors = {c};
	k.width = width;
	return k;
}

// Math::wrapf (float64)
double wrapf(double value, double lo, double hi) {
	const double range = hi - lo;
	if (std::abs(range) < 0.00001) {
		return lo;
	}
	const double result = value - (range * std::floor((value - lo) / range));
	double tol = 0.00001 * std::abs(result);
	if (tol < 0.00001) {
		tol = 0.00001;
	}
	if (result == hi || std::abs(result - hi) < tol) {
		return lo;
	}
	return result;
}

// SpawnPreviewNode.dashed: a segment drawn dashed
void dashed(std::vector<Call> &out, const SegGeom &seg, const Color &color, double width) {
	if (seg.is_line()) {
		Call k = line(seg.s, seg.e, color, width);
		k.kind = Kind::DashedLine;
		k.dash = DASH;
		out.push_back(k);
		return;
	}
	const rc::arc::GeneralizedCircle &g = seg.get_carrier();
	const V2 c = rc::arc::center_of(g);
	const double r = g.radius();
	if (r > rc::arc::MAX_ARC_RADIUS && !seg.full) {
		Call k = line(seg.s, seg.e, color, width);
		k.kind = Kind::DashedLine;
		k.dash = DASH;
		out.push_back(k);
		return;
	}
	double a0 = 0.0, span = rcg::TAU;
	if (!seg.full) {
		a0 = double((seg.s - c).angle());
		const double am = wrapf(double((seg.v - c).angle()) - a0, 0.0, rcg::TAU);
		const double a1 = wrapf(double((seg.e - c).angle()) - a0, 0.0, rcg::TAU);
		span = am < a1 ? a1 : a1 - rcg::TAU;
	}
	const int n = std::clamp(int(std::abs(span) * r / DASH), 8, MAX_DASHES);
	for (int i = 0; i < n; i += 2) {
		const double t0 = a0 + span * i / n;
		const double t1 = a0 + span * (i + 1) / n;
		out.push_back(arc(c, r, t0, t1, 4, color, width));
	}
}

// SurfaceNode.config_color
Color config_color(const BuiltSide *cfg) {
	if (cfg == nullptr) {
		return GRAY;
	}
	const BuiltEffect &e = cfg->effect;
	if (e.type == "pass") {
		if (!cfg->hits.empty()) {
			return writes_key(*cfg, "ghost", -1) ? GHOST_TOGGLER_COLOR : FLAG_TOGGLER_COLOR;
		}
		return GRAY;
	}
	if (e.type == "terminal") return RED;
	if (e.type == "reflection") return BLUE;
	if (e.type == "inversion") return PURPLE;
	if (e.type == "portal") return CYAN;
	return e.back ? rgba(1.0, 0.4, 0.1) : rgba(1.0, 0.8, 0.2);
}

// One surface node's _draw (a piece of a surface).
struct SurfacePainter {
	const BuiltSurface &surface;
	const SegGeom &seg;
	int piece_index;
	const rc::Flags &flags;
	std::vector<Call> &out;

	bool is_line() const { return seg.is_line(); }

	rc::arc::ArcParams params() const { return rc::arc::arc_params(seg.s, seg.v, seg.e); }

	bool left_outer() const {
		const rc::arc::GeneralizedCircle &g = seg.get_carrier();
		if (g.is_line()) {
			return true;
		}
		const V2 ctr = rc::arc::center_of(g);
		const V2 test = ctr + vs((seg.s - ctr).normalized(), g.radius() + SIDE_OFFSET);
		return seg.determine_side(test) == rc::Side::Left;
	}

	V2 line_side_offset(Side side, double dist) const {
		const V2 dir = (seg.e - seg.s).normalized();
		const V2 normal{-dir.y, dir.x};
		const V2 mid = (seg.s + seg.e) / 2.0f;
		const Side at = seg.determine_side(mid + vs(normal, SIDE_OFFSET)) == rc::Side::Left ? Side::Left : Side::Right;
		const float sign = at == side ? 1.0f : -1.0f;
		return vs(normal, dist) * sign;
	}

	struct ArcView {
		V2 ctr;
		double r, sa, ea, span;
		int pc;
		bool cw;
	};
	ArcView arc_view() const {
		if (seg.full) {
			const rc::arc::GeneralizedCircle &g = seg.get_carrier();
			return {rc::arc::center_of(g), g.radius(), 0.0, rcg::TAU, rcg::TAU, rc::arc::POINTS_PER_FULL_CIRCLE, false};
		}
		const rc::arc::ArcParams p = params();
		return {p.center, p.radius, p.start_angle, p.end_angle, p.span, p.point_count, p.clockwise};
	}

	void surface_arc(const Color &color, double width) {
		const ArcView a = arc_view();
		out.push_back(arc(a.ctr, a.r, a.sa, a.ea, a.pc, color, width));
	}

	void gradient(int side, bool arcs, const Color &base, bool l_outer) {
		const double strip = HOVER_WIDTH / GRADIENT_STRIPS;
		const bool outer = (side == int(Side::Left)) == l_outer;
		const double sign = outer ? 1.0 : -1.0;
		if (arcs) {
			const ArcView a = arc_view();
			for (int i = 0; i < GRADIENT_STRIPS; i++) {
				const double frac = (double(i) + 0.5) / GRADIENT_STRIPS;
				const double alpha = double(base[3]) * (1.0 - frac);
				const double offset = frac * HOVER_WIDTH * sign;
				out.push_back(arc(a.ctr, a.r + offset, a.sa, a.ea, a.pc, render::with_alpha(base, alpha), strip));
			}
		} else {
			for (int i = 0; i < GRADIENT_STRIPS; i++) {
				const double frac = (double(i) + 0.5) / GRADIENT_STRIPS;
				const double alpha = double(base[3]) * (1.0 - frac);
				const V2 offset = line_side_offset(Side(side), frac * HOVER_WIDTH);
				out.push_back(line(seg.s + offset, seg.e + offset, render::with_alpha(base, alpha), strip));
			}
		}
	}

	void chevrons(int side, bool arcs, const Color &base, double dist, bool point_away, bool l_outer) {
		const bool outer = (side == int(Side::Left)) == l_outer;
		const Color col = render::with_alpha(base, base[3]);
		static const double T[3] = {1.0 / 6.0, 0.5, 5.0 / 6.0};
		if (arcs) {
			const ArcView a = arc_view();
			for (double t : T) {
				const rc::arc::ArcSample s = rc::arc::arc_sample(a.ctr, a.r, a.sa, a.span, a.cw, t);
				const V2 side_dir = outer ? s.outward : -s.outward;
				const V2 dir = point_away ? side_dir : -side_dir;
				const V2 tip = s.position + vs(side_dir, dist);
				out.push_back(polygon(rc::arc::chevron_vertices(tip, dir, CHEVRON_SIZE), col));
			}
		} else {
			for (double t : T) {
				const rc::arc::LineSample s = rc::arc::line_sample(seg.s, seg.e, t);
				const Side at = seg.determine_side(s.position + vs(s.normal, SIDE_OFFSET)) == rc::Side::Left ? Side::Left : Side::Right;
				const V2 side_dir = int(at) == side ? s.normal : -s.normal;
				const V2 dir = point_away ? side_dir : -side_dir;
				const V2 tip = s.position + vs(side_dir, dist);
				out.push_back(polygon(rc::arc::chevron_vertices(tip, dir, CHEVRON_SIZE), col));
			}
		}
	}

	void overlay(const Overlay &ov, bool arcs, bool l_outer) {
		if (ov.has_incoming) {
			gradient(ov.side, arcs, ov.gradient_color, l_outer);
			chevrons(ov.side, arcs, ov.incoming_color, INNER_CHEVRON_DIST, false, l_outer);
		}
		if (ov.has_outgoing) {
			gradient(ov.outgoing_side, arcs, ov.gradient_color, l_outer);
			chevrons(ov.outgoing_side, arcs, ov.outgoing_color, OUTER_CHEVRON_DIST, true, l_outer);
		}
	}

	V2 marker_tangent(const V2 &at) const {
		if (seg.is_line()) {
			return (seg.e - seg.s).normalized();
		}
		return rot90(at - rc::arc::center_of(seg.get_carrier())).normalized();
	}

	V2 marker_normal(Side side, const V2 &at) const {
		const V2 n = rot90(marker_tangent(at));
		const V2 probe = at + n;
		return (seg.determine_side(probe) == rc::Side::Left ? Side::Left : Side::Right) == side ? n : -n;
	}

	void marker(const HitSpec &h, const V2 &at) {
		const double s = MARKER_SIZE;
		if (h.kind == "target") {
			out.push_back(arc(at, s, 0.0, rcg::TAU, 16, TARGET_COLOR, 1.5));
			out.push_back(circle(at, 1.5, TARGET_COLOR));
		} else if (h.kind == "spawn") {
			out.push_back(line(at - V2::of(s, 0), at + V2::of(s, 0), SPAWN_COLOR, 2.0));
			out.push_back(line(at - V2::of(0, s), at + V2::of(0, s), SPAWN_COLOR, 2.0));
		} else if (h.kind == "reset") {
			out.push_back(line(at - V2::of(s, s), at + V2::of(s, s), RESET_COLOR, 2.0));
			out.push_back(line(at - V2::of(s, -s), at + V2::of(s, -s), RESET_COLOR, 2.0));
		} else {
			Call k;
			k.kind = Kind::ColoredPolygon;
			k.pts = {at + V2::of(0, -s), at + V2::of(s, 0), at + V2::of(0, s), at + V2::of(-s, 0)};
			k.colors = {FLAG_TOGGLER_COLOR};
			out.push_back(k);
		}
		if (h.deferred) {
			out.push_back(circle(at, 2.0, rgba(0.1, 0.1, 0.1)));
		}
	}

	void hit_markers(const BuiltSide *left, const BuiltSide *right) {
		const V2 via = seg.v;
		const V2 tangent = marker_tangent(via);
		for (Side side : {Side::Left, Side::Right}) {
			const BuiltSide *cfg = side == Side::Left ? left : right;
			if (cfg == nullptr) {
				continue;
			}
			std::vector<const HitSpec *> marks;
			for (const HitSpec &h : cfg->hits) {
				if (h.kind == "state" && cfg->effect.type == "pass") {
					continue;
				}
				marks.push_back(&h);
			}
			const V2 normal = vs(marker_normal(side, via), MARKER_SIZE + 6.0);
			for (size_t i = 0; i < marks.size(); i++) {
				const double k = double(i) - double(marks.size() - 1) / 2.0;
				const V2 at = via + normal + vs(vs(tangent, MARKER_SPACING), k);
				marker(*marks[i], at);
			}
		}
	}

	void draw(const std::vector<Overlay> &hover, const std::vector<Overlay> &plan, const std::vector<int> &plan_indices) {
		if (surface.pre_transform) {
			const rc::arc::GeneralizedCircle &g = seg.get_carrier();
			out.push_back(arc(rc::arc::center_of(g), g.radius(), 0.0, rcg::TAU, rc::arc::POINTS_PER_FULL_CIRCLE, PRE_TRANSFORM_COLOR, 1.0));
			return;
		}
		if (!present(surface, flags)) {
			dashed(out, seg, ABSENT_COLOR, 1.5);
			return;
		}
		const BuiltSide *left = active_side(surface, Side::Left, flags);
		const BuiltSide *right = active_side(surface, Side::Right, flags);
		const Color lc = config_color(left), rc_ = config_color(right);
		const bool arcs = !seg.is_line();
		const bool l_outer = left_outer();
		if (lc == rc_) {
			if (arcs) {
				surface_arc(lc, LINE_WIDTH);
			} else {
				out.push_back(line(seg.s, seg.e, lc, LINE_WIDTH));
			}
		} else {
			const double la = left && left->interactive ? 1.0 : 0.5;
			const double ra = right && right->interactive ? 1.0 : 0.5;
			if (arcs) {
				const ArcView a = arc_view();
				const Color outer = l_outer ? render::with_alpha(lc, la) : render::with_alpha(rc_, ra);
				const Color inner = l_outer ? render::with_alpha(rc_, ra) : render::with_alpha(lc, la);
				out.push_back(arc(a.ctr, a.r + SIDE_OFFSET, a.sa, a.ea, a.pc, outer, LINE_WIDTH * 0.5));
				out.push_back(arc(a.ctr, a.r - SIDE_OFFSET, a.sa, a.ea, a.pc, inner, LINE_WIDTH * 0.5));
			} else {
				const V2 lo = line_side_offset(Side::Left, SIDE_OFFSET);
				const V2 ro = line_side_offset(Side::Right, SIDE_OFFSET);
				out.push_back(line(seg.s + lo, seg.e + lo, render::with_alpha(lc, la), LINE_WIDTH * 0.5));
				out.push_back(line(seg.s + ro, seg.e + ro, render::with_alpha(rc_, ra), LINE_WIDTH * 0.5));
			}
		}
		std::vector<int> hover_sides;
		for (const Overlay &o : hover) {
			hover_sides.push_back(o.side);
		}
		for (const Overlay &o : plan) {
			if (o.has_incoming && std::find(hover_sides.begin(), hover_sides.end(), o.side) != hover_sides.end()) {
				continue;
			}
			overlay(o, arcs, l_outer);
		}
		for (const Overlay &o : hover) {
			overlay(o, arcs, l_outer);
		}
		if (piece_index <= 0) {
			hit_markers(left, right);
		}
		if (piece_index >= 0) {
			for (const V2 &end : {seg.s, seg.e}) {
				if (end != surface.seg.s && end != surface.seg.e) {
					out.push_back(circle(end, CROSSING_DOT, CROSSING_COLOR));
				}
			}
		}
		if (!plan_indices.empty()) {
			std::string label;
			for (int idx : plan_indices) {
				if (!label.empty()) {
					label += ",";
				}
				label += std::to_string(idx);
			}
			out.push_back(text(seg.v + V2{-5, -10}, label, 1, 14, WHITE));
		}
	}
};

// ---------------------------------------------------------------- the edge (EdgeOverlayNode)

const V2 UP{0, -1}, DOWN{0, 1}, LEFT_V{-1, 0}, RIGHT_V{1, 0};

void edge_gradient(std::vector<Call> &out, const V2 &a, const V2 &b, const V2 &o, const Color &base) {
	const double strip = HOVER_WIDTH / GRADIENT_STRIPS;
	for (int i = 0; i < GRADIENT_STRIPS; i++) {
		const double frac = (double(i) + 0.5) / GRADIENT_STRIPS;
		const double alpha = double(base[3]) * (1.0 - frac);
		const V2 off = vs(-o, frac * HOVER_WIDTH);
		const V2 p0 = a + off - vs(o, strip * 0.5);
		const V2 p1 = b + off + vs(o, strip * 0.5);
		V2 lo = V2::of(std::min(double(p0.x), double(p1.x)), std::min(double(p0.y), double(p1.y)));
		V2 hi = V2::of(std::max(double(p0.x), double(p1.x)), std::max(double(p0.y), double(p1.y)));
		if (double(hi.x) - double(lo.x) < strip) {
			lo.x = float(double(lo.x) - strip * 0.5);
			hi.x = float(double(hi.x) + strip * 0.5);
		}
		if (double(hi.y) - double(lo.y) < strip) {
			lo.y = float(double(lo.y) - strip * 0.5);
			hi.y = float(double(hi.y) + strip * 0.5);
		}
		Call k;
		k.kind = Kind::Rect;
		k.pts = {lo, hi - lo};
		k.colors = {render::with_alpha(base, alpha)};
		out.push_back(k);
	}
}

void edge_chevrons(std::vector<Call> &out, const V2 &pt, const V2 &o, const Color &base) {
	out.push_back(polygon(rc::arc::chevron_vertices(pt - vs(o, INNER_CHEVRON_DIST), o, CHEVRON_SIZE), base));
	out.push_back(polygon(rc::arc::chevron_vertices(pt - vs(o, OUTER_CHEVRON_DIST), -o, CHEVRON_SIZE), base));
}

constexpr double POINT_RADIUS = 9.0;
constexpr double MARK_HALF_LENGTH = 60.0;

void point_overlay(std::vector<Call> &out, const V2 &q, const Color &base) {
	const double strip = HOVER_WIDTH / GRADIENT_STRIPS;
	for (int i = 0; i < GRADIENT_STRIPS; i++) {
		const double frac = (double(i) + 0.5) / GRADIENT_STRIPS;
		const double alpha = double(base[3]) * (1.0 - frac);
		out.push_back(arc(q, POINT_RADIUS + frac * HOVER_WIDTH, 0.0, rcg::TAU, 48, render::with_alpha(base, alpha), strip));
	}
	for (const V2 &dir : {RIGHT_V, DOWN, LEFT_V, UP}) {
		out.push_back(polygon(rc::arc::chevron_vertices(q + vs(dir, POINT_RADIUS + INNER_CHEVRON_DIST), -dir, CHEVRON_SIZE), base));
		out.push_back(polygon(rc::arc::chevron_vertices(q + vs(dir, POINT_RADIUS + OUTER_CHEVRON_DIST), dir, CHEVRON_SIZE), base));
	}
}

void point_marker(std::vector<Call> &out, const V2 &q, const Color &color, bool hovered) {
	const double r = POINT_RADIUS * (hovered ? 1.3 : 1.0);
	out.push_back(arc(q, r, 0.0, rcg::TAU, 32, color, 2.0));
	out.push_back(line(q + vs(V2::of(-r, -r), 0.7), q + vs(V2::of(r, r), 0.7), color, 2.0));
	out.push_back(line(q + vs(V2::of(-r, r), 0.7), q + vs(V2::of(r, -r), 0.7), color, 2.0));
}

// ---------------------------------------------------------------- the path (PathRenderer)

Color ghost_color(const Color &col) {
	Color c;
	for (int i = 0; i < 4; i++) {
		c[size_t(i)] = col[size_t(i)] + (WHITE[size_t(i)] - col[size_t(i)]) * 0.6f; // Color.lerp
	}
	c[3] = float(double(col[3]) * 0.45);
	return c;
}

Color step_color(rc::StepType t) {
	switch (t) {
		case rc::StepType::Aligned:
		case rc::StepType::AlignedPostPlanned:
			return GREEN;
		case rc::StepType::DivergedPhysical:
			return YELLOW;
		case rc::StepType::DivergedPlanned:
		case rc::StepType::DivergedPostPlanned:
			return RED;
	}
	return WHITE;
}

bool is_solid(rc::StepType t) { return t == rc::StepType::Aligned || t == rc::StepType::DivergedPlanned; }

// PathRenderer._clip_line_to_rect
bool clip_line(V2 &from, V2 &to, const rc::Rect2f &rect) {
	const double dx = double(to.x) - double(from.x);
	const double dy = double(to.y) - double(from.y);
	const double p[4] = {-dx, dx, -dy, dy};
	const V2 end = rect.position + rect.size;
	const double q[4] = {double(from.x) - double(rect.position.x), double(end.x) - double(from.x), double(from.y) - double(rect.position.y),
		double(end.y) - double(from.y)};
	double t0 = 0.0, t1 = 1.0;
	for (int i = 0; i < 4; i++) {
		if (p[i] == 0.0) {
			if (q[i] < 0.0) {
				return false;
			}
		} else if (p[i] < 0.0) {
			t0 = std::max(t0, q[i] / p[i]);
		} else {
			t1 = std::min(t1, q[i] / p[i]);
		}
	}
	if (t0 > t1) {
		return false;
	}
	const V2 d = V2::of(dx, dy);
	const V2 c0 = from + vs(d, t0);
	const V2 c1 = from + vs(d, t1);
	from = c0;
	to = c1;
	return true;
}

struct PathPainter {
	const rc::Rect2f bounds; // the draw bounds: the level's, grown by 50
	std::vector<Call> &out;

	void step(const rc::TraceStep &st, const Color &col, bool dashed_step) {
		const bool arcs = st.is_arc_step && rc::arc::is_arc(st.start, st.via, st.end);
		if (arcs) {
			const rc::arc::ArcParams p = rc::arc::arc_params(st.start, st.via, st.end);
			const V2 center = V2::of(double(p.center.x), double(p.center.y));
			if (dashed_step) {
				const std::vector<V2> pts = rc::arc::dash_arc(center, p.radius, p.start_angle, p.end_angle, p.point_count, 10.0, 5.0, 4000.0);
				if (!pts.empty()) {
					out.push_back(stroke(Kind::Multiline, pts, col, 2.0));
				}
			} else {
				out.push_back(arc(center, p.radius, p.start_angle, p.end_angle, p.point_count, col, 2.0));
			}
			return;
		}
		V2 from = st.start, to = st.end;
		if (!clip_line(from, to, bounds)) {
			return;
		}
		if (dashed_step) {
			const std::vector<V2> pts = rc::arc::dash_line(from, to, 10.0, 5.0, 4000.0);
			if (!pts.empty()) {
				out.push_back(stroke(Kind::Multiline, pts, col, 2.0));
			}
		} else {
			out.push_back(line(from, to, col, 2.0));
		}
	}

	void endpoint_circles(const std::vector<rc::TraceStep> &steps, bool by_type, const Color &base) {
		for (const rc::TraceStep &st : steps) {
			Color col = by_type ? step_color(st.type) : base;
			col[3] = 0.4f;
			if (bounds.has_point(st.start)) {
				out.push_back(circle(st.start, 4.0, col));
			}
			if (bounds.has_point(st.end)) {
				out.push_back(circle(st.end, 4.0, col));
			}
		}
	}

	void stop_marker(const V2 &at, const Color &col) {
		if (!bounds.has_point(at)) {
			return;
		}
		std::vector<V2> pts;
		for (int k = 0; k < 9; k++) {
			const float a = float(PI / 8.0 + k * PI / 4.0);
			pts.push_back(at + V2{std::cos(a), std::sin(a)} * 7.0f);
		}
		out.push_back(stroke(Kind::Polyline, pts, col, 2.0));
		out.push_back(line(at + V2{-0.5f, 0.0f} * 7.0f, at + V2{0.5f, 0.0f} * 7.0f, col, 2.0));
	}

	void raw(const rc::TracePath &path, const Color &base, const std::string &prefix) {
		if (path.steps.empty()) {
			return;
		}
		const size_t ci = path.cursor_index >= 0 ? size_t(path.cursor_index) : path.steps.size();
		for (size_t i = 0; i < path.steps.size(); i++) {
			const rc::TraceStep &st = path.steps[i];
			if (st.start == st.end) {
				continue;
			}
			Color col = base;
			if (i >= ci) {
				col = render::with_alpha(base, 0.35);
			}
			if (st.ghost) {
				col = ghost_color(col);
			}
			step(st, col, !st.hit.has_value());
		}
		endpoint_circles(path.steps, false, base);
		for (size_t i = 0; i < path.steps.size(); i++) {
			const rc::TraceStep &st = path.steps[i];
			const V2 mid = (st.start + st.end) / 2.0f + V2{0, -10};
			if (bounds.has_point(mid)) {
				out.push_back(text(mid, prefix + std::to_string(i) + (st.hit ? "" : "*"), 1, 11, render::with_alpha(base, 0.4)));
			}
		}
	}
};

} // namespace

std::vector<render::Node> Session::draw() const {
	// in the level's tree order (a scene's nodes, then the ones its _ready adds):
	// the order the engine draws nodes of one z-index in
	std::vector<render::Node> nodes;
	const rc::Rect2f b = bounds();
	// the background, by its properties
	if (level_.background) {
		const Level::Background &bg = *level_.background;
		Call k;
		k.kind = Kind::Rect;
		k.pts = {V2::of(bg.rect.x, bg.rect.y), V2::of(bg.rect.w, bg.rect.h)};
		k.colors = {rgba(bg.color[0], bg.color[1], bg.color[2], bg.color[3])};
		nodes.push_back(render::Node{{"background"}, 0, V2::of(bg.rect.x, bg.rect.y), WHITE, {k}});
	}
	if (level_.player_start) {
		Call k;
		k.kind = Kind::Polygon2D;
		k.pts = {V2{24, 0}, V2{-14.4f, -16.8f}, V2{-14.4f, 16.8f}};
		k.colors = {WHITE};
		k.rotation = double(facing_);
		nodes.push_back(render::Node{{"player"}, 0, player_, WHITE, {k}});
	}
	if (level_.features.cursor) {
		nodes.push_back(render::Node{{"cursor"}, 0, cursor_, WHITE,
			{line(V2{-10, 0}, V2{10, 0}, WHITE, 1.5), line(V2{0, -10}, V2{0, 10}, WHITE, 1.5)}});
	}
	const Color dim{1, 1, 1, flying_ ? 0.25f : 1.0f};
	if (level_.features.preview) {
		render::Node path{{"path"}, 20, V2{}, dim, {}};
		PathPainter pp{b.grow(50.0f), path.calls};
		if (preview_.has_paths || !preview_.merged.empty()) {
			if (display_mode_ == 0) {
				if (!preview_.merged.empty()) {
					for (const rc::TraceStep &ms : preview_.merged) {
						if (ms.start == ms.end) {
							continue;
						}
						const Color col = step_color(ms.type);
						pp.step(ms, ms.ghost ? ghost_color(col) : col, !is_solid(ms.type));
					}
					pp.endpoint_circles(preview_.merged, true, WHITE);
					for (const auto &[pt, col] : truncation_markers()) {
						pp.stop_marker(pt, col);
					}
				}
			} else if (preview_.has_paths) {
				if (display_mode_ == 1) {
					pp.raw(preview_.physical, CYAN, "P");
				} else {
					pp.raw(preview_.planned, MAGENTA, "L");
				}
			}
		}
		nodes.push_back(std::move(path));
	}
	if (level_.features.arrow && flying_) { // (the arrow's node is hidden between flights)
		render::Node a{{"arrow"}, 30, V2{}, WHITE, {}};
		const V2 tip = arrow_pos_;
		const V2 tail = tip - arrow_dir_ * 40.0f;
		a.calls.push_back(line(tail, tip, WHITE, 2.0));
		const float angle = float(30.0 * (PI / 180.0)); // deg_to_rad
		const V2 hl = tip - arrow_dir_.rotated(angle) * 16.0f;
		const V2 hr = tip - arrow_dir_.rotated(-angle) * 16.0f;
		a.calls.push_back(polygon({tip, hl, hr}, WHITE));
		nodes.push_back(std::move(a));
	}
	// the HUD (a Label at (10, 10) of a Control at the origin)
	if (level_.features.hud) {
		Call k;
		k.kind = Kind::Label;
		k.text = hud_;
		nodes.push_back(render::Node{{"hud"}, 0, V2{}, WHITE, {k}});
	}
	// the surfaces, one node per piece
	for (const Node &n : nodes_) {
		const BuiltSurface *s = world_->by_id(n.surface);
		if (s == nullptr) {
			continue;
		}
		render::Node node{{"surface", n.surface, n.piece}, 0, V2{}, WHITE, {}};
		SurfacePainter{*s, n.segment, n.piece, flags_, node.calls}.draw(n.hover, n.plan, n.plan_indices);
		nodes.push_back(std::move(node));
	}
	// the edge
	{
		render::Node node{{"edge"}, 5, V2{}, WHITE, {}};
		std::vector<Call> &out = node.calls;
		for (const auto &m : edge_marks_) {
			const Color color = m.valid ? PLAN_VALID_COLOR : PLAN_INVALID_COLOR;
			const V2 mp = m.point;
			const std::string idx = std::to_string(m.index);
			if (m.on_point) {
				point_overlay(out, mp, color);
				point_marker(out, mp, render::with_alpha(color, 0.9), false);
				out.push_back(text(mp + V2::of(POINT_RADIUS + OUTER_CHEVRON_DIST, -POINT_RADIUS - 4.0), idx, 0, 14, WHITE));
				continue;
			}
			// EdgeOverlayNode.snap_to_edge
			const V2 end = b.position + b.size;
			struct Cand {
				V2 point, out;
				double dist;
			};
			const Cand cands[4] = {
				{V2{mp.x, b.position.y}, UP, std::abs(double(mp.y) - double(b.position.y))},
				{V2{mp.x, end.y}, DOWN, std::abs(double(end.y) - double(mp.y))},
				{V2{b.position.x, mp.y}, LEFT_V, std::abs(double(mp.x) - double(b.position.x))},
				{V2{end.x, mp.y}, RIGHT_V, std::abs(double(end.x) - double(mp.x))},
			};
			Cand best = cands[0];
			for (const Cand &c : cands) {
				if (c.dist < best.dist) {
					best = c;
				}
			}
			V2 pt = best.point;
			pt.x = float(std::clamp(double(pt.x), double(b.position.x), double(end.x)));
			pt.y = float(std::clamp(double(pt.y), double(b.position.y), double(end.y)));
			const V2 o = best.out;
			const V2 along{-o.y, o.x};
			edge_gradient(out, pt - vs(along, MARK_HALF_LENGTH), pt + vs(along, MARK_HALF_LENGTH), o, color);
			edge_chevrons(out, pt, o, color);
			out.push_back(text(pt - vs(o, HOVER_WIDTH + 14.0) + V2{-5, 5}, idx, 1, 14, WHITE));
		}
		if (edge_point_) {
			if (edge_hovered_) {
				point_overlay(out, *edge_point_, HOVER_COLOR);
			}
			point_marker(out, *edge_point_, POINT_COLOR, edge_hovered_);
		} else if (edge_hovered_) {
			const V2 end = b.position + b.size;
			const V2 sides[4][3] = {{b.position, V2{end.x, b.position.y}, UP}, {V2{end.x, b.position.y}, end, RIGHT_V},
				{end, V2{b.position.x, end.y}, DOWN}, {V2{b.position.x, end.y}, b.position, LEFT_V}};
			for (const auto &sd : sides) {
				edge_gradient(out, sd[0], sd[1], sd[2], HOVER_COLOR);
				edge_chevrons(out, (sd[0] + sd[1]) * 0.5f, sd[2], HOVER_COLOR);
			}
		}
		nodes.push_back(std::move(node));
	}
	if (level_.features.preview) {
		// the visibility region
		render::Node vis{{"visibility"}, 10, V2{}, dim, {}};
		if (region_ && region_->supported && !fill_.indices.empty()) {
			Call k;
			k.kind = Kind::Triangles;
			for (const rc::vis::Pt &p : fill_.points) {
				k.pts.push_back(V2::of(p.x, p.y));
			}
			for (uint32_t i : fill_.indices) {
				k.indices.push_back(int(i));
			}
			k.colors = {FILL};
			vis.calls.push_back(k);
		}
		const Color whisker{FILL[0] * 1.0f, FILL[1] * 1.0f, FILL[2] * 1.0f, FILL[3] * 3.0f};
		for (const std::vector<V2> &l : whiskers_) {
			vis.calls.push_back(stroke(Kind::Polyline, l, whisker, 1.5));
		}
		nodes.push_back(std::move(vis));
	}
	if (spawn_preview_) {
		render::Node sp{{"spawn"}, 19, V2{}, dim, {}};
		for (const Placed &p : spawn_specs_) {
			dashed(sp.calls, segment_of(p.spec), GHOST_SPAWN_COLOR, 2.0);
			if (p.spec.is_portal()) {
				dashed(sp.calls, build_spec(p.spec, 0, std::nullopt).at(1).seg, GHOST_SPAWN_COLOR, 2.0);
			}
		}
		for (int id : spawn_vanishing_) {
			if (const BuiltSurface *s = world_->by_id(id)) {
				dashed(sp.calls, s->seg, VANISH_COLOR, 2.0);
			}
		}
		nodes.push_back(std::move(sp));
	}
	return nodes;
}

} // namespace rcg
