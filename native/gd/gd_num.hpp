// Bindings of the scalar tiers (Stage 90): NNumCtx (NumCtx), NNum (the F64Num /
// DualNum / BigFloat object API: one class over the tier variant), NCN (CN),
// NBigFloat (BigFloat's statics and limb access).
#pragma once

#include "gd_common.hpp"

#include <godot_cpp/variant/packed_int64_array.hpp>

class NNum;
class NCN;

class NNumCtx : public godot::RefCounted {
	GDCLASS(NNumCtx, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	int limbs = 0;

	static godot::Ref<NNumCtx> f64();
	static godot::Ref<NNumCtx> with_limbs(int n);
	static godot::Ref<NNumCtx> dual();
	bool is_f64() const { return limbs == 0; }
	bool is_dual() const { return limbs == rcgd::DUAL_LIMBS; }
	int bits() const { return limbs <= 0 ? 53 : limbs * rc::BigFloat::BITS; }
	double eps() const;
	godot::Ref<NNum> num(double f) const;
	godot::Ref<NNum> from_string(const godot::String &s) const;
	godot::Ref<NNum> inf() const;
	godot::Ref<NNum> zero() const;
	godot::Ref<NNum> one() const;
	godot::Ref<NCN> cn(double x, double y) const;
	godot::String name() const;
	int get_limbs() const { return limbs; }
	void set_limbs(int n) { limbs = n; }
};

// A scalar of any tier (F64Num / DualNum / BigFloat API). `v` and `d` expose
// the float64 value and, for the Dual tier, the derivative.
class NNum : public godot::RefCounted {
	GDCLASS(NNum, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rcgd::NumV value = rc::F64{0.0};

	static godot::Ref<NNum> wrap(rcgd::NumV v);
	static godot::Ref<NNum> of(double x);     // F64Num.of
	static godot::Ref<NNum> dual_of(double x, double dx); // DualNum.of

	bool is_inf() const;
	bool is_zero() const;
	bool is_negative() const;
	double to_float() const;
	godot::Ref<NNum> neg() const;
	godot::Ref<NNum> abs_val() const;
	godot::Ref<NNum> add(const godot::Ref<NNum> &o) const;
	godot::Ref<NNum> sub(const godot::Ref<NNum> &o) const;
	godot::Ref<NNum> mul(const godot::Ref<NNum> &o) const;
	godot::Ref<NNum> div(const godot::Ref<NNum> &o) const;
	godot::Ref<NNum> sqrt_val() const;
	godot::Ref<NNum> mul_pow2(int k) const;
	int cmp(const godot::Ref<NNum> &o) const;
	int limbs() const { return rcgd::limbs_of(value); }
	double get_v() const { return to_float(); }
	double get_d() const;
	// BigFloat fields
	int get_sign() const;
	int64_t get_exp() const;
	godot::PackedInt64Array get_limbs() const;
	godot::Ref<NNum> with_limbs(int n) const;
	int precision_limbs() const;
	int64_t top_exp() const;
	godot::String debug_string() const;
};

class NCN : public godot::RefCounted {
	GDCLASS(NCN, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rcgd::CNV value = rc::CN<rc::F64>{rc::F64{0.0}, rc::F64{0.0}};

	static godot::Ref<NCN> wrap(rcgd::CNV v);
	static godot::Ref<NCN> make(const godot::Ref<NNum> &re, const godot::Ref<NNum> &im);
	godot::Ref<NCN> add(const godot::Ref<NCN> &o) const;
	godot::Ref<NCN> sub(const godot::Ref<NCN> &o) const;
	godot::Ref<NCN> neg() const;
	godot::Ref<NCN> conj() const;
	godot::Ref<NCN> mul(const godot::Ref<NCN> &o) const;
	godot::Ref<NCN> scale(const godot::Ref<NNum> &s) const;
	godot::Ref<NCN> mul_i() const;
	godot::Ref<NCN> mul_neg_i() const;
	godot::Ref<NNum> abs2() const;
	godot::Ref<NNum> abs_val() const;
	godot::Ref<NCN> div(const godot::Ref<NCN> &o) const;
	godot::Ref<NCN> div_real(const godot::Ref<NNum> &s) const;
	godot::Ref<NCN> unit() const;
	bool is_zero() const;
	static godot::Ref<NNum> dot(const godot::Ref<NCN> &a, const godot::Ref<NCN> &b);
	static godot::Ref<NNum> cross(const godot::Ref<NCN> &a, const godot::Ref<NCN> &b);
	godot::Vector2 to_vector2() const;
	double fx() const;
	double fy() const;
	godot::Ref<NNum> get_re() const;
	godot::Ref<NNum> get_im() const;
	int limbs() const;
};

// BigFloat's statics (BigFloat.from_float(f, n) etc.); instances are NNum.
class NBigFloat : public godot::RefCounted {
	GDCLASS(NBigFloat, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static constexpr int BITS = rc::BigFloat::BITS;
	static godot::Ref<NNum> zero(int n);
	static godot::Ref<NNum> infinity(int n);
	static int limbs_for_bits(int bits);
	static godot::Ref<NNum> from_float(double f, int n);
	static godot::Ref<NNum> from_string(const godot::String &s, int n);
	static godot::Ref<NNum> from_int(int64_t v, int n);
};
