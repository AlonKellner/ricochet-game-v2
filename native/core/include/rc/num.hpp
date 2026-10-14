// The scalar tiers of the precision-generic kernels (Stage 90): F64 (plain
// float64), Dual (forward-mode dual number, the tangent-linear sensitivity)
// and BigFloat. Same operation names as scripts/math/precision/{f64_num,
// dual_num,big_float}.gd; the float64 operations are written exactly as the
// GDScript ones so results are bit-identical (no FMA: see cmake/fp.cmake).
#pragma once

#include "rc/bigfloat.hpp"

#include <cmath>
#include <concepts>
#include <limits>

namespace rc {

// Godot's maxf / minf: (a > b) ? a : b and (a < b) ? a : b. Unlike std::max /
// std::min they return the second argument when either one is NaN; the
// ladder's gain envelopes and error estimates inherit that (GDScript
// ErrorEstimate / PrecisionLadder), so every max / min of a float the
// GDScript wrote as maxf / minf goes through these.
inline double gd_maxf(double a, double b) { return (a > b) ? a : b; }
inline double gd_minf(double a, double b) { return (a < b) ? a : b; }

struct F64 {
	double v = 0.0;

	static F64 of(double x) { return F64{x}; }
	F64 one_like() const { return F64{1.0}; }
	F64 inf_like() const { return F64{std::numeric_limits<double>::infinity()}; }
	F64 like(double f) const { return F64{f}; }
	bool is_inf() const { return v == std::numeric_limits<double>::infinity(); }
	bool is_zero() const { return v == 0.0; }
	bool is_negative() const { return v < 0.0; }
	double to_float() const { return v; }
	F64 neg() const { return F64{-v}; }
	F64 abs_val() const { return F64{std::fabs(v)}; }
	F64 add(F64 o) const { return F64{v + o.v}; }
	F64 sub(F64 o) const { return F64{v - o.v}; }
	F64 mul(F64 o) const { return F64{v * o.v}; }
	F64 div(F64 o) const { return F64{v / o.v}; }
	F64 sqrt_val() const { return F64{std::sqrt(v)}; }
	F64 mul_pow2(int64_t k) const { return F64{v * std::pow(2.0, double(k))}; }
	int cmp(F64 o) const {
		if (v == o.v) {
			return 0;
		}
		return v > o.v ? 1 : -1;
	}
};

struct Dual {
	double v = 0.0;
	double d = 0.0;

	static Dual of(double x, double dx = 0.0) { return Dual{x, dx}; }
	Dual one_like() const { return Dual{1.0, 0.0}; }
	Dual inf_like() const { return Dual{std::numeric_limits<double>::infinity(), 0.0}; }
	Dual like(double f) const { return Dual{f, 0.0}; }
	bool is_inf() const { return v == std::numeric_limits<double>::infinity(); }
	bool is_zero() const { return v == 0.0; }
	bool is_negative() const { return v < 0.0; }
	double to_float() const { return v; }
	Dual neg() const { return Dual{-v, -d}; }
	Dual abs_val() const { return v < 0.0 ? Dual{-v, -d} : *this; }
	Dual add(Dual o) const { return Dual{v + o.v, d + o.d}; }
	Dual sub(Dual o) const { return Dual{v - o.v, d - o.d}; }
	Dual mul(Dual o) const { return Dual{v * o.v, d * o.v + v * o.d}; }
	Dual div(Dual o) const {
		const double q = v / o.v;
		return Dual{q, (d - q * o.d) / o.v};
	}
	Dual sqrt_val() const {
		const double s = std::sqrt(v);
		return Dual{s, s != 0.0 ? d / (2.0 * s) : 0.0};
	}
	Dual mul_pow2(int64_t k) const {
		const double f = std::pow(2.0, double(k));
		return Dual{v * f, d * f};
	}
	int cmp(Dual o) const {
		if (v == o.v) {
			return 0;
		}
		return v > o.v ? 1 : -1;
	}
};

// A precision tier's scalar: the operations every kernel is written against.
template <class T>
concept Num = requires(const T a, const T b, int64_t k) {
	{ a.is_inf() } -> std::same_as<bool>;
	{ a.is_zero() } -> std::same_as<bool>;
	{ a.is_negative() } -> std::same_as<bool>;
	{ a.to_float() } -> std::same_as<double>;
	{ a.neg() } -> std::same_as<T>;
	{ a.abs_val() } -> std::same_as<T>;
	{ a.add(b) } -> std::same_as<T>;
	{ a.sub(b) } -> std::same_as<T>;
	{ a.mul(b) } -> std::same_as<T>;
	{ a.div(b) } -> std::same_as<T>;
	{ a.sqrt_val() } -> std::same_as<T>;
	{ a.mul_pow2(k) } -> std::same_as<T>;
	{ a.cmp(b) } -> std::same_as<int>;
	{ a.one_like() } -> std::same_as<T>;
	{ a.inf_like() } -> std::same_as<T>;
	{ a.like(0.0) } -> std::same_as<T>;
};

static_assert(Num<F64>);
static_assert(Num<Dual>);
static_assert(Num<BigFloat>);

} // namespace rc
