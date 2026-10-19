#include "rcg/geom.hpp"

#include "rc/exact.hpp"

#include <algorithm>
#include <cmath>

namespace rcg {
namespace {

using rc::arc::GeneralizedCircle;

struct K {
	double a, b, c, d;
};

K coef(const SegGeom &seg) {
	const GeneralizedCircle &g = seg.get_carrier();
	return {g.a, g.b, g.c, g.d};
}

std::array<double, 2> center_xy(const K &k) { return {-k.b / (2.0 * k.a), -k.c / (2.0 * k.a)}; }

double r2_of(const K &k) { return (k.b * k.b + k.c * k.c - 4.0 * k.a * k.d) / (4.0 * k.a * k.a); }

// +1: start -> via -> end turns toward +90 degrees (float64 on the components)
double turn(const SegGeom &seg) {
	const double sx = seg.s.x, sy = seg.s.y, vx = seg.v.x, vy = seg.v.y, ex = seg.e.x, ey = seg.e.y;
	return (vx - sx) * (ey - vy) - (vy - sy) * (ex - vx) > 0.0 ? 1.0 : -1.0;
}

double angle_param(const SegGeom &seg, const K &k, double px, double py) {
	const auto c = center_xy(k);
	const double a0 = std::atan2(double(seg.s.y) - c[1], double(seg.s.x) - c[0]);
	const double a = std::atan2(py - c[1], px - c[0]);
	return fposmod((a - a0) * turn(seg), TAU);
}

// float64 [x, y] pairs: the carriers' intersections (none: parallel, disjoint, tangent)
std::vector<std::array<double, 2>> carrier_points(const K &ka, const K &kb, const SegGeom &a, const SegGeom &b) {
	if (ka.a == 0.0 && kb.a == 0.0) {
		const double det = ka.b * kb.c - kb.b * ka.c;
		if (det == 0.0) {
			return {};
		}
		return {{(ka.c * kb.d - kb.c * ka.d) / det, (kb.b * ka.d - ka.b * kb.d) / det}};
	}
	if (ka.a == 0.0 || kb.a == 0.0) {
		const SegGeom &line_seg = ka.a == 0.0 ? a : b;
		const K &kc = ka.a == 0.0 ? kb : ka;
		const auto c = center_xy(kc);
		const double r2 = r2_of(kc);
		const double sx = line_seg.s.x, sy = line_seg.s.y;
		const double dx = double(line_seg.e.x) - sx;
		const double dy = double(line_seg.e.y) - sy;
		const double fx = sx - c[0];
		const double fy = sy - c[1];
		const double qa = dx * dx + dy * dy;
		const double qb = 2.0 * (dx * fx + dy * fy);
		const double qc = fx * fx + fy * fy - r2;
		const double disc = qb * qb - 4.0 * qa * qc;
		if (disc <= 0.0) {
			return {};
		}
		const double sq = std::sqrt(disc);
		std::vector<std::array<double, 2>> out;
		for (const double t : {(-qb - sq) / (2.0 * qa), (-qb + sq) / (2.0 * qa)}) {
			out.push_back({sx + t * dx, sy + t * dy});
		}
		return out;
	}
	const auto c1 = center_xy(ka);
	const auto c2 = center_xy(kb);
	const double r1s = r2_of(ka);
	const double r2s = r2_of(kb);
	const double ux = c2[0] - c1[0];
	const double uy = c2[1] - c1[1];
	const double d2 = ux * ux + uy * uy;
	if (d2 == 0.0) {
		return {};
	}
	const double d = std::sqrt(d2);
	const double r1 = std::sqrt(r1s);
	const double r2 = std::sqrt(r2s);
	if (d >= r1 + r2 || d <= std::abs(r1 - r2)) {
		return {};
	}
	const double m = (r1s - r2s + d2) / (2.0 * d);
	const double h = std::sqrt(std::max(r1s - m * m, 0.0));
	const double bx = c1[0] + m * ux / d;
	const double by = c1[1] + m * uy / d;
	return {{bx - h * uy / d, by + h * ux / d}, {bx + h * uy / d, by - h * ux / d}};
}

struct Interior {
	bool in;
	double t;
};

Interior interior(const SegGeom &seg, const K &k, double px, double py) {
	if (k.a == 0.0) {
		const double sx = seg.s.x, sy = seg.s.y;
		const double dx = double(seg.e.x) - sx;
		const double dy = double(seg.e.y) - sy;
		const double t = ((px - sx) * dx + (py - sy) * dy) / (dx * dx + dy * dy);
		return {t > 0.0 && t < 1.0, t};
	}
	const double t2 = angle_param(seg, k, px, py);
	if (seg.full) {
		return {true, t2};
	}
	return {t2 > 0.0 && t2 < span(seg), t2};
}

// the carrier's unit travel tangent (float32, as the Vector2 it is)
V2 tangent(const SegGeom &seg, const K &k, double px, double py) {
	if (k.a == 0.0) {
		return (seg.e - seg.s).normalized();
	}
	const auto c = center_xy(k);
	return V2::of(-(py - c[1]), px - c[0]).normalized() * float(turn(seg));
}

bool is_portal(const BuiltSurface &s) { return s.portal_pair != 0; }

} // namespace

// A point on the carrier between a and b (Pieces._between).
V2 between(const SegGeom &seg, const V2 &a, const V2 &b) {
	const GeneralizedCircle &g = seg.get_carrier();
	if (g.is_line()) {
		return V2::of((double(a.x) + double(b.x)) / 2.0, (double(a.y) + double(b.y)) / 2.0);
	}
	const double cx = -g.b / (2.0 * g.a);
	const double cy = -g.c / (2.0 * g.a);
	const double r = std::sqrt((g.b * g.b + g.c * g.c - 4.0 * g.a * g.d) / (4.0 * g.a * g.a));
	const double ta = param(seg, a);
	double tb = param(seg, b);
	if (tb <= ta) {
		tb += TAU;
	}
	const double a0 = std::atan2(double(seg.s.y) - cy, double(seg.s.x) - cx);
	const double ang = a0 + turn(seg) * (ta + tb) / 2.0;
	return V2::of(cx + r * std::cos(ang), cy + r * std::sin(ang));
}

double fposmod(double x, double y) {
	double value = std::fmod(x, y);
	if ((value < 0 && y > 0) || (value > 0 && y < 0)) {
		value += y;
	}
	value += 0.0;
	return value;
}

double param(const SegGeom &seg, const V2 &p) {
	const K k = coef(seg);
	if (k.a == 0.0) {
		const V2 d = seg.e - seg.s;
		const double dx = d.x, dy = d.y;
		return ((double(p.x) - double(seg.s.x)) * dx + (double(p.y) - double(seg.s.y)) * dy) / (dx * dx + dy * dy);
	}
	return angle_param(seg, k, p.x, p.y);
}

double span(const SegGeom &seg) {
	if (seg.full) {
		return TAU;
	}
	const K k = coef(seg);
	if (k.a == 0.0) {
		return 1.0;
	}
	return angle_param(seg, k, seg.e.x, seg.e.y);
}

bool incident(const SegGeom &seg, const V2 &p) {
	const GeneralizedCircle &g = seg.get_carrier();
	if (g.is_line()) {
		return rc::exact::orient_zero(seg.s.x, seg.s.y, seg.e.x, seg.e.y, p.x, p.y);
	}
	if (seg.full) {
		const K k = coef(seg);
		const auto c = center_xy(k);
		return rc::exact::on_circle_zero(c[0], c[1], std::sqrt(r2_of(k)), p.x, p.y);
	}
	return rc::exact::incircle_zero(seg.s.x, seg.s.y, seg.v.x, seg.v.y, seg.e.x, seg.e.y, p.x, p.y);
}

std::vector<Crossing> crossings_of(const SegGeom &a, const SegGeom &b) {
	const K ka = coef(a);
	const K kb = coef(b);
	std::vector<V2> touching;
	for (const auto &[own, other] : {std::pair{&a, &b}, std::pair{&b, &a}}) {
		if (own->full) {
			continue;
		}
		for (const V2 &ep : {own->s, own->e}) {
			if (incident(*other, ep)) {
				touching.push_back(ep);
			}
		}
	}
	std::vector<Crossing> out;
	for (const auto &p : carrier_points(ka, kb, a, b)) {
		const double px = p[0], py = p[1];
		const Interior ia = interior(a, ka, px, py);
		const Interior ib = interior(b, kb, px, py);
		if (!(ia.in && ib.in)) {
			continue;
		}
		const V2 point = V2::of(px, py);
		bool at_end = false;
		for (const V2 &ep : touching) {
			// ep.distance_to(point) <= 1e-6 * maxf(1.0, ep.length()): float32 lengths, the bound in float64
			at_end = at_end || double(ep.distance_to(point)) <= 1e-6 * std::max(1.0, double(ep.length()));
		}
		if (at_end) {
			continue;
		}
		const V2 grad_b = V2::of(2.0 * kb.a * px + kb.b, 2.0 * kb.a * py + kb.c);
		out.push_back({point, ia.t, ib.t, tangent(a, ka, px, py).dot(grad_b) < 0.0f});
	}
	return out;
}

bool Piece::contains(const V2 &p) const {
	if (!span) {
		return true;
	}
	const double t = param(segment, p);
	return t >= 0.0 && t <= rcg::span(segment);
}

const std::vector<Piece> &Pieces::of(int id) const {
	static const std::vector<Piece> none;
	for (const auto &e : by_id) {
		if (e.first == id) {
			return e.second;
		}
	}
	return none;
}

const Piece *Pieces::piece(int id, int index) const {
	for (const Piece &p : of(id)) {
		if (p.index == index) {
			return &p;
		}
	}
	return nullptr;
}

Pieces build_pieces(const std::vector<BuiltSurface> &surfaces) {
	Pieces out;
	std::vector<std::pair<int, std::vector<std::pair<double, V2>>>> cuts; // id -> [t, point] (first-cut order)
	auto add_cut = [&](int id, double t, const V2 &pt) {
		auto it = std::find_if(cuts.begin(), cuts.end(), [&](const auto &e) { return e.first == id; });
		if (it == cuts.end()) {
			cuts.push_back({id, {}});
			it = cuts.end() - 1;
		}
		it->second.push_back({t, pt});
	};
	for (size_t i = 0; i < surfaces.size(); i++) {
		const BuiltSurface &a = surfaces[i];
		if (a.pre_transform) {
			continue;
		}
		for (size_t j = i + 1; j < surfaces.size(); j++) {
			const BuiltSurface &b = surfaces[j];
			if (b.pre_transform) {
				continue;
			}
			const std::vector<Crossing> xs = crossings_of(a.seg, b.seg);
			if (xs.empty()) {
				continue;
			}
			if (is_portal(a) || is_portal(b)) {
				out.portal_crossings.push_back("surface " + std::to_string(is_portal(a) ? b.id : a.id) + " crosses portal " +
					std::to_string(is_portal(a) ? a.id : b.id));
				continue;
			}
			for (const Crossing &x : xs) {
				add_cut(a.id, x.ta, x.point);
				add_cut(b.id, x.tb, x.point);
				out.crossings.push_back(x.point);
			}
		}
	}
	for (const BuiltSurface &s : surfaces) {
		std::vector<Piece> pieces;
		auto it = std::find_if(cuts.begin(), cuts.end(), [&](const auto &e) { return e.first == s.id; });
		const bool cuttable = !s.pre_transform && !is_portal(s);
		if (it == cuts.end() || !cuttable) {
			pieces.push_back(Piece{s.id, -1, s.seg, std::nullopt});
			out.by_id.push_back({s.id, std::move(pieces)});
			continue;
		}
		std::vector<std::pair<double, V2>> c = it->second;
		std::sort(c.begin(), c.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
		std::vector<V2> pts;
		for (const auto &x : c) {
			if (pts.empty() || pts.back() != x.second) {
				pts.push_back(x.second); // concurrent crossings share one point
			}
		}
		std::vector<std::pair<V2, V2>> ends;
		if (s.seg.full) {
			if (pts.size() < 2) {
				out.by_id.push_back({s.id, {Piece{s.id, -1, s.seg, std::nullopt}}});
				continue;
			}
			for (size_t i = 0; i < pts.size(); i++) {
				ends.push_back({pts[i], pts[(i + 1) % pts.size()]});
			}
		} else {
			std::vector<V2> all{s.seg.s};
			all.insert(all.end(), pts.begin(), pts.end());
			all.push_back(s.seg.e);
			for (size_t i = 0; i + 1 < all.size(); i++) {
				ends.push_back({all[i], all[i + 1]});
			}
		}
		const GeneralizedCircle carrier = s.seg.get_carrier();
		for (size_t i = 0; i < ends.size(); i++) {
			const V2 &a = ends[i].first;
			const V2 &b = ends[i].second;
			const V2 v = between(s.seg, a, b);
			SegGeom seg = SegGeom::make(a, b, v);
			seg.carrier = carrier;
			pieces.push_back(Piece{s.id, int(i), seg, std::array<double, 6>{a.x, a.y, b.x, b.y, v.x, v.y}});
		}
		out.by_id.push_back({s.id, std::move(pieces)});
	}
	return out;
}

bool span_contains(const BuiltSurface &s, const std::array<double, 6> &sp, const V2 &p) {
	if (p.is_inf() || std::isnan(p.x) || std::isnan(p.y)) {
		return false;
	}
	SegGeom seg = SegGeom::make(V2::of(sp[0], sp[1]), V2::of(sp[2], sp[3]), V2::of(sp[4], sp[5]));
	seg.carrier = s.seg.get_carrier();
	const double t = param(seg, p);
	return t >= 0.0 && t <= span(seg);
}

} // namespace rcg
