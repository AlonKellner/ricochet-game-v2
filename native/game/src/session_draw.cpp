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
#include <functional>
#include <map>

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
// Stage 130 (the user, 2026-10-22): a solid preview step is a slightly dark
// stroke with a slightly bright dashed stroke over it
const Color GREEN_DARK = render::hex(0x00B300FF);
const Color GREEN_BRIGHT = render::hex(0x80FF80FF);
const Color RED_DARK = render::hex(0xB30000FF);
const Color RED_BRIGHT = render::hex(0xFF8080FF);

Color rgba(double r, double g, double b, double a = 1.0) { return {float(r), float(g), float(b), float(a)}; }

const Color GHOST_TOGGLER_COLOR = rgba(0.75, 0.95, 1.0);
const Color FLAG_TOGGLER_COLOR = rgba(1.0, 0.85, 0.2);
const Color PRE_TRANSFORM_COLOR = rgba(0.55, 0.45, 0.85, 0.35);
const Color ABSENT_COLOR = rgba(0.6, 0.6, 0.6, 0.35);
const Color PLATFORM_COLOR = rgba(0.62, 0.42, 0.22); // Stage 172: wood brown
const double DESIGNER_MARKER_SIZE = 7.0;               // Stage 179: a designer's marker's radius
const double ABSENT_MARKER_ALPHA = 0.4;                // ... on an absent surface
const Color BODY_COLOR = rgba(0.8, 0.8, 0.8);        // Stage 173: the tall player's body under its triangle
const double SHAPE_HITBOX_ALPHA = 0.3;                 // Stage 181: a shaped object's circle under its outline
// the player's triangle (about its origin, turned by its facing) and, Stage 173 (the user): under gravity a
// rectangle from the triangle down to the feet (it does not turn)
const std::vector<V2> PLAYER_HEAD = {V2{24, 0}, V2{-14.4f, -16.8f}, V2{-14.4f, 16.8f}};
const std::vector<V2> PLAYER_BODY = {V2{-8, 0}, V2{8, 0}, V2{8, float(kinematics::BODY + kinematics::RADIUS)}, V2{-8, float(kinematics::BODY + kinematics::RADIUS)}};

// Stage 181: the player at `at` as polygons -- its body (when tall), then its triangle
std::vector<std::vector<V2>> player_outline(const V2 &at, double facing, bool tall) {
	std::vector<std::vector<V2>> out;
	if (tall) {
		std::vector<V2> body;
		for (const V2 &q : PLAYER_BODY) {
			body.push_back(V2::of(double(at.x) + q.x, double(at.y) + q.y));
		}
		out.push_back(std::move(body));
	}
	const double c = std::cos(facing), sn = std::sin(facing);
	std::vector<V2> head;
	for (const V2 &q : PLAYER_HEAD) {
		head.push_back(V2::of(double(at.x) + c * q.x - sn * q.y, double(at.y) + sn * q.x + c * q.y));
	}
	out.push_back(std::move(head));
	return out;
}
// Stage 153: the shadow, a dark shade's edge, a light one's
const Color SHADOW_FILL = rgba(0.02, 0.02, 0.05, 0.82);
const Color SHADE_DARK = rgba(0.55, 0.45, 0.95);
const Color SHADE_LIGHT = rgba(1.0, 0.85, 0.35);
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
// Stage 145: the goal and the detectors
const Color GOAL_COLOR = rgba(0.3, 0.95, 0.45);
const Color GOAL_WON_FILL = rgba(0.3, 0.95, 0.45, 0.25);
const Color DETECTOR_COLOR = rgba(1.0, 0.8, 0.25, 0.8);
const Color DETECTOR_FILL = rgba(1.0, 0.8, 0.25, 0.18);

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
Call polygon_n(const std::vector<V2> &pts, const Color &c) {
	Call k;
	k.kind = Kind::Polygon;
	k.pts = pts;
	k.colors.assign(pts.size(), c);
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

// Stage 157: a surface's outline, solid or dashed (a glow under or over it)
void outline_seg(std::vector<Call> &out, const SegGeom &seg, const Color &color, double width, bool dash) {
	if (dash) {
		dashed(out, seg, color, width);
		return;
	}
	if (seg.is_line()) {
		out.push_back(line(seg.s, seg.e, color, width));
		return;
	}
	const rc::arc::GeneralizedCircle &g = seg.get_carrier();
	const V2 c = rc::arc::center_of(g);
	const double r = g.radius();
	if (r > rc::arc::MAX_ARC_RADIUS && !seg.full) {
		out.push_back(line(seg.s, seg.e, color, width));
		return;
	}
	double a0 = 0.0, span = rcg::TAU;
	if (!seg.full) {
		a0 = double((seg.s - c).angle());
		const double am = wrapf(double((seg.v - c).angle()) - a0, 0.0, rcg::TAU);
		const double a1 = wrapf(double((seg.e - c).angle()) - a0, 0.0, rcg::TAU);
		span = am < a1 ? a1 : a1 - rcg::TAU;
	}
	out.push_back(arc(c, r, a0, a0 + span, std::clamp(int(std::abs(span) * r / 6.0), 8, 128), color, width));
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

	// Stage 181: an object drawn in its shape (the user): the outline, and its circle (what it collides as) faint
	bool shaped() const { return seg.full && !surface.shape.empty() && surface.shape != "circle"; }
	void shape_stroke(const Color &color, double width) {
		const ArcView a = arc_view();
		out.push_back(arc(a.ctr, a.r, 0.0, rcg::TAU, a.pc, render::with_alpha(color, double(color[3]) * SHAPE_HITBOX_ALPHA), 1.0));
		std::vector<V2> pts = render::shape_outline(surface.shape, a.ctr, a.r);
		pts.push_back(pts.front());
		out.push_back(stroke(Kind::Polyline, std::move(pts), color, width));
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

	// Stage 179: the designer's marker, on the surface's middle (a circle's top), once (on its first piece); faded
	// while the surface is absent (an open door still shows what opened it)
	void designer_marker(double alpha) {
		if (!surface.marker || piece_index > 0) {
			return;
		}
		render::marker(out, surface.marker->shape, surface.marker->color, marker_point(surface.seg), DESIGNER_MARKER_SIZE, alpha);
	}

	void draw(const std::vector<Overlay> &hover, const std::vector<Overlay> &plan, const std::vector<int> &plan_indices) {
		if (surface.pre_transform) {
			const rc::arc::GeneralizedCircle &g = seg.get_carrier();
			out.push_back(arc(rc::arc::center_of(g), g.radius(), 0.0, rcg::TAU, rc::arc::POINTS_PER_FULL_CIRCLE, PRE_TRANSFORM_COLOR, 1.0));
			return;
		}
		if (!present(surface, flags)) {
			dashed(out, seg, ABSENT_COLOR, 1.5);
			designer_marker(ABSENT_MARKER_ALPHA);
			return;
		}
		const BuiltSide *left = active_side(surface, Side::Left, flags);
		const BuiltSide *right = active_side(surface, Side::Right, flags);
		Color lc = config_color(left), rc_ = config_color(right);
		if (surface.platform) { // Stage 172: a platform's plain sides in its own colour
			lc = lc == GRAY ? PLATFORM_COLOR : lc;
			rc_ = rc_ == GRAY ? PLATFORM_COLOR : rc_;
		}
		const bool arcs = !seg.is_line();
		const bool l_outer = left_outer();
		if (shaped()) {
			shape_stroke(l_outer || lc == rc_ ? lc : rc_, LINE_WIDTH);
		} else if (lc == rc_) {
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
		designer_marker(1.0);
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

// Stage 130: the dashes over a solid step, and the solid stroke under them
Color solid_under(rc::StepType t) { return t == rc::StepType::Aligned ? GREEN_DARK : RED_DARK; }
Color dashes_over(rc::StepType t) { return t == rc::StepType::Aligned ? GREEN_BRIGHT : RED_BRIGHT; }

// Stage 130: steps drawn on one another are set side by side -- a step moved
// sideways (a line along its group's left normal; an arc to a larger or a
// smaller radius on its circle)
struct Shift {
	V2 by;          // a line's
	double dr = 0.0; // an arc's
	double off = 0.0; // the lane's offset, px to the left of its group's way
};
constexpr double SIDE_BY_SIDE = 3.5; // px between the centres of two strokes (2 px wide: a 1.5 px margin)

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
	// a portal surface at a point (pair: its pair, where the beam comes out): its
	// unit normal there, and its circle when it is an arc (the user: a half ring
	// normal to a line portal; on a circular one, the part of the ring on the
	// beam's side of the circle)
	struct PortalShape {
		V2 normal;
		bool arc = false;
		V2 center;
		double radius = 0.0;
	};
	std::function<std::optional<PortalShape>(int sid, const V2 &at, bool pair)> portal_shape = {};

	void step(const rc::TraceStep &st, const Color &col, bool dashed_step, double phase = 0.0, const Shift &sh = {}) {
		const bool arcs = st.is_arc_step && rc::arc::is_arc(st.start, st.via, st.end);
		if (arcs) {
			const rc::arc::ArcParams p = rc::arc::arc_params(st.start, st.via, st.end);
			const V2 center = V2::of(double(p.center.x), double(p.center.y));
			const double r = p.radius + sh.dr;
			if (dashed_step) {
				const std::vector<V2> pts = rc::arc::dash_arc(center, r, p.start_angle, p.end_angle, p.point_count, 10.0, 5.0, 4000.0, phase);
				if (!pts.empty()) {
					out.push_back(stroke(Kind::Multiline, pts, col, 2.0));
				}
			} else {
				out.push_back(arc(center, r, p.start_angle, p.end_angle, p.point_count, col, 2.0));
			}
			return;
		}
		V2 from = st.start + sh.by, to = st.end + sh.by;
		const V2 drawn_from = from;
		if (!clip_line(from, to, bounds)) {
			return;
		}
		if (dashed_step) {
			// (the pattern runs on from where the step begins, not where it is clipped)
			const double clipped = double(drawn_from.distance_to(from));
			const std::vector<V2> pts = rc::arc::dash_line(from, to, 10.0, 5.0, 4000.0, phase - clipped);
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

	// Stage 131: the merged path's event marks -- a significant event (an applied
	// effect, a firing state change, a passage of infinity: the ten a shot may
	// make) a ringed dot in its step's colour, any other crossing a small dim dot.
	// A mark sits at its step's end as the step is drawn (side by side with the
	// marks of steps drawn on one another: a loop's repeats). A passage of an
	// infinity point off the level (the real infinity of a straight beam, or a
	// circle beam's Q outside the bounds) is marked twice: where the outgoing step
	// leaves the level's bounds and where the incoming one enters them -- each
	// half outside, together the one mark.
	void ring(const V2 &at, const rc::TraceStep &st) {
		Color col = step_color(st.type);
		if (st.ghost) {
			col = ghost_color(col);
		}
		out.push_back(circle(at, 5.5, st.ghost ? ghost_color(WHITE) : WHITE));
		out.push_back(circle(at, 4.0, col));
	}

	// half a ring: the half facing `face` (a passage of infinity off the level, a
	// portal: one half where the beam goes out, the other where it comes back in)
	void half_ring(const V2 &at, const V2 &face, const rc::TraceStep &st, const std::optional<PortalShape> &circle = std::nullopt) {
		Color col = step_color(st.type);
		if (st.ghost) {
			col = ghost_color(col);
		}
		const double a0 = std::atan2(double(face.y), double(face.x));
		auto half = [&](double r) {
			std::vector<V2> pts{at};
			if (circle && circle->arc && circle->radius > r) {
				// on a circular portal: the ring's arc between the two points where it
				// meets the portal's circle, on the face's side, closed along the circle
				const V2 c = circle->center;
				const double R = circle->radius;
				const double al = std::atan2(double(at.y - c.y), double(at.x - c.x)); // outward at `at`
				const bool outside = double(face.x) * std::cos(al) + double(face.y) * std::sin(al) >= 0.0;
				const double w = std::acos(-r / (2.0 * R)); // the ring meets the circle at al +- w
				const double mid = outside ? al : al + PI;
				const double span = outside ? w : PI - w;
				auto on_circle = [&](double th) { return V2::of(double(c.x) + R * std::cos(th), double(c.y) + R * std::sin(th)); };
				const V2 e0 = at + V2::of(r * std::cos(mid - span), r * std::sin(mid - span));
				const V2 e1 = at + V2::of(r * std::cos(mid + span), r * std::sin(mid + span));
				const double b0 = std::atan2(double(e0.y - c.y), double(e0.x - c.x));
				const double b1 = std::atan2(double(e1.y - c.y), double(e1.x - c.x));
				auto wrap = [](double d) { return std::remainder(d, 2.0 * PI); };
				for (int k = 1; k <= 4; k++) { // along the circle, from `at` to the ring's first end
					pts.push_back(on_circle(al + wrap(b0 - al) * k / 4.0));
				}
				for (int k = 1; k < 12; k++) {
					const double a = mid - span + 2.0 * span * k / 12.0;
					pts.push_back(at + V2::of(r * std::cos(a), r * std::sin(a)));
				}
				for (int k = 0; k < 4; k++) { // and back along the circle to `at`
					pts.push_back(on_circle(b1 + wrap(al - b1) * k / 4.0));
				}
				return pts;
			}
			for (int k = 0; k <= 12; k++) {
				const double a = a0 - PI / 2.0 + PI * k / 12.0;
				pts.push_back(at + V2::of(r * std::cos(a), r * std::sin(a)));
			}
			return pts;
		};
		out.push_back(polygon_n(half(5.5), st.ghost ? ghost_color(WHITE) : WHITE));
		out.push_back(polygon_n(half(4.0), col));
	}

	// the step's way at its start or its end (a unit vector)
	static V2 way_at(const rc::TraceStep &st, bool at_end) {
		if (st.is_arc_step && rc::arc::is_arc(st.start, st.via, st.end)) {
			const rc::arc::ArcParams p = rc::arc::arc_params(st.start, st.via, st.end);
			const double th = at_end ? p.end_angle : p.start_angle;
			const double sgn = p.end_angle >= p.start_angle ? 1.0 : -1.0;
			return V2::of(-std::sin(th) * sgn, std::cos(th) * sgn);
		}
		const V2 d = st.end - st.start;
		return d / d.length();
	}

	// where the step as drawn (shifted) crosses the level's bounds: leaving them
	// (inside to outside), or entering them (the last outside-to-inside)
	static std::optional<V2> bounds_crossing(const rc::TraceStep &st, const Shift &sh, const rc::Rect2f &level, bool leaving) {
		if (st.is_arc_step && rc::arc::is_arc(st.start, st.via, st.end)) {
			const rc::arc::ArcParams p = rc::arc::arc_params(st.start, st.via, st.end);
			const double r = p.radius + sh.dr;
			auto at = [&](double u) {
				const double th = p.start_angle + (p.end_angle - p.start_angle) * u;
				return V2::of(double(p.center.x) + r * std::cos(th), double(p.center.y) + r * std::sin(th));
			};
			constexpr int N = 256;
			auto inside = [&](double u) { return level.has_point(at(u)); };
			auto bisect = [&](double a, double b) { // inside(a) != inside(b)
				const bool ia = inside(a);
				for (int k = 0; k < 40; k++) {
					const double m = (a + b) / 2.0;
					(inside(m) == ia ? a : b) = m;
				}
				return at(ia ? a : b); // (the inside side)
			};
			if (leaving) {
				for (int k = 0; k < N; k++) {
					if (inside(double(k) / N) && !inside(double(k + 1) / N)) {
						return bisect(double(k) / N, double(k + 1) / N);
					}
				}
			} else {
				for (int k = N; k > 0; k--) {
					if (inside(double(k) / N) && !inside(double(k - 1) / N)) {
						return bisect(double(k - 1) / N, double(k) / N);
					}
				}
			}
			return std::nullopt;
		}
		V2 from = st.start + sh.by, to = st.end + sh.by;
		if (level.has_point(leaving ? from : to) == false || !clip_line(from, to, level)) {
			return std::nullopt;
		}
		return leaving ? to : from;
	}

	// the inward normal of the level's edge nearest a point on its bounds
	static V2 inward(const V2 &p, const rc::Rect2f &level) {
		const V2 end = level.position + level.size;
		const double d[4] = {std::abs(double(p.x) - double(level.position.x)), std::abs(double(end.x) - double(p.x)),
			std::abs(double(p.y) - double(level.position.y)), std::abs(double(end.y) - double(p.y))};
		const V2 n[4] = {V2{1, 0}, V2{-1, 0}, V2{0, 1}, V2{0, -1}};
		int best = 0;
		for (int k = 1; k < 4; k++) {
			if (d[k] < d[best]) {
				best = k;
			}
		}
		return n[best];
	}

	// Stage 134 (the user): with `dots` (the merge's), the non-significant events
	// of both walks are dots coloured by the walk that computed them -- yellow the
	// physical, red the planned, green both (for debugging)
	void event_marks(const std::vector<rc::TraceStep> &steps, const std::vector<Shift> &shifts, const rc::Rect2f &level,
			const std::vector<rc::sweep::EventDot> *dots = nullptr) {
		// a point at the end of step i as the step is drawn (side by side)
		auto shifted = [&](size_t i, const V2 &end) {
			const rc::TraceStep &st = steps[i];
			if (st.is_arc_step && rc::arc::is_arc(st.start, st.via, st.end)) {
				const rc::arc::ArcParams p = rc::arc::arc_params(st.start, st.via, st.end);
				const V2 c = V2::of(double(p.center.x), double(p.center.y));
				return c + (end - c) * float((p.radius + shifts[i].dr) / p.radius);
			}
			return end + shifts[i].by;
		};
		if (dots != nullptr) {
			for (const rc::sweep::EventDot &d : *dots) {
				if (d.step < 0 || size_t(d.step) >= steps.size()) {
					continue;
				}
				const V2 at = shifted(size_t(d.step), d.at);
				if (!bounds.has_point(at)) {
					continue;
				}
				Color col = d.from == 3 ? GREEN : (d.from == 2 ? YELLOW : RED);
				if (steps[size_t(d.step)].ghost) {
					col = ghost_color(col);
				}
				col[3] = 0.8f;
				out.push_back(circle(at, 2.0, col));
			}
		}
		for (size_t i = 0; i < steps.size(); i++) {
			const rc::TraceStep &st = steps[i];
			if (st.start == st.end) {
				continue;
			}
			const V2 at = shifted(i, st.end);
			// a portal: at its exit, the half facing the way out -- along the pair's
			// normal (the user)
			if (st.after_portal && bounds.has_point(st.start)) {
				const V2 w = way_at(st, false);
				V2 face = w;
				std::optional<PortalShape> shape;
				if (i > 0 && steps[i - 1].teleports && portal_shape) {
					if ((shape = portal_shape(steps[i - 1].surface_id, st.start, true))) {
						face = shape->normal.dot(w) >= 0.0f ? shape->normal : -shape->normal;
					}
				}
				half_ring(st.start + shifts[i].by, face, st, shape);
			}
			// a passage of infinity off the level (the user: on screen): the step that
			// reaches it, and the step after it, lie off the level -- the outgoing half
			// where the path last leaves the level before the wrap, the incoming one
			// where it first enters again after it, each facing into the level. Stage
			// 136 (the user's F12): off the level's interior -- a wrap's way out is
			// clipped onto the edge, which has_point counts in on the top and left
			if (i + 1 < steps.size() && steps[i + 1].after_infinity && !level.grow(-0.5f).has_point(st.end)) {
				for (size_t j = i + 1; j-- > 0;) {
					if (j < i && steps[j + 1].after_infinity) {
						break; // (the previous wrap)
					}
					if (level.has_point(steps[j].start)) {
						if (const std::optional<V2> outp = bounds_crossing(steps[j], shifts[j], level, true)) {
							half_ring(*outp, inward(*outp, level), steps[j]); // (normal to the edge, like a portal's)
						}
						break;
					}
				}
				for (size_t k = i + 1; k < steps.size(); k++) {
					if (k > i + 1 && steps[k].after_infinity) {
						break; // (the next wrap)
					}
					if (level.has_point(steps[k].end)) {
						if (const std::optional<V2> in = bounds_crossing(steps[k], shifts[k], level, false)) {
							half_ring(*in, inward(*in, level), steps[k]);
						}
						break;
					}
				}
				continue;
			}
			if (st.teleports) {
				// a portal's entry: the half facing back the way the beam came -- along
				// the portal's normal (the user)
				if (bounds.has_point(at)) {
					const V2 w = way_at(st, true);
					V2 face = -w;
					std::optional<PortalShape> shape;
					if (portal_shape) {
						if ((shape = portal_shape(st.surface_id, st.end, false))) {
							face = shape->normal.dot(w) >= 0.0f ? -shape->normal : shape->normal;
						}
					}
					half_ring(at, face, st, shape);
				}
				continue;
			}
			if (!bounds.has_point(at)) {
				continue;
			}
			if (st.significant) {
				ring(at, st);
			} else if (dots == nullptr) {
				Color col = step_color(st.type);
				if (st.ghost) {
					col = ghost_color(col);
				}
				col[3] = 0.5f;
				out.push_back(circle(at, 2.0, col));
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

namespace {

// Stage 130: two steps drawn on one another -- the same carrier (a line, or a
// circle: the same centre and radius, to half a pixel), and some stretch more
// than 2 px long where both run: the same step (either way round), or steps
// with different ends (the planned path cut at the cursor, the physical one
// running on to the next hit)
struct StepGeom {
	bool ok = false;
	bool arcs = false;
	V2 a, b;                  // a line's ends
	rc::arc::ArcParams p;     // an arc's
	double len = 0.0;
};

StepGeom geom_of(const rc::TraceStep &s) {
	StepGeom g;
	const bool finite = std::isfinite(s.start.x) && std::isfinite(s.start.y) && std::isfinite(s.end.x) && std::isfinite(s.end.y);
	if (!finite || s.start == s.end) {
		return g;
	}
	g.ok = true;
	g.arcs = s.is_arc_step && rc::arc::is_arc(s.start, s.via, s.end);
	g.a = s.start;
	g.b = s.end;
	if (g.arcs) {
		g.p = rc::arc::arc_params(s.start, s.via, s.end);
		g.len = g.p.radius * std::abs(g.p.end_angle - g.p.start_angle);
	} else {
		g.len = double((s.end - s.start).length());
	}
	return g;
}

// the point at fraction f along the step
V2 along(const StepGeom &g, double f) {
	if (g.arcs) {
		const double th = g.p.start_angle + (g.p.end_angle - g.p.start_angle) * f;
		return V2::of(double(g.p.center.x) + g.p.radius * std::cos(th), double(g.p.center.y) + g.p.radius * std::sin(th));
	}
	return V2::of(double(g.a.x) + (double(g.b.x) - double(g.a.x)) * f, double(g.a.y) + (double(g.b.y) - double(g.a.y)) * f);
}

// q on the step, more than `inset` px from its ends (q on its carrier, to half a pixel)
bool runs_through(const StepGeom &g, const V2 &q, double inset) {
	if (g.arcs) {
		const double dx = double(q.x) - double(g.p.center.x), dy = double(q.y) - double(g.p.center.y);
		if (std::abs(std::sqrt(dx * dx + dy * dy) - g.p.radius) > 0.5) {
			return false;
		}
		const double sweep = g.p.end_angle - g.p.start_angle;
		double d = std::atan2(dy, dx) - g.p.start_angle;
		const double tau = 2.0 * PI;
		d = sweep >= 0.0 ? std::fmod(std::fmod(d, tau) + tau, tau) : -std::fmod(std::fmod(-d, tau) + tau, tau);
		const double at = d * g.p.radius * (sweep >= 0.0 ? 1.0 : -1.0); // (px along the way)
		return at > inset && at < g.len - inset;
	}
	const double ux = (double(g.b.x) - double(g.a.x)) / g.len, uy = (double(g.b.y) - double(g.a.y)) / g.len;
	const double rx = double(q.x) - double(g.a.x), ry = double(q.y) - double(g.a.y);
	if (std::abs(rx * uy - ry * ux) > 0.5) {
		return false;
	}
	const double at = rx * ux + ry * uy;
	return at > inset && at < g.len - inset;
}

bool on_one_another(const StepGeom &a, const StepGeom &b) {
	if (!a.ok || !b.ok || a.arcs != b.arcs) {
		return false;
	}
	if (a.arcs && ((a.p.center - b.p.center).length() > 0.5f || std::abs(a.p.radius - b.p.radius) > 0.5)) {
		return false;
	}
	// some stretch of one inside the other (sampled), or the same ends: the whole step
	auto near = [](const V2 &p, const V2 &q) { return (p - q).length() <= 0.5f; };
	if ((near(a.a, b.a) && near(a.b, b.b)) || (near(a.a, b.b) && near(a.b, b.a))) {
		return a.arcs ? near(along(a, 0.5), along(b, 0.5)) : true;
	}
	// F26: before sampling, steps no sample of which can lie inside the other (a
	// loop's many steps on one carrier): the same answer, sooner
	if (a.arcs) {
		// the angle ranges on the circle, with a pixel's margin (the centres agree to
		// half a pixel; a sample inside must be 2 px from the other's ends)
		const double tau = 2.0 * PI;
		const double m = 1.0 / std::max(a.p.radius, 1.0);
		auto span = [&](const StepGeom &g) {
			const double sw = g.p.end_angle - g.p.start_angle;
			return sw >= 0.0 ? std::pair{g.p.start_angle - m, sw + 2.0 * m} : std::pair{g.p.end_angle - m, -sw + 2.0 * m};
		};
		const auto [a0, sa] = span(a);
		const auto [b0, sb] = span(b);
		auto inside = [&](double x, double s0, double w) { return std::fmod(std::fmod(x - s0, tau) + tau, tau) <= w; };
		if (sa < tau && sb < tau && !inside(b0, a0, sa) && !inside(a0, b0, sb)) {
			return false;
		}
	} else {
		// no point of q's step can run through p's: q's ends both more than half a
		// pixel off p's line on one side, or both short of 2 px into p's extent at one end
		auto cannot = [](const StepGeom &p, const StepGeom &q) {
			const double ux = (double(p.b.x) - double(p.a.x)) / p.len, uy = (double(p.b.y) - double(p.a.y)) / p.len;
			auto off = [&](const V2 &v) { return (double(v.x) - double(p.a.x)) * uy - (double(v.y) - double(p.a.y)) * ux; };
			auto at = [&](const V2 &v) { return (double(v.x) - double(p.a.x)) * ux + (double(v.y) - double(p.a.y)) * uy; };
			const double o0 = off(q.a), o1 = off(q.b), t0 = at(q.a), t1 = at(q.b);
			return (o0 > 0.5 && o1 > 0.5) || (o0 < -0.5 && o1 < -0.5) || (t0 <= 2.0 && t1 <= 2.0) || (t0 >= p.len - 2.0 && t1 >= p.len - 2.0);
		};
		if (cannot(a, b) && cannot(b, a)) {
			return false;
		}
	}
	for (int k = 1; k < 16; k++) {
		if (runs_through(a, along(b, k / 16.0), 2.0) || runs_through(b, along(a, k / 16.0), 2.0)) {
			return true;
		}
	}
	return false;
}

// a step's lane: the planned path's steps on the left of a group's way, the
// physical path's on the right
bool physical_lane(rc::StepType t) { return t == rc::StepType::DivergedPhysical; }

std::vector<Shift> side_by_side(const std::vector<rc::TraceStep> &steps) {
	const size_t n = steps.size();
	std::vector<Shift> out(n);
	std::vector<StepGeom> g(n);
	for (size_t i = 0; i < n; i++) {
		g[i] = geom_of(steps[i]);
	}
	// Stage 135 (the user): lanes by passes, not steps. A pass is a run of one
	// walk's consecutive steps that go on from one another along one carrier
	// (through the cursor, the crossings that fire nothing, the ray's origin), the
	// same way round, no step on another of it. Each pass keeps one lane; a
	// group's lanes are packed (as many as its passes run together) and centred
	// on the path, the planned walk's on the left and the physical walk's on the right
	auto way = [&](size_t i) { // the step's unit way at its start and its end
		const StepGeom &st = g[i];
		if (st.arcs) {
			const double dir = st.p.end_angle > st.p.start_angle ? 1.0 : -1.0;
			auto at = [&](double th) { return V2::of(-std::sin(th) * dir, std::cos(th) * dir); };
			return std::pair{at(st.p.start_angle), at(st.p.end_angle)};
		}
		const V2 d0 = (st.b - st.a) / float(st.len);
		return std::pair{d0, d0};
	};
	auto one_carrier = [&](size_t i, size_t j) {
		const StepGeom &a = g[i], &b = g[j];
		if (a.arcs != b.arcs) {
			return false;
		}
		if (a.arcs) {
			return (a.p.center - b.p.center).length() <= 0.5f && std::abs(a.p.radius - b.p.radius) <= 0.5;
		}
		const double ux = (double(a.b.x) - double(a.a.x)) / a.len, uy = (double(a.b.y) - double(a.a.y)) / a.len;
		return std::abs((double(b.b.x) - double(a.a.x)) * uy - (double(b.b.y) - double(a.a.y)) * ux) <= 0.5;
	};
	std::vector<int> pass_of(n, -1);
	std::vector<std::vector<size_t>> passes;
	for (size_t i = 0; i < n; i++) {
		if (pass_of[i] >= 0 || !g[i].ok) {
			continue;
		}
		passes.emplace_back();
		const int pi = int(passes.size()) - 1;
		size_t cur = i;
		while (true) {
			pass_of[cur] = pi;
			passes.back().push_back(cur);
			// the walk's next step (the same lane class)
			size_t nx = n;
			for (size_t j = cur + 1; j < n && nx == n; j++) {
				if (physical_lane(steps[j].type) == physical_lane(steps[cur].type)) {
					nx = j;
				}
			}
			if (nx == n || pass_of[nx] >= 0 || !g[nx].ok || (g[nx].a - g[cur].b).length() > 0.5f || !one_carrier(cur, nx)
					|| way(cur).second.dot(way(nx).first) <= 0.0f) {
				break; // (not where it ended, on another carrier, or back the way it came)
			}
			bool overlaps = false;
			for (size_t q : passes.back()) {
				overlaps = overlaps || on_one_another(g[q], g[nx]);
			}
			if (overlaps) {
				break; // (round the loop again: a pass of its own)
			}
			cur = nx;
		}
	}
	// the groups: passes drawn on one another, transitively
	const size_t np = passes.size();
	std::vector<std::vector<char>> on(np, std::vector<char>(np, 0));
	for (size_t a = 0; a < np; a++) {
		for (size_t b = a + 1; b < np; b++) {
			bool hit = false;
			for (size_t x : passes[a]) {
				for (size_t y : passes[b]) {
					if (!hit && on_one_another(g[x], g[y])) {
						hit = true;
					}
				}
			}
			on[a][b] = on[b][a] = hit ? 1 : 0;
		}
	}
	std::vector<size_t> root(np);
	for (size_t i = 0; i < np; i++) {
		root[i] = i;
	}
	std::function<size_t(size_t)> find = [&](size_t i) { return root[i] == i ? i : root[i] = find(root[i]); };
	for (size_t a = 0; a < np; a++) {
		for (size_t b = a + 1; b < np; b++) {
			if (on[a][b] && find(a) != find(b)) {
				root[find(b)] = find(a);
			}
		}
	}
	std::map<size_t, std::vector<size_t>> groups;
	for (size_t a = 0; a < np; a++) {
		groups[find(a)].push_back(a);
	}
	for (const auto &[r, members] : groups) {
		if (members.size() < 2) {
			continue;
		}
		const StepGeom &ref = g[passes[members[0]][0]];
		const V2 d = ref.b - ref.a;
		const V2 left = V2{d.y, -d.x} / d.length(); // (y down: the left of the way)
		double sign = 1.0; // an arc: moving left grows the radius
		if (ref.arcs) {
			const double a0 = ref.p.start_angle;
			const double dir = ref.p.end_angle > ref.p.start_angle ? 1.0 : -1.0; // (the angle grows along the way)
			const V2 wy = V2::of(-std::sin(a0) * dir, std::cos(a0) * dir);
			const V2 l = V2{wy.y, -wy.x};
			sign = l.dot(V2::of(std::cos(a0), std::sin(a0))) > 0.0f ? 1.0 : -1.0;
		}
		// the lanes: in path order, each pass the lowest lane no pass on it holds;
		// the planned walk's lanes first (on the left), then the physical walk's
		std::map<size_t, int> lane;
		int used = 0;
		for (const bool phys : {false, true}) {
			const int base = used;
			for (size_t a : members) {
				if (physical_lane(steps[passes[a][0]].type) != phys) {
					continue;
				}
				std::vector<char> taken;
				for (const auto &[b, lb] : lane) {
					if (lb >= base && on[a][b]) {
						taken.resize(std::max(taken.size(), size_t(lb - base + 1)), 0);
						taken[size_t(lb - base)] = 1;
					}
				}
				int l = 0;
				while (size_t(l) < taken.size() && taken[size_t(l)]) {
					l++;
				}
				lane[a] = base + l;
				used = std::max(used, base + l + 1);
			}
		}
		for (size_t a : members) {
			const double off = ((double(used) - 1.0) / 2.0 - double(lane[a])) * SIDE_BY_SIDE;
			for (size_t i : passes[a]) {
				out[i].by = left * float(off);
				out[i].dr = off * sign;
				out[i].off = off;
			}
		}
	}
	return out;
}

// a step's length as drawn (an arc at its lane's radius; 0 for a step to or from
// infinity)
double drawn_length(const rc::TraceStep &st, const Shift &sh) {
	if (st.is_arc_step && rc::arc::is_arc(st.start, st.via, st.end)) {
		const rc::arc::ArcParams p = rc::arc::arc_params(st.start, st.via, st.end);
		return std::abs(p.span) * (p.radius + sh.dr);
	}
	const double d = double(st.start.distance_to(st.end));
	return std::isfinite(d) ? d : 0.0;
}

// Stage 136 (the user): each step's distance from the player along its walk, as
// drawn -- the common part's steps in turn, each branch past the divergence on
// from the common part's length (the merge lists the common part first)
// Stage 157: the arrow's states along a step, moving with its dashes (every MARK_PERIOD px of the walk): fire a
// red cross, charge a white chevron pointing the way it flies, frost a pale blue star
constexpr double MARK_PERIOD = 45.0;
const Color MARK_FIRE = rgba(1.0, 0.3, 0.15);
const Color MARK_CHARGE = rgba(1.0, 1.0, 1.0);
const Color MARK_FROST = rgba(0.6, 0.85, 1.0);
void arrow_marks(std::vector<Call> &out, const rc::TraceStep &st, uint8_t mask, double phase, const Shift &sh, const rc::Rect2f &draw) {
	const bool arcs = st.is_arc_step && rc::arc::is_arc(st.start, st.via, st.end);
	double len = 0.0;
	rc::arc::ArcParams ap;
	V2 center;
	double r = 0.0, span = 0.0;
	if (arcs) {
		ap = rc::arc::arc_params(st.start, st.via, st.end);
		center = V2::of(double(ap.center.x), double(ap.center.y));
		r = ap.radius + sh.dr;
		span = ap.end_angle - ap.start_angle;
		len = std::abs(span) * r;
	} else {
		len = double(st.start.distance_to(st.end));
	}
	if (!(len > 0.0) || len > 1e6) {
		return; // (a step far off the drawn bounds: no marks -- the loop below stays bounded)
	}
	auto at = [&](double t, V2 &p, V2 &dir) {
		if (arcs) {
			const double a = ap.start_angle + span * (t / len);
			p = V2::of(double(center.x) + r * std::cos(a), double(center.y) + r * std::sin(a));
			const double sgn = span >= 0 ? 1.0 : -1.0;
			dir = V2::of(-std::sin(a) * sgn, std::cos(a) * sgn);
		} else {
			const double f = t / len;
			p = V2::of(double(st.start.x) + (double(st.end.x) - st.start.x) * f + sh.by.x, double(st.start.y) + (double(st.end.y) - st.start.y) * f + sh.by.y);
			dir = V2::of((double(st.end.x) - st.start.x) / len, (double(st.end.y) - st.start.y) / len);
		}
	};
	for (int k = 0; k < 3; k++) {
		if ((mask & (1 << k)) == 0) {
			continue;
		}
		const double offset = MARK_PERIOD * k / 3.0;
		for (double t = std::fmod(std::fmod(phase + offset, MARK_PERIOD) + MARK_PERIOD, MARK_PERIOD); t < len; t += MARK_PERIOD) {
			V2 p, d;
			at(t, p, d);
			if (p.x < draw.position.x || p.y < draw.position.y || p.x > draw.position.x + draw.size.x || p.y > draw.position.y + draw.size.y) {
				continue;
			}
			const double s = 5.0;
			const V2 n = V2::of(-double(d.y), double(d.x));
			auto seg = [&](double ax, double ay, double bx, double by, const Color &c) {
				out.push_back(line(V2::of(double(p.x) + ax, double(p.y) + ay), V2::of(double(p.x) + bx, double(p.y) + by), c, 2.0));
			};
			if (k == 0) {
				seg(-s, -s, s, s, MARK_FIRE);
				seg(-s, s, s, -s, MARK_FIRE);
			} else if (k == 1) {
				seg(-d.x * s + n.x * s, -d.y * s + n.y * s, 0, 0, MARK_CHARGE);
				seg(-d.x * s - n.x * s, -d.y * s - n.y * s, 0, 0, MARK_CHARGE);
			} else {
				for (int q = 0; q < 3; q++) {
					const double a = 3.14159265358979 * q / 3.0;
					seg(-std::cos(a) * s, -std::sin(a) * s, std::cos(a) * s, std::sin(a) * s, MARK_FROST);
				}
			}
		}
	}
}

std::vector<double> walk_lengths(const std::vector<rc::TraceStep> &steps, const std::vector<Shift> &shifts) {
	std::vector<double> out;
	double common = 0.0;
	std::optional<double> planned, physical;
	for (size_t i = 0; i < steps.size(); i++) {
		const rc::StepType t = steps[i].type;
		const bool pl = t == rc::StepType::DivergedPlanned || t == rc::StepType::DivergedPostPlanned;
		const bool ph = t == rc::StepType::DivergedPhysical;
		double &walk = pl ? (planned ? *planned : planned.emplace(common)) : ph ? (physical ? *physical : physical.emplace(common)) : common;
		out.push_back(walk);
		walk += drawn_length(steps[i], shifts[i]);
	}
	return out;
}

} // namespace

std::vector<double> dash_offsets(const std::vector<rc::TraceStep> &steps) { return walk_lengths(steps, side_by_side(steps)); }

std::vector<double> lane_offsets(const std::vector<rc::TraceStep> &steps) {
	std::vector<double> out;
	for (const Shift &sh : side_by_side(steps)) {
		out.push_back(sh.off);
	}
	return out;
}

// Stage 126: the merged path's steps as the preview draws them (the presenter
// paints a morph's steps with it). Stage 130: a solid step is a slightly dark
// stroke under slightly bright dashes; dashes run the way the arrow flies
// (dash_phase, px: the presenter's clock; 0 in a replay); steps drawn on one
// another are set side by side.
void Session::paint_steps(const std::vector<rc::TraceStep> &steps, std::vector<Call> &out, double dash_phase,
		const std::vector<rc::sweep::EventDot> *dots, const std::vector<uint8_t> *marks) const {
	PathPainter pp{bounds().grow(50.0f), out};
	pp.portal_shape = [&](int sid, const V2 &at, bool pair) -> std::optional<PathPainter::PortalShape> {
		const BuiltSurface *s = world_->by_id(sid);
		if (s != nullptr && pair && s->portal_pair != 0) {
			s = world_->by_id(s->portal_pair);
		}
		if (s == nullptr) {
			return std::nullopt;
		}
		// a line's normal, or an arc's radius at the point (and its circle)
		PathPainter::PortalShape out;
		V2 n;
		if (rc::arc::is_arc(s->seg.s, s->seg.v, s->seg.e)) {
			const rc::arc::ArcParams p = rc::arc::arc_params(s->seg.s, s->seg.v, s->seg.e);
			out.arc = true;
			out.center = V2::of(double(p.center.x), double(p.center.y));
			out.radius = p.radius;
			n = at - out.center;
		} else {
			const V2 d = s->seg.e - s->seg.s;
			n = V2{-d.y, d.x};
		}
		const float len = n.length();
		if (!(len > 0.0f)) {
			return std::nullopt;
		}
		out.normal = n / len;
		return out;
	};
	const std::vector<Shift> shifts = side_by_side(steps);
	const std::vector<double> along = walk_lengths(steps, shifts);
	for (size_t i = 0; i < steps.size(); i++) {
		const rc::TraceStep &ms = steps[i];
		if (ms.start == ms.end) {
			continue;
		}
		auto tint = [&](const Color &c) { return ms.ghost ? ghost_color(c) : c; };
		// (Stage 136, the user: the dashes run on across the steps, by the walk's length)
		const double phase = dash_phase - along[i];
		if (is_solid(ms.type)) {
			pp.step(ms, tint(solid_under(ms.type)), false, 0.0, shifts[i]);
			pp.step(ms, tint(dashes_over(ms.type)), true, phase, shifts[i]);
		} else {
			pp.step(ms, tint(step_color(ms.type)), true, phase, shifts[i]);
		}
		if (marks != nullptr && marks->size() == steps.size() && (*marks)[i] != 0) {
			arrow_marks(out, ms, (*marks)[i], phase, shifts[i], bounds().grow(50.0f)); // Stage 157
		}
	}
	pp.event_marks(steps, shifts, bounds(), dots);
}

// The path node's calls (Stage 130: the presenter repaints it as its dashes run)
void Session::paint_path(std::vector<Call> &out, double dash_phase) const {
	const Preview &pv = shown(); // (the provisional preview while its exact climb runs)
	PathPainter pp{bounds().grow(50.0f), out};
	if (pv.has_paths || !pv.merged.empty()) {
		if (display_mode_ == 0) {
			if (!pv.merged.empty()) {
				paint_steps(pv.merged, out, dash_phase, &pv.dots, &pv.marks);
				for (const auto &[pt, col] : truncation_markers(pv)) {
					pp.stop_marker(pt, col);
				}
			}
		} else if (pv.has_paths) {
			if (display_mode_ == 1) {
				pp.raw(pv.physical, CYAN, "P");
			} else {
				pp.raw(pv.planned, MAGENTA, "L");
			}
		}
	}
}

std::vector<render::Node> Session::draw() const {
	std::vector<render::Node> nodes;
	for (const render::FrameNode &f : draw_frame()) {
		nodes.push_back(render::Node{f.key.json(), f.z, f.position, f.modulate, *f.calls});
	}
	// (the cache keeps the surface pieces still shown)
	if (surface_draw_.size() > 2 * nodes_.size() + 8) {
		std::map<int, SurfaceDraw> kept;
		for (const Node &n : nodes_) {
			if (auto it = surface_draw_.find(n.serial); it != surface_draw_.end()) {
				kept.emplace(n.serial, std::move(it->second));
			}
		}
		surface_draw_ = std::move(kept);
	}
	return nodes;
}

std::vector<render::FrameNode> Session::draw_frame() const {
	// in the level's tree order (a scene's nodes, then the ones its _ready adds):
	// the order the engine draws nodes of one z-index in
	using render::NodeKind;
	std::vector<render::FrameNode> nodes;
	const rc::Rect2f b = bounds();
	// a node whose calls are made this frame / taken from a painter's cache
	auto fresh = [&](NodeKind kind, int z, const V2 &pos, const Color &mod, std::vector<Call> calls, int a = 0, int bb = 0) {
		const uint64_t v = render::content_hash(calls);
		nodes.push_back(render::FrameNode{render::NodeKey{kind, a, bb}, z, pos, mod, std::make_shared<const std::vector<Call>>(std::move(calls)), v});
	};
	auto cached = [&](CachedDraw &c, const std::array<int, 3> &key, NodeKind kind, int z, const V2 &pos, const Color &mod,
					  const std::function<void(std::vector<Call> &)> &paint) {
		if (!c.calls || c.key != key) {
			std::vector<Call> calls;
			paint(calls);
			c.version = render::content_hash(calls);
			c.calls = std::make_shared<const std::vector<Call>>(std::move(calls));
			c.key = key;
		}
		nodes.push_back(render::FrameNode{render::NodeKey{kind, 0, 0}, z, pos, mod, c.calls, c.version});
	};
	// the background, by its properties
	if (level_.background) {
		const Level::Background &bg = *level_.background;
		Call k;
		k.kind = Kind::Rect;
		k.pts = {V2::of(bg.rect.x, bg.rect.y), V2::of(bg.rect.w, bg.rect.h)};
		k.colors = {rgba(bg.color[0], bg.color[1], bg.color[2], bg.color[3])};
		cached(static_draw_[0], {0, 0, 0}, NodeKind::Background, 0, V2::of(bg.rect.x, bg.rect.y), WHITE, [&](std::vector<Call> &out) { out.push_back(k); });
	}
	// Stage 164 (the user: every element visible): the player-only colliders -- floors and ledges -- drawn in play
	if (!level_.colliders.empty()) {
		cached(collider_draw_, {0, 0, 0}, NodeKind::Colliders, 0, V2{}, WHITE, [&](std::vector<Call> &out) {
			for (const Collider &k : level_.colliders) {
				out.push_back(line(k.a, k.b, rgba(0.55, 0.62, 0.72, 0.9), 3.0));
			}
		});
	}
	// Stage 145: the goal (dashed while locked, solid when open, filled when won) and the detectors
	auto outline = [&](std::vector<Call> &out, const Region &r, const Color &c, double width, bool dash) {
		if (r.circle) {
			const int n = dash ? 24 : 1;
			for (int i = 0; i < n; i += dash ? 2 : 1) {
				out.push_back(arc(r.center, r.radius, 2 * PI * i / n, 2 * PI * (i + 1) / n, dash ? 8 : 64, c, width));
			}
			return;
		}
		const V2 p[4] = {V2::of(r.rect.x, r.rect.y), V2::of(r.rect.x + r.rect.w, r.rect.y), V2::of(r.rect.x + r.rect.w, r.rect.y + r.rect.h),
			V2::of(r.rect.x, r.rect.y + r.rect.h)};
		for (int i = 0; i < 4; i++) {
			Call k = line(p[i], p[(i + 1) % 4], c, width);
			if (dash) {
				k.kind = Kind::DashedLine;
				k.dash = DASH;
			}
			out.push_back(k);
		}
	};
	auto fill = [&](std::vector<Call> &out, const Region &r, const Color &c) {
		Call k;
		if (r.circle) {
			k.kind = Kind::Circle;
			k.pts = {r.center};
			k.radius = r.radius;
		} else {
			k.kind = Kind::Rect;
			k.pts = {V2::of(r.rect.x, r.rect.y), V2::of(r.rect.w, r.rect.h)};
		}
		k.colors = {c};
		out.push_back(k);
	};
	if (level_.objective) {
		std::vector<Call> g;
		const Region &r = level_.objective->goal;
		if (won_) {
			fill(g, r, GOAL_WON_FILL);
		}
		outline(g, r, GOAL_COLOR, 3.0, !goal_open());
		const V2 at = r.circle ? r.center : V2::of(r.rect.x + r.rect.w / 2, r.rect.y + r.rect.h / 2);
		Call t;
		t.kind = Kind::Text;
		t.pts = {at};
		t.text = won_ ? "GOAL!" : "goal";
		t.alignment = 1;
		t.font_size = 16;
		t.colors = {GOAL_COLOR};
		g.push_back(t);
		fresh(NodeKind::Goal, 0, V2{}, WHITE, std::move(g));
	}
	for (size_t i = 0; i < level_.detectors.size(); i++) {
		const Detector &d = level_.detectors[i];
		const bool on = i < inside_.size() && inside_[i];
		std::vector<Call> dc;
		if (d.segment) {
			Call k = line(d.a, d.b, DETECTOR_COLOR, 2.0);
			k.kind = Kind::DashedLine;
			k.dash = DASH;
			dc.push_back(k);
		} else {
			if (on) {
				fill(dc, d.region, DETECTOR_FILL);
			}
			outline(dc, d.region, DETECTOR_COLOR, 2.0, true);
		}
		if (d.marker) { // Stage 179: at its middle
			const V2 at = d.segment ? V2::of((double(d.a.x) + d.b.x) / 2, (double(d.a.y) + d.b.y) / 2)
					: d.region.circle ? d.region.center : V2::of(d.region.rect.x + d.region.rect.w / 2, d.region.rect.y + d.region.rect.h / 2);
			render::marker(dc, d.marker->shape, d.marker->color, at, DESIGNER_MARKER_SIZE);
		}
		fresh(NodeKind::Detector, 0, V2{}, WHITE, std::move(dc), int(i));
	}
	// Stage 157: the things on fire, charged or frosted (a glow over them), and what the preview's walk does to them
	// before it diverges (dashed): catches fire, is charged, is discharged. Stage 159 (the user: every change the
	// shot makes shows in the preview): surfaces that vanish, appear or change, targets it registers, the goal it
	// opens, lights it switches, objects it releases, and where a grapple or pull leaves the player or an object.
	{
		std::vector<std::pair<std::string, std::vector<const SegGeom *>>> things;
		for (const Tagged &t : level_.tagged) {
			std::vector<const SegGeom *> segs;
			for (const size_t k : t.surfaces) {
				for (const int id : level_.surfaces[k].ids) {
					if (const BuiltSurface *bs = world_->undarkened(id); bs != nullptr && present(*bs, flags_)) {
						segs.push_back(&bs->seg);
					}
				}
			}
			things.push_back({t.name, segs});
		}
		for (const World::Body &body : world_->bodies()) {
			if (!body.object.tags.empty()) {
				things.push_back({body.object.name, present(body.built, flags_) ? std::vector<const SegGeom *>{&body.built.seg} : std::vector<const SegGeom *>{}});
			}
		}
		const Preview &pv = shown();
		// (only a preview that changes something lasting, or starts a journey, adds the node)
		const bool preview = !flying_ && display_mode_ == 0 && level_.features.preview && pv.predicted.has_value()
			&& (pv.trip.has_value() || pv.predicted->canonical_key() != flags_.canonical_key());
		if (!things.empty() || preview) {
			cached(glow_draw_, {flags_version_, preview ? trace_version_ : -1, world_->version()}, NodeKind::Glow, 1, V2{}, WHITE,
					[&](std::vector<Call> &out) {
						auto tagged = [&](const std::string &name, const char *tag) {
							for (const Tagged &t : level_.tagged) {
								if (t.name == name) {
									return std::find(t.tags.begin(), t.tags.end(), tag) != t.tags.end();
								}
							}
							for (const World::Body &body : world_->bodies()) {
								if (body.object.name == name) {
									return std::find(body.object.tags.begin(), body.object.tags.end(), tag) != body.object.tags.end();
								}
							}
							return false;
						};
						for (const auto &[name, segs] : things) {
							const bool fire = flags_.is_true("el:" + name + ":fire"), charge = flags_.is_true("el:" + name + ":charge");
							int becomes = 0;
							if (preview) {
								for (const auto &[n, bits] : pv.becomes) {
									becomes = n == name ? bits : becomes;
								}
							}
							for (const SegGeom *g : segs) {
								if (tagged(name, "frosted")) {
									outline_seg(out, *g, rgba(0.6, 0.85, 1.0, 0.35), 6.0, false);
								}
								if (fire) {
									outline_seg(out, *g, rgba(1.0, 0.45, 0.1, 0.55), 8.0, false);
								}
								if (charge) {
									outline_seg(out, *g, rgba(0.85, 0.95, 1.0, 0.5), 5.0, false);
								}
								if (becomes & 1) {
									outline_seg(out, *g, rgba(1.0, 0.45, 0.1, 0.9), 4.0, true);
								}
								if (becomes & 2) {
									outline_seg(out, *g, rgba(0.85, 0.95, 1.0, 0.9), 3.0, true);
								}
								if (becomes & 4) {
									outline_seg(out, *g, rgba(0.5, 0.5, 0.55, 0.9), 3.0, true);
								}
							}
						}
						if (!preview) {
							return;
						}
						const rc::Flags &pred = *pv.predicted;
						// surfaces: vanish (red), appear (green), change their sides (yellow); targets registered (cyan)
						auto kinds = [&](const BuiltSurface &bs, const rc::Flags &f) {
							const BuiltSide *l = active_side(bs, Side::Left, f), *r = active_side(bs, Side::Right, f);
							return std::make_pair(l ? l->effect.type : std::string("pass"), r ? r->effect.type : std::string("pass"));
						};
						for (const BuiltSurface &cur : world_->surfaces()) {
							if (!cur.label || cur.pre_transform) {
								continue;
							}
							const BuiltSurface &bs = *world_->undarkened(cur.id);
							const bool now = present(bs, flags_), then = present(bs, pred);
							if (now && !then) {
								outline_seg(out, bs.seg, rgba(1.0, 0.35, 0.35, 0.95), 3.0, true);
							} else if (!now && then) {
								outline_seg(out, bs.seg, rgba(0.4, 1.0, 0.5, 0.95), 3.0, true);
							} else if (now && kinds(bs, flags_) != kinds(bs, pred)) {
								outline_seg(out, bs.seg, rgba(1.0, 0.85, 0.3, 0.95), 3.0, true);
							}
							const std::string target = "target:" + std::to_string(bs.id);
							if (bs.is_target() && !flags_.is_true(target) && pred.is_true(target)) {
								outline_seg(out, bs.seg, rgba(0.3, 0.9, 1.0, 0.95), 3.0, true);
							}
						}
						// the goal opening
						if (level_.objective) {
							auto open = [&](const rc::Flags &f) {
								std::vector<rc::FlagCond> conds;
								for (const PresenceSpec &c : level_.objective->conditions) {
									conds.push_back({c.key, c.values});
								}
								return f.holds(conds);
							};
							if (!open(flags_) && open(pred)) {
								outline(out, level_.objective->goal, rgba(0.4, 1.0, 0.5, 0.95), 3.0, true);
							}
						}
						// lights and darks switching
						for (size_t k = 0; k < level_.shades.size(); k++) {
							const Shade &sh = level_.shades[k];
							if (sh.when.empty() || rc::key_true(sh.when, flags_) == rc::key_true(sh.when, pred)) {
								continue;
							}
							const Color on = sh.dark ? rgba(0.65, 0.55, 1.0, 0.95) : rgba(1.0, 0.9, 0.4, 0.95);
							outline_seg(out, segment_of(level_.surfaces[sh.surface].spec), rc::key_true(sh.when, pred) ? on : rgba(0.5, 0.5, 0.55, 0.95), 3.0, true);
						}
						// objects a popped bubble releases (they fall)
						for (const World::Body &body : world_->bodies()) {
							const std::string popped = "obj:" + body.object.name + ":popped";
							if (body.object.carries.empty() || flags_.is_true(popped) || !pred.is_true(popped)) {
								continue;
							}
							for (const World::Body &held : world_->bodies()) {
								if (held.object.name == body.object.carries) {
									outline_seg(out, held.built.seg, rgba(1.0, 0.85, 0.3, 0.95), 3.0, true);
								}
							}
						}
						// the journey: its trail and where it ends (orange: stopped by a barrier on the way)
						if (pv.trip && pv.trip->trail.size() >= 2) {
							const Preview::Trip &t = *pv.trip;
							size_t from = 0;
							std::vector<size_t> cuts = t.breaks;
							cuts.push_back(t.trail.size());
							for (const size_t to : cuts) { // (a leg per side of each portal crossed)
								if (to >= from + 2) {
									Call k;
									k.kind = Kind::Polyline;
									k.pts.assign(t.trail.begin() + long(from), t.trail.begin() + long(to));
									k.colors = {rgba(0.6, 1.0, 0.7, 0.6)};
									k.width = 2.0;
									out.push_back(k);
								}
								from = to;
							}
							// Stage 181 (the user): where it ends drawn as what moves -- the player's triangle (facing the cursor
							// from there) and body, or the object's shape
							const Color c = t.blocked ? rgba(1.0, 0.6, 0.2, 0.95) : rgba(0.6, 1.0, 0.7, 0.95);
							const V2 end = t.trail.back();
							if (t.body < 0) {
								for (std::vector<V2> &pts : player_outline(end, double((cursor_ - end).angle()), tall())) {
									pts.push_back(pts.front());
									out.push_back(stroke(Kind::Polyline, std::move(pts), c, 2.0));
								}
							} else {
								const Object &o = world_->bodies()[size_t(t.body)].object;
								std::vector<V2> pts = render::shape_outline(o.shape, end, o.radius);
								if (pts.empty()) {
									out.push_back(arc(end, o.radius, 0.0, rcg::TAU, 32, c, 2.0));
								} else {
									pts.push_back(pts.front());
									out.push_back(stroke(Kind::Polyline, std::move(pts), c, 2.0));
								}
							}
						}
					});
		}
	}
	// Stage 153: the shadow (over the surfaces it hides) and the shades' edges, dashed (faint while inactive)
	if (!level_.shades.empty()) {
		cached(shade_draw_, {shade_serial_, 0, 0}, NodeKind::Shadow, 15, V2{}, WHITE, [&](std::vector<Call> &out) {
			if (!shade_fill_.indices.empty()) {
				Call k;
				k.kind = Kind::Triangles;
				for (const rc::vis::Pt &p : shade_fill_.points) {
					k.pts.push_back(V2::of(p.x, p.y));
				}
				for (uint32_t i : shade_fill_.indices) {
					k.indices.push_back(int(i));
				}
				k.colors = {SHADOW_FILL};
				out.push_back(k);
			}
			for (const SurfaceSpec &e : light_edges_) { // Stage 168: where the player's light stops the arrow
				dashed(out, segment_of(e), SHADE_LIGHT, 2.0);
			}
			for (size_t i = 0; i < level_.shades.size(); i++) {
				const Shade &sh = level_.shades[i];
				const bool on = i < shade_active_.size() && shade_active_[i];
				Color c = sh.dark ? SHADE_DARK : SHADE_LIGHT;
				c[3] = on ? 1.0f : 0.35f;
				dashed(out, segment_of(level_.surfaces[sh.surface].spec), c, 2.0);
			}
		});
	}
	const bool player_crossing = journey_ && journey_->crossing && journey_->body < 0;
	if (level_.player_start && !player_crossing) {
		std::vector<Call> calls;
		if (tall()) {
			Call legs;
			legs.kind = Kind::Polygon2D;
			legs.pts = PLAYER_BODY;
			legs.colors = {BODY_COLOR};
			calls.push_back(legs);
		}
		Call k;
		k.kind = Kind::Polygon2D;
		k.pts = PLAYER_HEAD;
		k.colors = {WHITE};
		k.rotation = double(facing_);
		calls.push_back(k);
		fresh(NodeKind::Player, 0, player_, WHITE, std::move(calls));
	}
	// Stage 166: a traveller crossing a portal -- the part still before the entry, and its copy's part beyond the
	// exit (the portal's motion of the part already through)
	if (journey_ && journey_->crossing) {
		const Journey &j = *journey_;
		const V2 at = j.body < 0 ? player_ : world_->bodies()[size_t(j.body)].pos;
		auto before = [&](const V2 &p) { return (double(p.x) - j.entry_a.x) * j.entry_n.x + (double(p.y) - j.entry_a.y) * j.entry_n.y <= 0.0; };
		std::vector<Call> calls;
		if (j.body >= 0) {
			const Object &o = world_->bodies()[size_t(j.body)].object;
			const double r = o.radius;
			const int n = 64;
			// its outline: the circle's 65 points, or (Stage 181) its shape's, each side cut in 16
			std::vector<V2> ring;
			const std::vector<V2> corners = render::shape_outline(o.shape, at, r);
			if (corners.empty()) {
				for (int i = 0; i <= n; i++) {
					const double a = TAU * i / n;
					ring.push_back(V2::of(double(at.x) + r * std::cos(a), double(at.y) + r * std::sin(a)));
				}
			} else {
				for (size_t i = 0; i < corners.size(); i++) {
					const V2 &p = corners[i], &q = corners[(i + 1) % corners.size()];
					for (int k = 0; k < 16; k++) {
						ring.push_back(V2::of(double(p.x) + (double(q.x) - p.x) * k / 16, double(p.y) + (double(q.y) - p.y) * k / 16));
					}
				}
				ring.push_back(corners.front());
			}
			std::vector<V2> near_pts, far_pts;
			auto flush = [&](std::vector<V2> &pts) {
				if (pts.size() >= 2) {
					calls.push_back(stroke(Kind::Polyline, pts, rgba(0.95, 0.6, 0.3), 2.5));
				}
				pts.clear();
			};
			for (const V2 &p : ring) {
				if (before(p)) {
					near_pts.push_back(p);
					flush(far_pts);
				} else {
					far_pts.push_back(j.map(p));
					flush(near_pts);
				}
			}
			flush(near_pts);
			flush(far_pts);
		} else {
			// the player's triangle (and body), cut by the entry's line: the near part here, the far part at the exit
			std::vector<std::pair<std::vector<V2>, Color>> parts;
			std::vector<std::vector<V2>> outlines = player_outline(at, double(facing_), tall());
			for (size_t i = 0; i < outlines.size(); i++) {
				parts.push_back({std::move(outlines[i]), i + 1 < outlines.size() ? BODY_COLOR : WHITE});
			}
			for (const auto &[tri, color] : parts)
			for (const bool near_side : {true, false}) {
				std::vector<V2> poly;
				for (size_t i = 0; i < tri.size(); i++) {
					const V2 &p = tri[i], &q = tri[(i + 1) % tri.size()];
					const double fp = (double(p.x) - j.entry_a.x) * j.entry_n.x + (double(p.y) - j.entry_a.y) * j.entry_n.y;
					const double fq = (double(q.x) - j.entry_a.x) * j.entry_n.x + (double(q.y) - j.entry_a.y) * j.entry_n.y;
					const bool pin = near_side ? fp <= 0 : fp > 0, qin = near_side ? fq <= 0 : fq > 0;
					if (pin) {
						poly.push_back(p);
					}
					if (pin != qin) {
						const double t = fp / (fp - fq);
						poly.push_back(V2::of(double(p.x) + (double(q.x) - p.x) * t, double(p.y) + (double(q.y) - p.y) * t));
					}
				}
				if (poly.size() >= 3) {
					if (!near_side) {
						for (V2 &p : poly) {
							p = j.map(p);
						}
					}
					Call k;
					k.kind = Kind::Polygon;
					k.pts = poly;
					k.colors.assign(poly.size(), color); // (one per point)
					calls.push_back(k);
				}
			}
		}
		fresh(NodeKind::Journey, 25, V2{}, WHITE, std::move(calls));
	}
	if (level_.features.cursor) {
		cached(static_draw_[1], {0, 0, 0}, NodeKind::Cursor, 0, cursor_, WHITE, [&](std::vector<Call> &out) {
			out.push_back(line(V2{-10, 0}, V2{10, 0}, WHITE, 1.5));
			out.push_back(line(V2{0, -10}, V2{0, 10}, WHITE, 1.5));
		});
	}
	const Color dim{1, 1, 1, flying_ ? 0.25f : 1.0f};
	if (level_.features.preview) {
		const Preview &pv = shown(); // (the provisional preview while its exact climb runs)
		(void)pv;
		cached(path_draw_, {trace_version_, display_mode_, provisional_version_}, NodeKind::Path, 20, V2{}, dim,
			[&](std::vector<Call> &out) { paint_path(out, 0.0); });
	}
	if (level_.features.arrow && flying_) { // (the arrow's node is hidden between flights)
		std::vector<Call> a;
		const V2 tip = arrow_pos_;
		const V2 tail = tip - arrow_dir_ * 40.0f;
		a.push_back(line(tail, tip, WHITE, 2.0));
		const float angle = float(30.0 * (PI / 180.0)); // deg_to_rad
		const V2 hl = tip - arrow_dir_.rotated(angle) * 16.0f;
		const V2 hr = tip - arrow_dir_.rotated(-angle) * 16.0f;
		a.push_back(polygon({tip, hl, hr}, WHITE));
		fresh(NodeKind::Arrow, 30, V2{}, WHITE, std::move(a));
	}
	// the HUD (a Label at (10, 10) of a Control at the origin)
	if (level_.features.hud) {
		Call k;
		k.kind = Kind::Label;
		k.text = hud_;
		fresh(NodeKind::Hud, 0, V2{}, WHITE, {k});
	}
	// the surfaces, one node per piece
	const int crossing_id = journey_ && journey_->crossing && journey_->body >= 0 ? world_->bodies()[size_t(journey_->body)].id : -1;
	for (const Node &n : nodes_) {
		if (n.surface == crossing_id) {
			continue; // (Stage 166: a body crossing a portal is drawn clipped, with its copy: the journey node)
		}
		const BuiltSurface *s = level_.shades.empty() ? world_->by_id(n.surface) : world_->undarkened(n.surface); // (Stage 153: drawn as it is, under the shadow)
		if (s == nullptr) {
			continue;
		}
		SurfaceDraw &c = surface_draw_[n.serial];
		if (!c.calls || c.flags != flags_version_ || c.hover != n.hover || c.plan != n.plan || c.indices != n.plan_indices) {
			std::vector<Call> calls;
			SurfacePainter{*s, n.segment, n.piece, flags_, calls}.draw(n.hover, n.plan, n.plan_indices);
			c = SurfaceDraw{flags_version_, n.hover, n.plan, n.plan_indices, nullptr, render::content_hash(calls)};
			c.calls = std::make_shared<const std::vector<Call>>(std::move(calls));
		}
		nodes.push_back(render::FrameNode{render::NodeKey{NodeKind::Surface, n.surface, n.piece}, 0, V2{}, WHITE, c.calls, c.version});
	}
	// the edge
	{
		std::vector<Call> out;
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
		// Stage 132: the ray's origin -- the planned entries' marks, and its marker where plannable
		for (const auto &m : origin_marks_) {
			const Color color = m.valid ? PLAN_VALID_COLOR : PLAN_INVALID_COLOR;
			point_overlay(out, m.point, color);
			point_marker(out, m.point, render::with_alpha(color, 0.9), false);
			out.push_back(text(m.point + V2::of(POINT_RADIUS + OUTER_CHEVRON_DIST, -POINT_RADIUS - 4.0), std::to_string(m.index), 0, 14, WHITE));
		}
		if (origin_point_) {
			if (origin_hovered_) {
				point_overlay(out, *origin_point_, HOVER_COLOR);
			}
			point_marker(out, *origin_point_, POINT_COLOR, origin_hovered_);
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
		fresh(NodeKind::Edge, 5, V2{}, WHITE, std::move(out));
	}
	if (level_.features.preview) {
		// the visibility region
		cached(vis_draw_, {visibility_version_, 0, 0}, NodeKind::Visibility, 10, V2{}, dim, [&](std::vector<Call> &out) {
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
			out.push_back(k);
		}
		const Color whisker{FILL[0] * 1.0f, FILL[1] * 1.0f, FILL[2] * 1.0f, FILL[3] * 3.0f};
		for (const std::vector<V2> &l : whiskers_) {
			out.push_back(stroke(Kind::Polyline, l, whisker, 1.5));
		}
		});
	}
	if (spawn_preview_) {
		const int sv = spawn_inputs_ ? spawn_inputs_->trace : -1, sw = spawn_inputs_ ? spawn_inputs_->world : -1;
		cached(spawn_draw_, {sv, sw, 0}, NodeKind::Spawn, 19, V2{}, dim, [&](std::vector<Call> &out) {
		for (const Placed &p : spawn_specs_) {
			dashed(out, segment_of(p.spec), GHOST_SPAWN_COLOR, 2.0);
			if (p.spec.is_portal()) {
				dashed(out, build_spec(p.spec, 0, std::nullopt).at(1).seg, GHOST_SPAWN_COLOR, 2.0);
			}
		}
		for (int id : spawn_vanishing_) {
			if (const BuiltSurface *s = world_->by_id(id)) {
				dashed(out, s->seg, VANISH_COLOR, 2.0);
			}
		}
		});
	}
	// (the cache keeps the surface pieces still shown)
	if (surface_draw_.size() > 2 * nodes_.size() + 8) {
		std::map<int, SurfaceDraw> kept;
		for (const Node &n : nodes_) {
			if (auto it = surface_draw_.find(n.serial); it != surface_draw_.end()) {
				kept.emplace(n.serial, std::move(it->second));
			}
		}
		surface_draw_ = std::move(kept);
	}
	return nodes;
}

} // namespace rcg
