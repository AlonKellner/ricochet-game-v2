// Precision-generic segment geometry (Stage 92): the port of
// scripts/math/precision/beam_geom.gd. Every decision (on-segment, side,
// endpoint blockage) is computed at the tier's precision:
//   side: approach = -dir·∇f(P), winding = -(E - S) × ∇f(V); LEFT iff (winding >= 0) == (approach > 0)
//   on-segment: Re(CR(S, P; E, V)) >= 0 in homogeneous coordinates (V at infinity for full lines)
//   endpoint blocked sides: sign of dir × (tangent into the segment)
#pragma once

#include "rc/beam.hpp"

#include <array>
#include <optional>

namespace rc {

enum class SegKind { Line, Circle };

// Plain data of a segment as the scene dumps describe it (Variant-free).
struct SegDef {
	int id = 0;
	SegKind kind = SegKind::Line;
	bool full = false;
	double sx = 0, sy = 0, ex = 0, ey = 0;
	bool has_v = false;
	double vx = 0, vy = 0;
	double cx = 0, cy = 0, r = 0; // full circles: the carrier's exact center and radius
};

template <Num N>
struct Seg {
	int id = 0;
	SegKind kind = SegKind::Line;
	bool full = false;
	CN<N> s;
	CN<N> e;
	std::optional<CN<N>> v; // nullopt = the point at infinity
	std::optional<Circle<N>> circ;
	mutable std::optional<bool> wind_nonneg; // cached sign of winding (constant)

	const CN<N> &c() const { return circ->c; }
};

namespace geom {

// Center of the circle through A, B, C (at the tier's precision).
template <Num N>
CN<N> circumcenter(const CN<N> &A, const CN<N> &B, const CN<N> &C) {
	const CN<N> ab = B.sub(A);
	const CN<N> ac = C.sub(A);
	const N d = CN<N>::cross(ab, ac).mul_pow2(1);
	const N ab2 = ab.abs2();
	const N ac2 = ac.abs2();
	const N ux = ac.im.mul(ab2).sub(ab.im.mul(ac2)).div(d);
	const N uy = ab.re.mul(ac2).sub(ac.re.mul(ab2)).div(d);
	return A.add(CN<N>{ux, uy});
}

// `like` provides the tier (its limb count for BigFloat).
template <Num N>
Seg<N> seg_from_def(const SegDef &d, const N &like) {
	Seg<N> sg;
	sg.id = d.id;
	sg.s = CN<N>{like.like(d.sx), like.like(d.sy)};
	sg.e = CN<N>{like.like(d.ex), like.like(d.ey)};
	if (d.has_v) {
		sg.v = CN<N>{like.like(d.vx), like.like(d.vy)};
	}
	sg.full = d.full;
	sg.kind = d.kind;
	if (d.kind == SegKind::Circle) {
		if (d.full) {
			sg.circ = Circle<N>::make(d.id, CN<N>{like.like(d.cx), like.like(d.cy)}, like.like(d.r));
		} else {
			const CN<N> c = circumcenter(sg.s, *sg.v, sg.e);
			sg.circ = Circle<N>::make(d.id, c, sg.s.sub(c).abs_val());
		}
	}
	return sg;
}

template <Num N>
std::array<N, 3> line_coeffs(const Beam<N> &beam, const Seg<N> &seg) {
	const CN<N> D = seg.e.sub(seg.s);
	const CN<N> g{D.im.neg(), D.re};
	const N fp = CN<N>::cross(D, beam.p.sub(seg.s));
	const N &k = beam.kappa;
	const N c1 = CN<N>::dot(g, beam.tau);
	const N c2 = fp.mul(k.mul(k)).mul_pow2(-2).add(k.mul_pow2(-1).mul(CN<N>::cross(beam.tau, g)));
	return {fp, c1, c2};
}

// Real roots t of beam ∩ seg's carrier. depart_on: beam.p lies exactly on it
// (provenance), so the root t = 0 is the departure and is not returned.
template <Num N>
InlineVec<N, 4> hits(const Beam<N> &beam, const Seg<N> &seg, bool depart_on) {
	if (seg.kind == SegKind::Circle) {
		return beam.hits(*seg.circ, depart_on);
	}
	const auto co = line_coeffs(beam, seg);
	const N &c0 = co[0];
	const N &c1 = co[1];
	const N &c2 = co[2];
	// degree drop (c2 = 0): a curved beam meets the line at its antipode, t = inf
	InlineVec<N, 4> at_inf;
	if (!beam.kappa.is_zero()) {
		at_inf.push_back(beam.kappa.inf_like());
	}
	if (depart_on) {
		if (c2.is_zero()) {
			return at_inf;
		}
		return {c1.neg().div(c2)};
	}
	if (c2.is_zero()) {
		if (c1.is_zero()) {
			return at_inf;
		}
		InlineVec<N, 4> out{c0.neg().div(c1)};
		for (const N &t : at_inf) {
			out.push_back(t);
		}
		return out;
	}
	const N disc = c1.mul(c1).sub(c2.mul(c0).mul_pow2(2));
	if (disc.is_negative()) {
		return {};
	}
	const N s = disc.sqrt_val();
	const N big = !c1.is_negative() ? c1.add(s).neg() : c1.sub(s).neg();
	if (big.is_zero()) {
		return {c0.sub(c0)};
	}
	return {big.div(c2.mul_pow2(1)), c0.mul_pow2(1).div(big)};
}

// Stage 206 (GAME_SPEC 12.1): the counterpart's carriers -- a full circle or a full
// line as plain segments (nothing beyond what `hits` reads), and the inversion
// about Q (R2 = its radius squared) of a point, a circle and a line.
template <Num N>
Seg<N> circle_seg(const CN<N> &c, const N &r) {
	Seg<N> sg;
	sg.id = -1;
	sg.kind = SegKind::Circle;
	sg.full = true;
	sg.s = c.add(CN<N>{r, r.sub(r)});
	sg.e = sg.s;
	sg.circ = Circle<N>::make(-1, c, r);
	return sg;
}

template <Num N>
Seg<N> line_seg(const CN<N> &a, const CN<N> &b) {
	Seg<N> sg;
	sg.id = -1;
	sg.kind = SegKind::Line;
	sg.full = true;
	sg.s = a;
	sg.e = b;
	return sg;
}

// the image of z (not Q)
template <Num N>
CN<N> invert_about(const CN<N> &z, const CN<N> &Q, const N &R2) {
	const CN<N> u = z.sub(Q);
	return Q.add(u.scale(R2).div_real(u.abs2()));
}

// the image of the circle (c, r2 = r^2) not through Q: (centre, radius^2)
template <Num N>
std::pair<CN<N>, N> invert_circle(const CN<N> &c, const N &r2, const CN<N> &Q, const N &R2) {
	const CN<N> d = c.sub(Q);
	const N k = R2.div(d.abs2().sub(r2));
	return {Q.add(d.scale(k)), k.mul(k).mul(r2)};
}

// the image of the line a b not through Q: the circle through Q, (centre, radius^2)
template <Num N>
std::pair<CN<N>, N> invert_line_not_through(const CN<N> &a, const CN<N> &b, const CN<N> &Q, const N &R2) {
	const CN<N> D = b.sub(a);
	const CN<N> F = a.add(D.scale(CN<N>::dot(Q.sub(a), D).div(D.abs2()))); // the foot of Q on the line
	const CN<N> w = F.sub(Q);
	const N d2 = w.abs2();
	return {Q.add(w.scale(R2.div(d2.mul_pow2(1)))), R2.mul(R2).div(d2.mul_pow2(2))};
}

// zA·wB - zB·wA with integer homogeneous weights (0 or 1)
template <Num N>
CN<N> hdet(const CN<N> &zA, int wA, const CN<N> &zB, int wB) {
	if (wA == 1 && wB == 1) {
		return zA.sub(zB);
	}
	if (wB == 1) {
		return zA; // wA == 0
	}
	return zB.neg(); // wB == 0, wA == 1
}

// Re(cross ratio); on-segment iff >= 0.
template <Num N>
N on_segment_value(const Seg<N> &seg, const CN<N> &p) {
	CN<N> zv;
	int wv;
	if (!seg.v) {
		zv = CN<N>{p.re.sub(p.re).add(p.re.one_like()), p.re.sub(p.re)};
		wv = 0;
	} else {
		zv = *seg.v;
		wv = 1;
	}
	const CN<N> sv = hdet(seg.s, 1, zv, wv);
	const CN<N> ep = hdet(seg.e, 1, p, 1);
	const CN<N> sp = hdet(seg.s, 1, p, 1);
	const CN<N> ev = hdet(seg.e, 1, zv, wv);
	const CN<N> num = sv.mul(ep);
	const CN<N> den = sp.mul(ev);
	return num.re.mul(den.re).add(num.im.mul(den.im));
}

template <Num N>
bool on_segment(const Seg<N> &seg, const CN<N> &p) {
	if (seg.full) {
		return true;
	}
	return !on_segment_value(seg, p).is_negative();
}

template <Num N>
bool eq(const CN<N> &a, const CN<N> &b) {
	return a.re.cmp(b.re) == 0 && a.im.cmp(b.im) == 0;
}

// 1 / 2 if p is exactly the start / end endpoint (never for full segments).
template <Num N>
int endpoint_at(const Seg<N> &seg, const CN<N> &p) {
	if (seg.full) {
		return 0;
	}
	if (eq(p, seg.s)) {
		return 1;
	}
	if (eq(p, seg.e)) {
		return 2;
	}
	return 0;
}

template <Num N>
CN<N> grad(const Seg<N> &seg, const CN<N> &p) {
	if (seg.kind == SegKind::Line) {
		const CN<N> D = seg.e.sub(seg.s);
		return CN<N>{D.im.neg(), D.re};
	}
	return p.sub(seg.circ->c).scale(p.re.one_like().mul_pow2(1));
}

template <Num N>
N winding(const Seg<N> &seg) {
	const CN<N> g = seg.kind == SegKind::Line ? grad(seg, seg.s) : grad(seg, *seg.v);
	return CN<N>::cross(seg.e.sub(seg.s), g).neg();
}

// Side the beam (travel direction dir at p) comes from: true = LEFT.
template <Num N>
bool side_left(const Seg<N> &seg, const CN<N> &p, const CN<N> &dir) {
	const N approach = CN<N>::dot(dir, grad(seg, p)).neg();
	const bool approach_pos = !approach.is_negative() && !approach.is_zero();
	if (!seg.wind_nonneg) {
		seg.wind_nonneg = !winding(seg).is_negative();
	}
	return *seg.wind_nonneg ? approach_pos : !approach_pos;
}

template <Num N>
CN<N> tangent_into(const Seg<N> &seg, int which) {
	const CN<N> &ep = which == 1 ? seg.s : seg.e;
	if (seg.kind == SegKind::Line) {
		return which == 1 ? seg.e.sub(seg.s) : seg.s.sub(seg.e);
	}
	const CN<N> rad = ep.sub(seg.circ->c);
	const CN<N> ccw{rad.im.neg(), rad.re};
	const CN<N> cw{rad.im, rad.re.neg()};
	const CN<N> ref = seg.v->sub(ep);
	return CN<N>::dot(ccw, ref).cmp(CN<N>::dot(cw, ref)) >= 0 ? ccw : cw;
}

struct Blocked {
	bool left;
	bool right;
};

// Blocked sides for a beam through endpoint `which`.
template <Num N>
Blocked blocked_sides(const Seg<N> &seg, int which, const CN<N> &dir) {
	const N c = CN<N>::cross(dir, tangent_into(seg, which));
	if (c.is_zero()) {
		return {true, true};
	}
	if (!c.is_negative()) {
		return {false, true};
	}
	return {true, false};
}

} // namespace geom
} // namespace rc
