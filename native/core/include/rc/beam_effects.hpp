// Beam maps of the transformative effects, precision-generic (Stage 92): the
// port of scripts/math/precision/beam_effects.gd.
//   reflection in a line (D = E - S, u² = D²/|D|²): h' = h, τ' = u²·conj(τ_h), κ' = −κ
//   reflection on a circle carrier = inversion in the carrier; inversion: Beam::invert_at
//   portal: p' = a h + d, τ' = a τ_h, κ' = κ; projective: a straight beam from h along ±axis
// Point maps (anchored chains): the same transforms on points, nullopt for infinity.
#pragma once

#include "rc/scene.hpp"

#include <cmath>
#include <limits>
#include <type_traits>
#include <optional>
#include <utility>
#include <vector>

namespace rc {
namespace effects {

template <Num N>
CN<N> u2(const Seg<N> &seg) {
	const CN<N> D = seg.e.sub(seg.s);
	return D.mul(D).div_real(D.abs2());
}

template <Num N>
Beam<N> reflect_line(const Beam<N> &beam, const N &t, const Seg<N> &seg, const CN<N> *h_exact = nullptr) {
	const CN<N> h = h_exact == nullptr ? beam.at(t) : *h_exact;
	const CN<N> th = beam.tangent_at(t);
	const CN<N> tau2 = u2(seg).mul(th.conj()).unit();
	return Beam<N>{h, tau2, beam.kappa.neg()};
}

// Portal: the rigid image of the beam, departing the paired surface at T(h).
template <Num N>
Beam<N> portal(const Beam<N> &beam, const N &t, const Surf<N> &surf, const CN<N> *h_exact = nullptr) {
	const CN<N> h = h_exact == nullptr ? beam.at(t) : *h_exact;
	return Beam<N>{surf.alpha.mul(h).add(surf.dvec), surf.alpha.mul(beam.tangent_at(t)), beam.kappa};
}

// h: the hit point when known exactly by provenance; nullptr: beam.at(t).
template <Num N>
Beam<N> apply(const Beam<N> &beam, const N &t, const Surf<N> &surf, const CN<N> *h = nullptr) {
	if (surf.portal) {
		return portal(beam, t, surf, h);
	}
	if (surf.seg.kind == SegKind::Line) {
		return reflect_line(beam, t, surf.seg, h);
	}
	return beam.invert_at(t, *surf.seg.circ, h);
}

template <Num N>
CN<N> projective_axis(const Surf<N> &surf, EffectType etype, const Projective &pr, const CN<N> &h, const CN<N> &g) {
	if (etype == EffectType::LineNormalProjection) {
		return g;
	}
	if (etype == EffectType::CircleNormalProjection) {
		return h.sub(surf.seg.circ->c);
	}
	return CN<N>{h.re.sub(h.re).add(h.re.like(pr.nx)), h.re.sub(h.re).add(h.re.like(pr.ny))};
}

// Projective effect of the side `left` at h: a straight beam from h along
// ±axis. "through" exits to the far side (sign(exit·grad) = sign(dir·grad)),
// "back" returns to the approach side; an axis tangent to the carrier exits
// along +axis (through) / -axis (back).
template <Num N>
Beam<N> project(const Beam<N> &beam, const N &t, const Surf<N> &surf, bool left, const CN<N> *h_exact = nullptr) {
	const CN<N> h = h_exact == nullptr ? beam.at(t) : *h_exact;
	const EffectType etype = surf.effect(left);
	const Projective &pr = surf.proj(left);
	const CN<N> g = geom::grad(surf.seg, h);
	const CN<N> axis = projective_axis(surf, etype, pr, h, g);
	const N ag = CN<N>::dot(axis, g);
	bool flip = false;
	if (!ag.is_zero()) {
		const N dg = CN<N>::dot(beam.tangent_at(t), g);
		flip = ag.is_negative() != dg.is_negative();
	}
	if (pr.back) {
		flip = !flip;
	}
	const CN<N> ex = flip ? axis.neg() : axis;
	CN<N> unit = ex.unit();
	if constexpr (std::is_same_v<N, Dual>) {
		// The shipped float64 walk (FastSolver.project) differentiates the exit
		// direction by hand: zero for a line normal and a fixed normal, and for
		// a circle normal (the radius depends on the hit) d(u) = (d(axis) -
		// u (u . d(axis))) / |axis|. Same values, that operation order for the
		// derivative.
		double dux = 0.0;
		double duy = 0.0;
		if (etype == EffectType::CircleNormalProjection) {
			const double n = std::sqrt(ex.re.v * ex.re.v + ex.im.v * ex.im.v);
			const double sg = flip ? -1.0 : 1.0;
			const double dax = sg * h.re.d;
			const double day = sg * h.im.d;
			const double proj = unit.re.v * dax + unit.im.v * day;
			dux = (dax - unit.re.v * proj) / n;
			duy = (day - unit.im.v * proj) / n;
		}
		unit = CN<Dual>{Dual{unit.re.v, dux}, Dual{unit.im.v, duy}};
	}
	return Beam<N>{h, unit, beam.kappa.sub(beam.kappa)};
}

// The exit direction at h of a projective side entered from `left`.
template <Num N>
CN<N> projective_exit(const Surf<N> &surf, bool left, const CN<N> &h) {
	const EffectType etype = surf.effect(left);
	const Projective &pr = surf.proj(left);
	const CN<N> g = geom::grad(surf.seg, h);
	const CN<N> axis = projective_axis(surf, etype, pr, h, g);
	const N ag = CN<N>::dot(axis, g);
	bool flip = false;
	if (!ag.is_zero()) {
		// sign(dir . grad): approach = -dir . grad > 0 iff LEFT == (winding >= 0)
		const bool wind_nonneg = !geom::winding(surf.seg).is_negative();
		const bool dg_negative = left == wind_nonneg;
		flip = ag.is_negative() != dg_negative;
	}
	if (pr.back) {
		flip = !flip;
	}
	return flip ? axis.neg() : axis;
}

template <Num N>
struct Candidate {
	CN<N> h;
	bool direct; // the exit ray from h reaches X directly
};

// The candidate hit points H on a projective side from which the projected
// beam passes through X (GAME_SPEC 13.3): line normal -> the foot of X; circle
// normal -> c ± R u; directional -> the carrier crossings of the line through
// X along the normal. x_on_carrier: X is itself a candidate and is excluded.
template <Num N>
std::vector<Candidate<N>> back_propagate_candidates(const Surf<N> &surf, bool left, const CN<N> &X, bool x_on_carrier = false) {
	const EffectType etype = surf.effect(left);
	const Projective &pr = surf.proj(left);
	const Seg<N> &seg = surf.seg;
	const N zero = X.re.sub(X.re);
	std::vector<CN<N>> cands;
	if (etype == EffectType::LineNormalProjection) {
		const CN<N> D = seg.e.sub(seg.s);
		cands = {seg.s.add(D.scale(CN<N>::dot(X.sub(seg.s), D).div(D.abs2())))};
	} else if (etype == EffectType::CircleNormalProjection) {
		const CN<N> u = X.sub(seg.circ->c);
		if (u.is_zero()) {
			return {};
		}
		cands = {seg.circ->c.add(u.unit().scale(seg.circ->r)), seg.circ->c.sub(u.unit().scale(seg.circ->r))};
	} else {
		const CN<N> n{zero.add(X.re.like(pr.nx)), zero.add(X.re.like(pr.ny))};
		if (seg.kind == SegKind::Line) {
			const CN<N> D = seg.e.sub(seg.s);
			const N den = CN<N>::cross(D, n);
			if (den.is_zero()) {
				return {};
			}
			cands = {X.sub(n.scale(CN<N>::cross(D, X.sub(seg.s)).div(den)))};
		} else {
			// |X - s n - c|^2 = R^2
			const CN<N> dc = X.sub(seg.circ->c);
			const N a = CN<N>::dot(n, n);
			const N b = CN<N>::dot(n, dc).mul_pow2(1).neg();
			const N c0 = CN<N>::dot(dc, dc).sub(seg.circ->r.mul(seg.circ->r));
			const N disc = b.mul(b).sub(a.mul(c0).mul_pow2(2));
			if (disc.is_negative()) {
				return {};
			}
			const N sq = disc.sqrt_val();
			const N two_a = a.mul_pow2(1);
			cands = {X.sub(n.scale(b.neg().add(sq).div(two_a))), X.sub(n.scale(b.neg().sub(sq).div(two_a)))};
		}
	}
	if (x_on_carrier && !cands.empty()) {
		// provenance: X is itself a candidate -- that one is X exactly, in every
		// tier (Stage 114: the fork rule chooses between X and the other; before,
		// X was excluded)
		size_t i = 0;
		if (cands.size() == 2 && X.sub(cands[1]).abs2().to_float() < X.sub(cands[0]).abs2().to_float()) {
			i = 1;
		}
		cands[i] = X;
	}
	std::vector<Candidate<N>> out;
	for (const CN<N> &H : cands) {
		const N d = CN<N>::dot(X.sub(H), projective_exit(surf, left, H));
		out.push_back({H, !d.is_negative() && !d.is_zero()});
	}
	return out;
}

// The player's click as the game stores it (Vector2: float32 components).
struct F32Point {
	float x;
	float y;
};

// The fork rule (GAME_SPEC 13.3, Stage 114): of the candidates, those on the
// segment are preferred; among what remains, the one nearest the point the
// player clicked -- by default the segment's midpoint (its via point). With
// none on the segment the click picks an off-segment one (a planned-only
// firing). The segment test is the rounded one on each candidate; a candidate
// snapped to a data point (K11) is tested as that point.
template <Num N>
CN<N> choose_candidate(const Surf<N> &surf, const std::vector<CN<N>> &pool, const F32Point *click = nullptr) {
	std::vector<CN<N>> on;
	for (const CN<N> &h : pool) {
		if (geom::on_segment(surf.seg, h)) {
			on.push_back(h);
		}
	}
	const std::vector<CN<N>> &p = on.empty() ? pool : on;
	if (p.size() == 1) {
		return p[0];
	}
	// F32-SEMANTICS: Vector2(H.fx(), H.fy()).distance_squared_to(point) in float32 (B7)
	const float px = click != nullptr ? click->x : float(surf.vx);
	const float py = click != nullptr ? click->y : float(surf.vy);
	const CN<N> *best = &p[0];
	float best_d = std::numeric_limits<float>::infinity();
	for (const CN<N> &h : p) {
		const float hx = float(h.fx());
		const float hy = float(h.fy());
		const float ddx = hx - px;
		const float ddy = hy - py;
		const float d = ddx * ddx + ddy * ddy;
		if (d < best_d) {
			best_d = d;
			best = &h;
		}
	}
	return *best;
}

// The hit point by the fork rule (choose_candidate); nullopt without a candidate.
template <Num N>
std::optional<CN<N>> back_propagate(const Surf<N> &surf, bool left, const CN<N> &X, bool x_on_carrier = false,
		const F32Point *click = nullptr) {
	const auto c = back_propagate_candidates(surf, left, X, x_on_carrier);
	if (c.empty()) {
		return std::nullopt;
	}
	std::vector<CN<N>> pool;
	for (const Candidate<N> &k : c) {
		pool.push_back(k.h);
	}
	return choose_candidate(surf, pool, click);
}

// T(z) of a portal surface (inverse: T^-1(z) = conj(a)(z - d)).
template <Num N>
CN<N> portal_point(const CN<N> &z, const Surf<N> &surf, bool inverse = false) {
	if (inverse) {
		return surf.alpha.conj().mul(z.sub(surf.dvec));
	}
	return surf.alpha.mul(z).add(surf.dvec);
}

// The surface's transform (or its inverse) applied to a point (nullopt = infinity).
template <Num N>
std::optional<CN<N>> map_point(const std::optional<CN<N>> &z, const Surf<N> &surf, bool inverse = false) {
	if (surf.portal) {
		return z ? std::optional<CN<N>>(portal_point(*z, surf, inverse)) : std::nullopt;
	}
	const Seg<N> &seg = surf.seg;
	if (seg.kind == SegKind::Line) {
		if (!z) {
			return std::nullopt;
		}
		return seg.s.add(u2(seg).mul(z->sub(seg.s).conj()));
	}
	if (!z) {
		return seg.circ->c;
	}
	if (z->sub(seg.circ->c).is_zero()) {
		return std::nullopt;
	}
	return Beam<N>::invert_point(*z, *seg.circ);
}

// Q0: the scene's pre-transform applied to infinity (nullopt = infinity).
template <Num N>
std::optional<CN<N>> scene_pre_q(const Scene<N> &sc) {
	std::optional<CN<N>> q;
	for (const auto &s : sc.pre_surfaces) {
		q = map_point(q, *s, false);
	}
	return q;
}

// Pole chain state: P, Q (nullopt = infinity), delta = P - Q (tracked exactly).
template <Num N>
struct Poles {
	std::optional<CN<N>> P;
	std::optional<CN<N>> Q;
	std::optional<CN<N>> delta;
};

template <Num N>
Poles<N> map_pole(const Poles<N> &parent, const Surf<N> &surf) {
	const CN<N> &P0 = *parent.P;
	const Seg<N> &seg = surf.seg;
	if (surf.portal) {
		// rigid: infinity is fixed, differences rotate
		const CN<N> Pp = portal_point(P0, surf);
		if (!parent.Q) {
			return {Pp, std::nullopt, std::nullopt};
		}
		const CN<N> Qp = portal_point(*parent.Q, surf);
		const CN<N> dp = surf.alpha.mul(*parent.delta);
		return {Qp.add(dp), Qp, dp};
	}
	if (seg.kind == SegKind::Line) {
		const CN<N> P1 = *map_point(std::optional<CN<N>>(P0), surf);
		if (!parent.Q) {
			return {P1, std::nullopt, std::nullopt};
		}
		const CN<N> d1 = u2(seg).mul(parent.delta->conj());
		const CN<N> Q1 = *map_point(parent.Q, surf);
		return {Q1.add(d1), Q1, d1};
	}
	const Circle<N> &circ = *seg.circ;
	if (P0.sub(circ.c).is_zero()) {
		return {std::nullopt, std::nullopt, std::nullopt}; // P at infinity (Stage 83)
	}
	const CN<N> P1c = Beam<N>::invert_point(P0, circ);
	if (!parent.Q) {
		const CN<N> u = P0.sub(circ.c);
		return {P1c, circ.c, u.scale(circ.r2).div_real(u.abs2())};
	}
	const CN<N> uq = parent.Q->sub(circ.c);
	if (uq.is_zero()) {
		return {P1c, std::nullopt, std::nullopt};
	}
	const CN<N> Q1c = Beam<N>::invert_point(*parent.Q, circ);
	// delta' = -R^2 conj(delta / ((P0 - c)(Q0 - c)))
	const CN<N> d1c = parent.delta->div(P0.sub(circ.c).mul(uq)).conj().scale(circ.r2).neg();
	return {Q1c.add(d1c), Q1c, d1c};
}

} // namespace effects
} // namespace rc
