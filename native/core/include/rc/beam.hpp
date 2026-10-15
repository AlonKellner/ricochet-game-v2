// Oriented generalized circle in visual space, precision-generic (Stage 92):
// the port of scripts/math/precision/hp_beam.gd, every operation in the same
// order (the F64 instantiation is bit-identical to GDScript HPBeam over F64Num
// and to FastSolver's raw math; Dual carries the tangent-linear derivative).
//   point p, unit tangent tau, signed curvature kappa (w.r.t. normal i*tau).
//   z(t) = p + t*tau / (1 - i*kappa*t/2); t = inf is the antipode of p.
#pragma once

#include "rc/cn.hpp"

#include <cmath>

#include <optional>
#include <type_traits>
#include <vector>

namespace rc {

template <Num N>
struct Circle {
	int id = 0;
	CN<N> c;
	N r;
	N r2;

	static Circle make(int p_id, const CN<N> &p_c, const N &p_r) { return Circle{p_id, p_c, p_r, p_r.mul(p_r)}; }
};

template <Num N>
struct Beam {
	CN<N> p;
	CN<N> tau;
	N kappa;

	static Beam make(const CN<N> &p_p, const CN<N> &p_tau, const N &p_kappa) { return Beam{p_p, p_tau, p_kappa}; }

	// 1 + i*kappa*t/2
	CN<N> f_of(const N &t) const { return CN<N>{t.one_like(), kappa.mul(t).mul_pow2(-1)}; }

	CN<N> at(const N &t) const {
		if (t.is_inf()) {
			return *at_infinity();
		}
		// t*tau / (1 - i k t/2) = t*tau*(1 + i k t/2) / (1 + k^2 t^2 / 4)
		const CN<N> f = f_of(t);
		const N den = f.abs2();
		return p.add(tau.mul(f).scale(t).div_real(den));
	}

	// Point at t = infinity (antipode of p); nullopt for a line.
	std::optional<CN<N>> at_infinity() const {
		if (kappa.is_zero()) {
			return std::nullopt;
		}
		// p + 2 i tau / kappa
		CN<N> z = p.add(tau.mul_i().scale(kappa.one_like().mul_pow2(1)).div_real(kappa));
		if constexpr (std::is_same_v<N, Dual>) {
			// the shipped float64 walk (FastSolver.antipode) differentiates by
			// hand: same values, that operation order for the derivative
			const double k = kappa.v;
			const double dk = kappa.d;
			z.re.d = p.re.d + (-2.0) * (tau.im.d * k - tau.im.v * dk) / (k * k);
			z.im.d = p.im.d + 2.0 * (tau.re.d * k - tau.re.v * dk) / (k * k);
		}
		return z;
	}

	CN<N> tangent_at(const N &t) const {
		if (t.is_inf()) {
			return tau.neg();
		}
		const CN<N> f = f_of(t);
		return tau.mul(f).mul(f).unit();
	}

	// t = Re(w / den), w = z - p, den = tau + i kappa w / 2; near the antipode
	// of a circle beam the stable form t = 1 / Re(den / w).
	N param_of(const CN<N> &z) const {
		const CN<N> w = z.sub(p);
		const CN<N> den = tau.add(w.mul_i().scale(kappa).scale(kappa.one_like().mul_pow2(-1)));
		if (den.is_zero()) {
			return kappa.inf_like(); // z is the antipode
		}
		if (!kappa.is_zero() && w.abs2().cmp(den.abs2()) > 0) {
			const N qre = den.div(w).re;
			if (qre.is_zero()) {
				return kappa.inf_like();
			}
			return kappa.one_like().div(qre);
		}
		return w.div(den).re;
	}

	// Real roots of beam ∩ circle. depart_on: p lies exactly on the circle
	// (provenance) and the t = 0 root is dropped.
	std::vector<N> hits(const Circle<N> &circ, bool depart_on) const {
		const CN<N> q = circ.c.sub(p);
		const N tq = CN<N>::dot(q, tau);
		const N &k = kappa;
		const N one = k.one_like();
		std::vector<N> roots;
		if (depart_on) {
			const N a2_d = one.sub(k.mul(CN<N>::cross(tau, q)));
			if (!a2_d.is_zero()) {
				roots.push_back(tq.mul_pow2(1).div(a2_d));
			} else if (!k.is_zero()) {
				roots.push_back(k.inf_like()); // the other crossing is the antipode
			}
			return roots;
		}
		const N P = q.abs2().sub(circ.r2);
		const N a2 = one.sub(k.mul(CN<N>::cross(tau, q))).add(k.mul(k).mul(P).mul_pow2(-2));
		const N disc = tq.mul(tq).sub(a2.mul(P));
		if (disc.is_negative()) {
			return roots;
		}
		const N s = disc.sqrt_val();
		const N big = !tq.is_negative() ? tq.add(s) : tq.sub(s);
		if (!big.is_zero()) {
			roots.push_back(P.div(big));
			if (!a2.is_zero()) {
				roots.push_back(big.div(a2));
			} else if (!k.is_zero()) {
				roots.push_back(k.inf_like());
			}
		} else if (!a2.is_zero()) {
			roots.push_back(k.sub(k));
		}
		return roots;
	}

	// h_exact: the hit point when known exactly by provenance (else at(t)).
	Beam invert_at(const N &t, const Circle<N> &circ, const CN<N> *h_exact = nullptr) const {
		const CN<N> h = h_exact == nullptr ? at(t) : *h_exact;
		const CN<N> tau_h = tangent_at(t);
		const CN<N> u = h.sub(circ.c);
		const CN<N> nu = u.div_real(circ.r);
		const CN<N> tau2 = tau_h.conj().mul(nu).mul(nu).neg().unit();
		const N k2 = kappa.neg().sub(CN<N>::cross(tau_h, u).mul_pow2(1).div(circ.r2));
		return Beam{h, tau2, k2};
	}

	// Oriented circle through P -> X -> Q (Q = P - delta), anchored at P;
	// delta == nullptr: Q at infinity (a straight beam toward X).
	static Beam through(const CN<N> &P, const CN<N> &X, const CN<N> *delta) {
		const CN<N> a = X.sub(P);
		if (delta == nullptr) {
			return Beam{P, a.unit(), a.re.sub(a.re)};
		}
		const CN<N> b = delta->neg();
		if (b.sub(a).is_zero() || b.is_zero()) {
			// X = Q or P = Q (the aim point or the player at the frame's infinity, Stage 101): the line through them
			return Beam{P, a.unit(), a.re.sub(a.re)};
		}
		// g = |a|^2 J(b) - |b|^2 J(a), J = rotate by -90 deg; tau = -i * unit(g)
		const CN<N> g = b.mul_neg_i().scale(a.abs2()).sub(a.mul_neg_i().scale(b.abs2()));
		const CN<N> tau_p = g.mul_neg_i().unit();
		const CN<N> ba = b.sub(a);
		const N kap = CN<N>::cross(a, b).mul_pow2(1).div(a.abs_val().mul(b.abs_val()).mul(ba.abs_val()));
		return Beam{P, tau_p, kap};
	}

	// The beam from P along the unit tangent tau whose circle passes Q (Stage
	// 101, a pre-transform frame): kappa = 2 cross(tau, Q - P) / |Q - P|^2;
	// the player at Q: a straight beam (Q at its start).
	static Beam through_tangent(const CN<N> &P, const CN<N> &tau, const CN<N> &Q) {
		const CN<N> w = Q.sub(P);
		if (w.is_zero()) {
			return Beam{P, tau, w.re};
		}
		return Beam{P, tau, CN<N>::cross(tau, w).mul_pow2(1).div(w.abs2())};
	}

	static CN<N> invert_point(const CN<N> &z, const Circle<N> &circ) {
		const CN<N> u = z.sub(circ.c);
		// c + R^2 / conj(u) = c + R^2 * u / |u|^2
		return circ.c.add(u.scale(circ.r2).div_real(u.abs2()));
	}
};

} // namespace rc

namespace rc {
namespace fast {

// Point at t for display and margins only: no overflow for huge |t| (the
// parametrization is evaluated as tau / (1/t - i kappa/2) there). Port of
// FastSolver.at_view on the float64 values of a beam.
inline void at_view(double px, double py, double tx, double ty, double k, double t, double &ox, double &oy) {
	if (std::isinf(t)) {
		// antipode: p + 2 i tau / kappa
		ox = px + ((-ty) * 2.0) / k;
		oy = py + (tx * 2.0) / k;
		return;
	}
	const double u = k * t * 0.5;
	if (std::fabs(u) <= 1.0) {
		// FastSolver.at(b, t, 0.0) on the values
		const double kt = k * t;
		const double uu = kt * 0.5;
		const double re1 = tx * 1.0 - ty * uu;
		const double im1 = tx * uu + ty * 1.0;
		const double re2 = re1 * t;
		const double im2 = im1 * t;
		const double den = 1.0 * 1.0 + uu * uu;
		ox = px + re2 / den;
		oy = py + im2 / den;
		return;
	}
	const double wr = 1.0 / t;
	const double wi = -k * 0.5;
	const double d = wr * wr + wi * wi;
	// tau / (wr + i wi) = tau * (wr - i wi) / d
	ox = px + (tx * wr + ty * wi) / d;
	oy = py + (ty * wr - tx * wi) / d;
}

} // namespace fast
} // namespace rc
