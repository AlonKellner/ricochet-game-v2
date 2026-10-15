// Complex number over a precision-generic scalar (Stage 90): the port of
// scripts/math/precision/cn.gd, with every operation in the same order.
#pragma once

#include "rc/num.hpp"

namespace rc {

template <Num N>
struct CN {
	N re;
	N im;

	static CN make(N p_re, N p_im) { return CN{p_re, p_im}; }

	CN add(const CN &o) const { return CN{re.add(o.re), im.add(o.im)}; }
	CN sub(const CN &o) const { return CN{re.sub(o.re), im.sub(o.im)}; }
	CN neg() const { return CN{re.neg(), im.neg()}; }
	CN conj() const { return CN{re, im.neg()}; }
	CN mul(const CN &o) const {
		return CN{re.mul(o.re).sub(im.mul(o.im)), re.mul(o.im).add(im.mul(o.re))};
	}
	// multiply by a real scalar
	CN scale(const N &s) const { return CN{re.mul(s), im.mul(s)}; }
	// i * z
	CN mul_i() const { return CN{im.neg(), re}; }
	// -i * z
	CN mul_neg_i() const { return CN{im, re.neg()}; }
	N abs2() const { return re.mul(re).add(im.mul(im)); }
	N abs_val() const { return abs2().sqrt_val(); }
	CN div(const CN &o) const {
		const N d = o.abs2();
		const CN n = mul(o.conj());
		return CN{n.re.div(d), n.im.div(d)};
	}
	CN div_real(const N &s) const { return CN{re.div(s), im.div(s)}; }
	CN unit() const { return div_real(abs_val()); }
	bool is_zero() const { return re.is_zero() && im.is_zero(); }
	static N dot(const CN &a, const CN &b) { return a.re.mul(b.re).add(a.im.mul(b.im)); }
	// a.x * b.y - a.y * b.x
	static N cross(const CN &a, const CN &b) { return a.re.mul(b.im).sub(a.im.mul(b.re)); }
	double fx() const { return re.to_float(); }
	double fy() const { return im.to_float(); }
};

} // namespace rc
