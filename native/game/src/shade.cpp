// Stage 153 (GAME_SPEC 9.9): light and shadow (rcg/shade.hpp).
#include "rcg/shade.hpp"

#include "rc/earcut.hpp"
#include "rc/exact.hpp"
#include "rc/region_union.hpp"

#include <clipper2/clipper.engine.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace rcg::shade {

namespace {

constexpr double PI = 3.14159265358979323846;

V2 round2(double x, double y) { return V2::of(x, y); }

// The direction determine_side calls Left, at a point of a line (unit).
std::pair<double, double> left_normal(const rc::arc::SegGeom &g) {
	const double dx = double(g.e.x) - g.s.x, dy = double(g.e.y) - g.s.y;
	const double len = std::hypot(dx, dy);
	const double px = -dy / len, py = dx / len;
	const double mx = (double(g.s.x) + g.e.x) / 2, my = (double(g.s.y) + g.e.y) / 2;
	const bool left = g.determine_side(V2::of(mx + px, my + py)) == rc::Side::Left;
	return left ? std::pair{px, py} : std::pair{-px, -py};
}

// Whether a circle's (or an arc's) left side is its inside.
bool left_inside(const rc::arc::SegGeom &g, double cx, double cy, double r, double x, double y) {
	const double step = std::min(1.0, r / 2);
	const double k = (r - step) / r;
	return g.determine_side(V2::of(cx + (x - cx) * k, cy + (y - cy) * k)) == rc::Side::Left;
}

double ring_area(const std::vector<rc::vis::Pt> &ring) {
	double a = 0.0;
	for (size_t i = 0; i < ring.size(); i++) {
		const rc::vis::Pt &p = ring[i], &q = ring[(i + 1) % ring.size()];
		a += p.x * q.y - p.y * q.x;
	}
	return a / 2.0;
}

using Paths = Clipper2Lib::PathsD;

Paths run(Clipper2Lib::ClipType type, const Paths &subjects, const Paths &clips) {
	Clipper2Lib::ClipperD c(4);
	c.AddSubject(subjects);
	if (!clips.empty()) {
		c.AddClip(clips);
	}
	Paths out;
	c.Execute(type, Clipper2Lib::FillRule::NonZero, out);
	return out;
}

} // namespace

bool Region::contains(double x, double y) const {
	if (line) {
		return (x - sx) * nx + (y - sy) * ny > 0.0;
	}
	const double d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
	return inside ? d2 < r * r : d2 > r * r;
}

Region region_of(const rc::arc::SegGeom &g, const std::string &side, bool dark) {
	Region out;
	out.dark = dark;
	if (g.is_line()) {
		out.line = true;
		out.sx = g.s.x;
		out.sy = g.s.y;
		out.ex = g.e.x;
		out.ey = g.e.y;
		const auto [lx, ly] = left_normal(g);
		const bool left = side != "right";
		out.nx = left ? lx : -lx;
		out.ny = left ? ly : -ly;
		return out;
	}
	out.line = false;
	const rc::arc::GeneralizedCircle &c = g.get_carrier();
	out.cx = c.center_x();
	out.cy = c.center_y();
	out.r = c.radius();
	if (side == "left" || side == "right") {
		const bool li = left_inside(g, out.cx, out.cy, out.r, g.s.x, g.s.y);
		out.inside = (side == "left") == li;
	} else {
		out.inside = side != "outside";
	}
	return out;
}

bool shadowed(const std::vector<Region> &regions, const std::vector<bool> &active, double x, double y) {
	bool dark = false;
	for (size_t k = 0; k < regions.size(); k++) {
		if (!active[k] || !regions[k].contains(x, y)) {
			continue;
		}
		if (!regions[k].dark) {
			return false;
		}
		dark = true;
	}
	return dark;
}

std::vector<Piece> arrange(const std::vector<Region> &regions, const Rect &bounds) {
	// the carriers: regions on one line (exactly collinear points) or one circle share one
	std::vector<size_t> group(regions.size());
	std::vector<size_t> heads;
	for (size_t i = 0; i < regions.size(); i++) {
		group[i] = heads.size();
		for (size_t h = 0; h < heads.size(); h++) {
			const Region &a = regions[heads[h]], &b = regions[i];
			const bool same = a.line && b.line
					? rc::exact::orient_zero(a.sx, a.sy, a.ex, a.ey, b.sx, b.sy) && rc::exact::orient_zero(a.sx, a.sy, a.ex, a.ey, b.ex, b.ey)
					: !a.line && !b.line && a.cx == b.cx && a.cy == b.cy && a.r == b.r;
			if (same) {
				group[i] = h;
				break;
			}
		}
		if (group[i] == heads.size()) {
			heads.push_back(i);
		}
	}
	struct Carrier {
		const Region *g = nullptr;
		double dx = 0, dy = 0;      // a line's direction
		double t0 = 0, t1 = 0;      // ... its span within the bounds
		bool empty = false;         // ... none
		V2 p0, p1;                  // ... its ends on the bounds
		std::vector<std::pair<double, V2>> at; // the crossings: a parameter (t, an angle) and the point
	};
	std::vector<Carrier> cs(heads.size());
	const double x0 = bounds.x, y0 = bounds.y, x1 = bounds.x + bounds.w, y1 = bounds.y + bounds.h;
	for (size_t h = 0; h < heads.size(); h++) {
		Carrier &c = cs[h];
		c.g = &regions[heads[h]];
		if (!c.g->line) {
			continue;
		}
		c.dx = c.g->ex - c.g->sx;
		c.dy = c.g->ey - c.g->sy;
		// the span inside the bounds (Liang-Barsky), the end on an edge exactly on it
		double lo = -INFINITY, hi = INFINITY;
		int lo_edge = -1, hi_edge = -1;
		const double p[4] = {-c.dx, c.dx, -c.dy, c.dy};
		const double q[4] = {c.g->sx - x0, x1 - c.g->sx, c.g->sy - y0, y1 - c.g->sy};
		for (int k = 0; k < 4; k++) {
			if (p[k] == 0.0) {
				if (q[k] < 0.0) {
					c.empty = true;
				}
				continue;
			}
			const double t = q[k] / p[k];
			if (p[k] < 0.0 && t > lo) {
				lo = t;
				lo_edge = k;
			} else if (p[k] > 0.0 && t < hi) {
				hi = t;
				hi_edge = k;
			}
		}
		if (c.empty || !(lo < hi)) {
			c.empty = true;
			continue;
		}
		c.t0 = lo;
		c.t1 = hi;
		auto end = [&](double t, int edge) {
			double x = c.g->sx + t * c.dx, y = c.g->sy + t * c.dy;
			const double edges[4] = {x0, x1, y0, y1};
			if (edge < 2) {
				x = edges[edge];
			} else {
				y = edges[edge];
			}
			return round2(x, y);
		};
		c.p0 = end(lo, lo_edge);
		c.p1 = end(hi, hi_edge);
	}
	// the crossings of every two carriers, each rounded once (both carriers hold the same point)
	auto param = [&](const Carrier &c, double x, double y) {
		return c.g->line ? ((x - c.g->sx) * c.dx + (y - c.g->sy) * c.dy) / (c.dx * c.dx + c.dy * c.dy) : std::atan2(y - c.g->cy, x - c.g->cx);
	};
	auto add = [&](size_t a, size_t b, double x, double y) {
		const V2 pt = round2(x, y);
		for (const size_t k : {a, b}) {
			Carrier &c = cs[k];
			const double t = param(c, x, y);
			if (c.g->line && (c.empty || !(t > c.t0 && t < c.t1))) {
				continue;
			}
			c.at.push_back({t, pt});
		}
	};
	for (size_t a = 0; a < cs.size(); a++) {
		for (size_t b = a + 1; b < cs.size(); b++) {
			const Region &A = *cs[a].g, &B = *cs[b].g;
			if (A.line && B.line) {
				const double den = cs[a].dx * cs[b].dy - cs[a].dy * cs[b].dx;
				if (den == 0.0) {
					continue;
				}
				const double t = ((B.sx - A.sx) * cs[b].dy - (B.sy - A.sy) * cs[b].dx) / den;
				add(a, b, A.sx + t * cs[a].dx, A.sy + t * cs[a].dy);
			} else if (A.line != B.line) {
				const size_t li = A.line ? a : b, ci = A.line ? b : a;
				const Region &L = *cs[li].g, &C = *cs[ci].g;
				const double dx = cs[li].dx, dy = cs[li].dy;
				const double fx = L.sx - C.cx, fy = L.sy - C.cy;
				const double qa = dx * dx + dy * dy, qb = 2 * (fx * dx + fy * dy), qc = fx * fx + fy * fy - C.r * C.r;
				const double disc = qb * qb - 4 * qa * qc;
				if (disc < 0.0) {
					continue;
				}
				const double sq = std::sqrt(disc);
				for (const double t : disc == 0.0 ? std::vector<double>{-qb / (2 * qa)} : std::vector<double>{(-qb - sq) / (2 * qa), (-qb + sq) / (2 * qa)}) {
					add(li, ci, L.sx + t * dx, L.sy + t * dy);
				}
			} else {
				const double dx = B.cx - A.cx, dy = B.cy - A.cy;
				const double d = std::hypot(dx, dy);
				if (d == 0.0 || d > A.r + B.r || d < std::fabs(A.r - B.r)) {
					continue;
				}
				const double l = (A.r * A.r - B.r * B.r + d * d) / (2 * d);
				const double h2 = A.r * A.r - l * l;
				const double h = h2 > 0.0 ? std::sqrt(h2) : 0.0;
				const double mx = A.cx + l * dx / d, my = A.cy + l * dy / d;
				add(a, b, mx + h * dy / d, my - h * dx / d);
				if (h > 0.0) {
					add(a, b, mx - h * dy / d, my + h * dx / d);
				}
			}
		}
	}
	// the pieces, carrier by carrier
	std::vector<Piece> out;
	auto keep = [&](size_t h, Piece p) {
		SurfaceSpec probe = p.spec;
		SideSpec wall;
		wall.effect.raw = {{"type", "terminal"}};
		wall.effect.type = "terminal";
		probe.left = probe.right = wall;
		if (!validate_spec(probe).empty()) {
			return; // (a sliver)
		}
		const rc::arc::SegGeom g = segment_of(p.spec);
		const Region &c = *cs[h].g;
		std::pair<double, double> ln{0.0, 0.0};
		bool li = false;
		if (c.line) {
			ln = left_normal(g);
		} else {
			li = left_inside(g, c.cx, c.cy, c.r, p.mx, p.my);
		}
		for (size_t k = 0; k < regions.size(); k++) {
			if (group[k] == h) {
				const Region &r = regions[k];
				p.own.push_back({k, r.line ? ln.first * r.nx + ln.second * r.ny > 0.0 : li == r.inside});
			}
		}
		out.push_back(std::move(p));
	};
	for (size_t h = 0; h < cs.size(); h++) {
		Carrier &c = cs[h];
		std::sort(c.at.begin(), c.at.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
		if (c.g->line) {
			if (c.empty) {
				continue;
			}
			std::vector<V2> pts{c.p0};
			for (const auto &[t, p] : c.at) {
				pts.push_back(p);
			}
			pts.push_back(c.p1);
			for (size_t i = 0; i + 1 < pts.size(); i++) {
				if (pts[i] == pts[i + 1]) {
					continue;
				}
				Piece p;
				p.spec.start = pts[i];
				p.spec.end = pts[i + 1];
				p.mx = (double(pts[i].x) + pts[i + 1].x) / 2;
				p.my = (double(pts[i].y) + pts[i + 1].y) / 2;
				p.spec.via = V2::of(p.mx, p.my);
				p.spec.straight = true;
				keep(h, std::move(p));
			}
			continue;
		}
		std::vector<std::pair<double, V2>> at;
		for (const auto &q : c.at) {
			if (at.empty() || !(at.back().second == q.second)) {
				at.push_back(q);
			}
		}
		while (at.size() > 1 && at.back().second == at.front().second) {
			at.pop_back();
		}
		if (at.size() < 2) {
			Piece p;
			p.spec.full_circle = true;
			p.spec.center = V2::of(c.g->cx, c.g->cy);
			p.spec.radius = c.g->r;
			p.mx = c.g->cx + c.g->r;
			p.my = c.g->cy;
			keep(h, std::move(p));
			continue;
		}
		for (size_t i = 0; i < at.size(); i++) {
			const auto &a = at[i], &b = at[(i + 1) % at.size()];
			const double end = i + 1 < at.size() ? b.first : b.first + 2 * PI;
			const double mid = (a.first + end) / 2;
			Piece p;
			p.spec.start = a.second;
			p.spec.end = b.second;
			p.mx = c.g->cx + c.g->r * std::cos(mid);
			p.my = c.g->cy + c.g->r * std::sin(mid);
			p.spec.via = V2::of(p.mx, p.my);
			keep(h, std::move(p));
		}
	}
	return out;
}

std::vector<BuiltSurface> boundary(const std::vector<Piece> &pieces, const std::vector<Region> &regions, const std::vector<bool> &active,
		int first_id) {
	std::vector<BuiltSurface> out;
	for (size_t i = 0; i < pieces.size(); i++) {
		const Piece &p = pieces[i];
		bool shadow[2];
		for (int side = 0; side < 2; side++) {
			bool dark = false, light = false;
			for (size_t k = 0; k < regions.size(); k++) {
				if (!active[k]) {
					continue;
				}
				bool in;
				auto own = std::find_if(p.own.begin(), p.own.end(), [&](const auto &o) { return o.first == k; });
				if (own != p.own.end()) {
					in = side == 0 ? own->second : !own->second;
				} else {
					in = regions[k].contains(p.mx, p.my);
				}
				if (in) {
					(regions[k].dark ? dark : light) = true;
				}
			}
			shadow[side] = dark && !light;
		}
		if (shadow[0] == shadow[1]) {
			continue;
		}
		SurfaceSpec s = p.spec;
		SideSpec wall, pass;
		wall.effect.raw = {{"type", "terminal"}};
		wall.effect.type = "terminal";
		s.left = shadow[0] ? pass : wall; // the lit side stops the arrow
		s.right = shadow[0] ? wall : pass;
		out.push_back(build_spec(s, first_id + int(i), std::nullopt).front());
	}
	return out;
}

rc::vis::Fill fill(const std::vector<Region> &regions, const std::vector<bool> &active, const Rect &bounds) {
	const double x0 = bounds.x, y0 = bounds.y, x1 = bounds.x + bounds.w, y1 = bounds.y + bounds.h;
	const Clipper2Lib::PathD rect{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
	Paths dark, light;
	for (size_t k = 0; k < regions.size(); k++) {
		if (!active[k]) {
			continue;
		}
		const Region &r = regions[k];
		Paths shape;
		if (r.line) {
			Clipper2Lib::PathD poly; // the bounds clipped by the half-plane
			for (size_t i = 0; i < rect.size(); i++) {
				const Clipper2Lib::PointD &a = rect[i], &b = rect[(i + 1) % rect.size()];
				const double fa = (a.x - r.sx) * r.nx + (a.y - r.sy) * r.ny, fb = (b.x - r.sx) * r.nx + (b.y - r.sy) * r.ny;
				if (fa >= 0.0) {
					poly.push_back(a);
				}
				if ((fa >= 0.0) != (fb >= 0.0)) {
					const double t = fa / (fa - fb);
					poly.push_back({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)});
				}
			}
			if (poly.size() >= 3) {
				shape.push_back(poly);
			}
		} else {
			const int n = std::clamp(int(std::ceil(2 * PI * r.r / 6.0)), 24, 256);
			Clipper2Lib::PathD disk;
			for (int i = 0; i < n; i++) {
				disk.push_back({r.cx + r.r * std::cos(2 * PI * i / n), r.cy + r.r * std::sin(2 * PI * i / n)});
			}
			shape = r.inside ? Paths{disk} : run(Clipper2Lib::ClipType::Difference, {rect}, {disk});
		}
		Paths &into = r.dark ? dark : light;
		into.insert(into.end(), shape.begin(), shape.end());
	}
	rc::vis::Fill out;
	if (dark.empty()) {
		return out;
	}
	const Paths d = run(Clipper2Lib::ClipType::Union, dark, {});
	const Paths s = light.empty() ? d : run(Clipper2Lib::ClipType::Difference, d, run(Clipper2Lib::ClipType::Union, light, {}));
	Clipper2Lib::ClipperD c(4);
	c.AddSubject(s);
	c.AddClip({rect});
	Clipper2Lib::PolyTreeD tree;
	c.Execute(Clipper2Lib::ClipType::Intersection, Clipper2Lib::FillRule::NonZero, tree);
	rc::vis::Outline o;
	std::function<void(const Clipper2Lib::PolyPathD &, int)> walk = [&](const Clipper2Lib::PolyPathD &node, int outer) {
		for (const auto &child : node) {
			std::vector<rc::vis::Pt> ring;
			for (const Clipper2Lib::PointD &q : child->Polygon()) {
				ring.push_back(rc::vis::Pt{q.x, q.y});
			}
			const bool hole = child->IsHole();
			if (ring.size() >= 3) {
				const double a = ring_area(ring);
				if ((hole && a > 0.0) || (!hole && a < 0.0)) {
					std::reverse(ring.begin(), ring.end());
				}
				o.rings.push_back(std::move(ring));
				o.parent.push_back(hole ? outer : -1);
			}
			walk(*child, hole ? outer : int(o.rings.size()) - 1);
		}
	};
	walk(tree, -1);
	return rc::vis::fill_of(o);
}

V2 point_of(const rc::arc::SegGeom &g) {
	if (g.full) {
		const rc::arc::GeneralizedCircle &c = g.get_carrier();
		return V2::of(c.center_x() + c.radius(), c.center_y());
	}
	if (g.is_line()) {
		return V2::of((double(g.s.x) + g.e.x) / 2, (double(g.s.y) + g.e.y) / 2);
	}
	return g.v;
}

BuiltSurface darkened(const BuiltSurface &s) {
	BuiltSurface d = s;
	d.left.reset();
	d.right.reset();
	d.resolver.clear();
	d.resolver_key.clear();
	return d;
}

} // namespace rcg::shade
