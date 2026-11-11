// Stage 208: one surface's look and the draw primitives (rcg/surface_paint.hpp), moved from the session's draw
// list (session_draw.cpp) so the editor paints its elements as the game does.
#include "rcg/surface_paint.hpp"

#include "rcg/model.hpp"
#include "rcg/style.hpp"

#include <algorithm>
#include <cmath>

namespace rcg::paint {

using namespace style;

namespace {
constexpr int MAX_DASHES = 256;
constexpr double PI_CAP = 3.141592653589793;
} // namespace

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
	if (e.type == "pass") { // (Stage 210: a pass side's hits say themselves by their marks, a button's by its key)
		return !cfg->hits.empty() && writes_key(*cfg, "ghost", -1) ? GHOST_TOGGLER_COLOR : GRAY;
	}
	if (e.type == "terminal") return RED;
	if (e.type == "reflection") return BLUE;
	if (e.type == "inversion") return PURPLE;
	if (e.type == "portal") return CYAN;
	return e.back ? PROJ_BACK : PROJ_FWD;
}

void state_glyph(std::vector<Call> &out, Glyph g, const V2 &p, const V2 &d, double s) {
	const V2 n = V2::of(-double(d.y), double(d.x));
	auto seg = [&](double ax, double ay, double bx, double by, const Color &c) {
		out.push_back(line(V2::of(double(p.x) + ax, double(p.y) + ay), V2::of(double(p.x) + bx, double(p.y) + by), c, 2.0));
	};
	if (g == Glyph::Fire) {
		seg(-s, -s, s, s, MARK_FIRE);
		seg(-s, s, s, -s, MARK_FIRE);
	} else if (g == Glyph::Charge) {
		seg(-d.x * s + n.x * s, -d.y * s + n.y * s, 0, 0, MARK_CHARGE);
		seg(-d.x * s - n.x * s, -d.y * s - n.y * s, 0, 0, MARK_CHARGE);
	} else {
		for (int q = 0; q < 3; q++) {
			const double a = 3.14159265358979 * q / 3.0;
			seg(-std::cos(a) * s, -std::sin(a) * s, std::cos(a) * s, std::sin(a) * s, MARK_FROST);
		}
	}
}

namespace {

bool starts_with(const std::string &s, const char *p) { return s.rfind(p, 0) == 0; }
bool ends_with(const std::string &s, const std::string &t) { return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0; }

// Stage 210: a write that makes its surface a button -- a flag of the level's (not the arrow's states, a break, the
// ghost, an object's or an element's own state)
bool button_write(const HitSpec &h) {
	return h.kind == "state" && h.key != "ghost" && !starts_with(h.key, "shot:") && !starts_with(h.key, "obj:") && !starts_with(h.key, "el:");
}

// The flag a button writes (its first; any side, any resolver row), if it is one.
std::optional<std::string> button_flag(const BuiltSurface &s) {
	std::vector<const BuiltSide *> sides = {s.left ? &*s.left : nullptr, s.right ? &*s.right : nullptr};
	for (const BuiltRow &r : s.resolver) {
		sides.push_back(r.left ? &*r.left : nullptr);
		sides.push_back(r.right ? &*r.right : nullptr);
	}
	for (const BuiltSide *side : sides) {
		for (const HitSpec &h : side != nullptr ? side->hits : std::vector<HitSpec>{}) {
			if (button_write(h)) {
				return h.key;
			}
		}
	}
	return std::nullopt;
}

// Stage 209: what a hit's mark is. A grapple and a pull are drawn along the surface instead, and (Stage 210) a
// button's write by its key.
enum class Mark { None, Flag, Target, Spawn, Reset, Break, Pop, Fire, Charge, Frost };
Mark mark_of(const HitSpec &h, const BuiltSide &side) {
	if (h.kind == "target") return Mark::Target;
	if (h.kind == "spawn") return Mark::Spawn;
	if (h.kind == "reset") return Mark::Reset;
	if (h.kind == "grapple" || h.kind == "pull") return Mark::None;
	if (h.kind != "state") return Mark::Flag;
	if (starts_with(h.key, "shot:") && ends_with(h.key, ":broken")) return Mark::Break;
	if (starts_with(h.key, "obj:") && ends_with(h.key, ":popped")) return Mark::Pop;
	if (ends_with(h.key, "fire")) return Mark::Fire;
	if (ends_with(h.key, "charge")) return Mark::Charge;
	if (ends_with(h.key, "frost")) return Mark::Frost;
	return button_write(h) || side.effect.type == "pass" ? Mark::None : Mark::Flag;
}

bool has_hit(const BuiltSide *side, const char *kind) {
	return side != nullptr && std::any_of(side->hits.begin(), side->hits.end(), [&](const HitSpec &h) { return h.kind == kind; });
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

	// a hit's mark at `at`, `dir` along the surface (Stage 209: one per feature)
	void marker(Mark m, bool deferred, const V2 &at, const V2 &dir) {
		const double s = MARKER_SIZE;
		const V2 n = rot90(dir);
		if (m == Mark::Target && registered()) { // Stage 210: a checkmark once registered
			out.push_back(stroke(Kind::Polyline, {at + V2::of(-s, 0), at + V2::of(-s * 0.3, s * 0.7), at + V2::of(s, -s * 0.8)}, TARGET_COLOR, 2.0));
		} else if (m == Mark::Target) {
			out.push_back(arc(at, s, 0.0, rcg::TAU, 16, TARGET_COLOR, 1.5));
			out.push_back(circle(at, 1.5, TARGET_COLOR));
		} else if (m == Mark::Spawn) {
			out.push_back(line(at - V2::of(s, 0), at + V2::of(s, 0), SPAWN_COLOR, 2.0));
			out.push_back(line(at - V2::of(0, s), at + V2::of(0, s), SPAWN_COLOR, 2.0));
		} else if (m == Mark::Reset) {
			out.push_back(line(at - V2::of(s, s), at + V2::of(s, s), RESET_COLOR, 2.0));
			out.push_back(line(at - V2::of(s, -s), at + V2::of(s, -s), RESET_COLOR, 2.0));
		} else if (m == Mark::Break) { // a crack: a zigzag across the surface
			const V2 p0 = at - vs(n, s) - vs(dir, s * 0.3), p1 = at - vs(n, s * 0.3) + vs(dir, s * 0.5), p2 = at + vs(n, s * 0.3) - vs(dir, s * 0.5),
					   p3 = at + vs(n, s) + vs(dir, s * 0.3);
			out.push_back(stroke(Kind::Polyline, {p0, p1, p2, p3}, BREAK_COLOR, 2.0));
		} else if (m == Mark::Pop) { // a burst: six short rays
			for (int q = 0; q < 6; q++) {
				const double a = rcg::TAU * q / 6.0;
				const V2 u = V2::of(std::cos(a), std::sin(a));
				out.push_back(line(at + vs(u, s * 0.4), at + vs(u, s * 1.1), POP_COLOR, 1.5));
			}
		} else if (m == Mark::Fire || m == Mark::Charge || m == Mark::Frost) {
			state_glyph(out, m == Mark::Fire ? Glyph::Fire : m == Mark::Charge ? Glyph::Charge : Glyph::Frost, at, dir, s);
		} else {
			Call k;
			k.kind = Kind::ColoredPolygon;
			k.pts = {at + V2::of(0, -s), at + V2::of(s, 0), at + V2::of(0, s), at + V2::of(-s, 0)};
			k.colors = {FLAG_TOGGLER_COLOR};
			out.push_back(k);
		}
		if (deferred) {
			out.push_back(circle(at, 2.0, DEFERRED_DOT));
		}
	}

	// Stage 209: the marks of each side's hits, in a row by the surface's marker point (a line's middle, an arc's
	// via, a full circle's top), MARK_OFFSET off it on the side's half; along an arc the row follows it. A full
	// circle whose sides mark alike shows them once, outside.
	void hit_markers(const BuiltSide *left, const BuiltSide *right) {
		struct Item {
			Mark mark;
			bool deferred;
		};
		auto marks_of = [&](const BuiltSide *cfg) {
			std::vector<Item> out_marks;
			if (cfg == nullptr) {
				return out_marks;
			}
			for (const HitSpec &h : cfg->hits) {
				const Mark m = mark_of(h, *cfg);
				const bool seen = std::any_of(out_marks.begin(), out_marks.end(), [&](const Item &it) { return it.mark == m && it.deferred == h.deferred; });
				if (m != Mark::None && !seen) {
					out_marks.push_back({m, h.deferred});
				}
			}
			return out_marks;
		};
		const std::vector<Item> lm = marks_of(left), rm = marks_of(right);
		const bool same = lm.size() == rm.size() && std::equal(lm.begin(), lm.end(), rm.begin(), [](const Item &a, const Item &b) { return a.mark == b.mark && a.deferred == b.deferred; });
		const V2 anchor = marker_point(surface.seg);
		const bool round = !surface.seg.is_line();
		const V2 ctr = round ? rc::arc::center_of(surface.seg.get_carrier()) : V2{};
		const double r = round ? surface.seg.get_carrier().radius() : 0.0;
		for (Side side : {Side::Left, Side::Right}) {
			const std::vector<Item> &marks = side == Side::Left ? lm : rm;
			if (marks.empty()) {
				continue;
			}
			const V2 nrm = marker_normal(side, anchor);
			const bool outward = !round || double(nrm.x) * (double(anchor.x) - ctr.x) + double(nrm.y) * (double(anchor.y) - ctr.y) > 0;
			if (surface.seg.full && same && !outward) {
				continue; // (drawn outside only)
			}
			const V2 dir = marker_tangent(anchor);
			for (size_t i = 0; i < marks.size(); i++) {
				const double k = double(i) - double(marks.size() - 1) / 2.0;
				V2 at, along = dir;
				if (round) { // on the offset arc, by arc length
					const double rr = outward ? r + MARK_OFFSET : std::max(r - MARK_OFFSET, r * 0.5);
					const double a0 = std::atan2(double(anchor.y) - ctr.y, double(anchor.x) - ctr.x) + k * MARKER_SPACING / rr;
					at = V2::of(double(ctr.x) + rr * std::cos(a0), double(ctr.y) + rr * std::sin(a0));
					along = V2::of(-std::sin(a0), std::cos(a0));
				} else {
					at = anchor + vs(nrm, MARK_OFFSET) + vs(vs(dir, MARKER_SPACING), k);
				}
				marker(marks[i].mark, marks[i].deferred, at, along);
			}
		}
	}

	// Stage 210: a target whose flag the shots have set
	bool registered() const { return flags.is_true("target:" + std::to_string(surface.id)); }

	// Stage 210: the outline `half` about this piece -- a line's or an arc's capsule (round ends), as a closed polygon
	std::vector<V2> capsule(double half) const {
		std::vector<V2> pts;
		constexpr int CAP = 8;
		auto cap = [&](const V2 &at, const V2 &u, const V2 &w) { // from at + u*half round through at + w*half to at - u*half
			for (int k = 0; k <= CAP; k++) {
				const double t = PI_CAP * k / CAP;
				pts.push_back(V2::of(double(at.x) + (double(u.x) * std::cos(t) + double(w.x) * std::sin(t)) * half,
						double(at.y) + (double(u.y) * std::cos(t) + double(w.y) * std::sin(t)) * half));
			}
		};
		if (seg.is_line()) {
			const V2 d = (seg.e - seg.s).normalized(), n = rot90(d);
			cap(seg.e, n, d);
			cap(seg.s, -n, -d);
		} else {
			const ArcView a = arc_view();
			const int count = std::max(8, a.pc);
			const double outer = a.r + half, inner = std::max(a.r - half, 1.0), sgn = a.ea >= a.sa ? 1.0 : -1.0;
			for (int k = 0; k <= count; k++) {
				const double t = a.sa + (a.ea - a.sa) * k / count;
				pts.push_back(V2::of(double(a.ctr.x) + outer * std::cos(t), double(a.ctr.y) + outer * std::sin(t)));
			}
			const V2 re = V2::of(std::cos(a.ea), std::sin(a.ea)), rs = V2::of(std::cos(a.sa), std::sin(a.sa));
			cap(V2::of(double(a.ctr.x) + a.r * re.x, double(a.ctr.y) + a.r * re.y), re, V2::of(-double(re.y) * sgn, double(re.x) * sgn));
			for (int k = count; k >= 0; k--) {
				const double t = a.sa + (a.ea - a.sa) * k / count;
				pts.push_back(V2::of(double(a.ctr.x) + inner * std::cos(t), double(a.ctr.y) + inner * std::sin(t)));
			}
			cap(V2::of(double(a.ctr.x) + a.r * rs.x, double(a.ctr.y) + a.r * rs.y), -rs, V2::of(double(rs.y) * sgn, -double(rs.x) * sgn));
		}
		return pts;
	}

	// Stage 210 (the user's option A): a button's key -- an amber outline about it, dim while its flag is off, bright and
	// filled once it is on. `fill`: the fill (under the face), else the outline (over it).
	void key(const std::string &flag, bool fill) {
		const bool on = flags.is_true(flag);
		if (seg.full) {
			const ArcView a = arc_view();
			if (fill && on) {
				outline_seg(out, seg, KEY_FILL, 2 * KEY_HALF_WIDTH, false);
			} else if (!fill) {
				for (const double rr : {a.r + KEY_HALF_WIDTH, std::max(a.r - KEY_HALF_WIDTH, 1.0)}) {
					out.push_back(arc(a.ctr, rr, 0.0, rcg::TAU, a.pc, on ? KEY_ON : KEY_OFF, 1.5));
				}
			}
			return;
		}
		std::vector<V2> pts = capsule(KEY_HALF_WIDTH);
		if (fill && on) {
			out.push_back(polygon_n(pts, KEY_FILL));
		} else if (!fill) {
			pts.push_back(pts.front());
			out.push_back(stroke(Kind::Polyline, std::move(pts), on ? KEY_ON : KEY_OFF, 1.5));
		}
	}

	// Stage 209 (the user's option B): a grapple's barbs along this piece, on each side that grapples, in the journey's
	// mint -- every BARB_SPACING of its length, leaning back along it
	void grapple_barbs(const BuiltSide *left, const BuiltSide *right) {
		for (Side side : {Side::Left, Side::Right}) {
			if (!has_hit(side == Side::Left ? left : right, "grapple")) {
				continue;
			}
			const bool round = !seg.is_line();
			double len = 0.0;
			ArcView a{};
			if (round) {
				a = arc_view();
				len = std::abs(a.span) * a.r;
			} else {
				len = double(seg.s.distance_to(seg.e));
			}
			const int n = std::max(1, int(len / BARB_SPACING));
			for (int i = 0; i < n; i++) {
				const double t = (double(i) + 0.5) / n;
				V2 p, dir;
				if (round) {
					const double ang = a.sa + (a.ea - a.sa) * t;
					p = V2::of(double(a.ctr.x) + a.r * std::cos(ang), double(a.ctr.y) + a.r * std::sin(ang));
					const double sgn = a.ea >= a.sa ? 1.0 : -1.0;
					dir = V2::of(-std::sin(ang) * sgn, std::cos(ang) * sgn);
				} else {
					p = V2::of(double(seg.s.x) + (double(seg.e.x) - seg.s.x) * t, double(seg.s.y) + (double(seg.e.y) - seg.s.y) * t);
					dir = (seg.e - seg.s).normalized();
				}
				const V2 nrm = marker_normal(side, p);
				const V2 root = p + vs(nrm, SIDE_OFFSET + 1.0);
				const V2 tip = root + vs(V2::of(double(nrm.x) - dir.x, double(nrm.y) - dir.y).normalized(), BARB_LENGTH);
				out.push_back(line(root, tip, JOURNEY, 2.0));
			}
		}
	}

	// Stage 209 (option B): an object filled -- the journey's mint and four handles when a pull brings it back, a
	// faint gray when it stays -- its circle (what it collides as), under its outline
	void object_fill(const BuiltSide *left, const BuiltSide *right) {
		const ArcView a = arc_view();
		const bool pullable = has_hit(left, "pull") || has_hit(right, "pull");
		out.push_back(circle(a.ctr, a.r, pullable ? OBJECT_PULL_FILL : OBJECT_FILL));
		if (!pullable) {
			return;
		}
		for (int q = 0; q < 4; q++) {
			const double ang = rcg::TAU * (q + 0.5) / 4.0;
			const V2 u = V2::of(std::cos(ang), std::sin(ang));
			out.push_back(line(a.ctr + vs(u, a.r + 3.0), a.ctr + vs(u, a.r + 3.0 + HANDLE_LENGTH), JOURNEY, 3.0));
		}
	}

	// Stage 179: the designer's marker, on the surface's middle (a circle's top), once (on its first piece); faded
	// while the surface is absent (an open door still shows what opened it)
	// Stage 190: a door's posts, at the ends of the surface this piece has
	void door_posts(double alpha) {
		if (!is_door(surface) || seg.full) {
			return;
		}
		for (const V2 &end : {seg.s, seg.e}) {
			if (end == surface.seg.s || end == surface.seg.e) {
				const auto [a, b] = door_post(seg, end, DOOR_POST);
				out.push_back(line(a, b, render::with_alpha(DOOR_COLOR, alpha), DOOR_POST_WIDTH));
			}
		}
	}

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
			door_posts(0.5); // (an open door's frame)
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
		const bool shield_l = shields(surface, left), shield_r = shields(surface, right); // Stage 186: unlike a wall
		lc = shield_l ? SHIELD_COLOR : lc;
		rc_ = shield_r ? SHIELD_COLOR : rc_;
		const bool arcs = !seg.is_line();
		const bool l_outer = left_outer();
		if (shield_l && shield_r && !shaped()) {
			outline_seg(out, seg, SHIELD_BAND, BAND_WIDTH, false);
		} else if (steel_band(surface, left, right) && !shaped()) {
			outline_seg(out, seg, STEEL_BAND, BAND_WIDTH, false);
		}
		if (surface.object && seg.full && piece_index <= 0) {
			object_fill(left, right);
		}
		const std::optional<std::string> button = button_flag(surface);
		if (button) {
			key(*button, true);
		}
		const bool target = has_hit(left, "target") || has_hit(right, "target");
		if (target && !registered()) { // Stage 210: a target not yet registered, haloed
			outline_seg(out, seg, TARGET_HALO, TARGET_HALO_WIDTH, false);
		}
		const size_t face_from = out.size();
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
		if (is_door(surface)) { // Stage 190 (the user): unlike the wall beside it, whatever its face
			outline_seg(out, seg, DOOR_COLOR, DOOR_CORE_WIDTH, false);
			door_posts(1.0);
		}
		grapple_barbs(left, right);
		if (button) {
			key(*button, false);
		}
		if (target && registered()) { // Stage 210: done -- drawn faint (its checkmark is not)
			for (size_t i = face_from; i < out.size(); i++) {
				for (Color &c : out[i].colors) {
					c[3] = float(double(c[3]) * TARGET_DONE_ALPHA);
				}
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

} // namespace

void surface(std::vector<Call> &out, const BuiltSurface &s, const SegGeom &seg, int piece, const rc::Flags &flags,
		const std::vector<Overlay> &hover, const std::vector<Overlay> &plan, const std::vector<int> &plan_indices) {
	SurfacePainter{s, seg, piece, flags, out}.draw(hover, plan, plan_indices);
}

} // namespace rcg::paint
