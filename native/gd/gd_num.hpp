// Bindings of the scalar tiers (Stage 90): NumCtx (NumCtx), Num (the F64Num /
// DualNum / BigFloat object API: one class over the tier variant), CN (CN),
// BigFloat (BigFloat's statics and limb access).
#pragma once

#include "gd_common.hpp"

#include <godot_cpp/variant/packed_int64_array.hpp>

class Num;
class CN;

class NumCtx : public godot::RefCounted {
	GDCLASS(NumCtx, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	int limbs = 0;

	static godot::Ref<NumCtx> f64();
	static godot::Ref<NumCtx> with_limbs(int n);
	static godot::Ref<NumCtx> dual();
	bool is_f64() const { return limbs == 0; }
	bool is_dual() const { return limbs == rcgd::DUAL_LIMBS; }
	int bits() const { return limbs <= 0 ? 53 : limbs * rc::BigFloat::BITS; }
	double eps() const;
	godot::Ref<Num> num(double f) const;
	godot::Ref<Num> from_string(const godot::String &s) const;
	godot::Ref<Num> inf() const;
	godot::Ref<Num> zero() const;
	godot::Ref<Num> one() const;
	godot::Ref<CN> cn(double x, double y) const;
	godot::String name() const;
	int get_limbs() const { return limbs; }
	void set_limbs(int n) { limbs = n; }
};

// A scalar of any tier (F64Num / DualNum / BigFloat API). `v` and `d` expose
// the float64 value and, for the Dual tier, the derivative.
class Num : public godot::RefCounted {
	GDCLASS(Num, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rcgd::NumV value = rc::F64{0.0};

	static godot::Ref<Num> wrap(rcgd::NumV v);
	static godot::Ref<Num> of(double x);     // F64Num.of
	static godot::Ref<Num> dual_of(double x, double dx); // DualNum.of

	bool is_inf() const;
	bool is_zero() const;
	bool is_negative() const;
	double to_float() const;
	godot::Ref<Num> neg() const;
	godot::Ref<Num> abs_val() const;
	godot::Ref<Num> add(const godot::Ref<Num> &o) const;
	godot::Ref<Num> sub(const godot::Ref<Num> &o) const;
	godot::Ref<Num> mul(const godot::Ref<Num> &o) const;
	godot::Ref<Num> div(const godot::Ref<Num> &o) const;
	godot::Ref<Num> sqrt_val() const;
	godot::Ref<Num> mul_pow2(int k) const;
	int cmp(const godot::Ref<Num> &o) const;
	int limbs() const { return rcgd::limbs_of(value); }
	double get_v() const { return to_float(); }
	double get_d() const;
	// BigFloat fields
	int get_sign() const;
	int64_t get_exp() const;
	godot::PackedInt64Array get_limbs() const;
	godot::Ref<Num> with_limbs(int n) const;
	int precision_limbs() const;
	int64_t top_exp() const;
	godot::String debug_string() const;
};

class CN : public godot::RefCounted {
	GDCLASS(CN, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rcgd::CNV value = rc::CN<rc::F64>{rc::F64{0.0}, rc::F64{0.0}};

	static godot::Ref<CN> wrap(rcgd::CNV v);
	static godot::Ref<CN> make(const godot::Ref<Num> &re, const godot::Ref<Num> &im);
	godot::Ref<CN> add(const godot::Ref<CN> &o) const;
	godot::Ref<CN> sub(const godot::Ref<CN> &o) const;
	godot::Ref<CN> neg() const;
	godot::Ref<CN> conj() const;
	godot::Ref<CN> mul(const godot::Ref<CN> &o) const;
	godot::Ref<CN> scale(const godot::Ref<Num> &s) const;
	godot::Ref<CN> mul_i() const;
	godot::Ref<CN> mul_neg_i() const;
	godot::Ref<Num> abs2() const;
	godot::Ref<Num> abs_val() const;
	godot::Ref<CN> div(const godot::Ref<CN> &o) const;
	godot::Ref<CN> div_real(const godot::Ref<Num> &s) const;
	godot::Ref<CN> unit() const;
	bool is_zero() const;
	static godot::Ref<Num> dot(const godot::Ref<CN> &a, const godot::Ref<CN> &b);
	static godot::Ref<Num> cross(const godot::Ref<CN> &a, const godot::Ref<CN> &b);
	godot::Vector2 to_vector2() const;
	double fx() const;
	double fy() const;
	godot::Ref<Num> get_re() const;
	godot::Ref<Num> get_im() const;
	int limbs() const;
};

// BigFloat's statics (BigFloat.from_float(f, n) etc.); instances are Num.
class BigFloat : public godot::RefCounted {
	GDCLASS(BigFloat, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static constexpr int BITS = rc::BigFloat::BITS;
	static godot::Ref<Num> zero(int n);
	static godot::Ref<Num> infinity(int n);
	static int limbs_for_bits(int bits);
	static godot::Ref<Num> from_float(double f, int n);
	static godot::Ref<Num> from_string(const godot::String &s, int n);
	static godot::Ref<Num> from_int(int64_t v, int n);
};

// GDScript's F64Num.of(x) and DualNum.of(x, dx): instances are Num.
class F64Num : public godot::RefCounted {
	GDCLASS(F64Num, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static godot::Ref<Num> of(double x) { return Num::of(x); }
};

class DualNum : public godot::RefCounted {
	GDCLASS(DualNum, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static godot::Ref<Num> of(double x, double dx = 0.0) { return Num::dual_of(x, dx); }
};
