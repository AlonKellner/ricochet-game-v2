#include "gd_num.hpp"

#include <godot_cpp/core/class_db.hpp>

using godot::ClassDB;
using godot::D_METHOD;
using godot::PropertyInfo;
using godot::Ref;
using godot::Variant;
using namespace rcgd;

// ---------------------------------------------------------------- NumCtx

void NumCtx::_bind_methods() {
	ClassDB::bind_static_method("NumCtx", D_METHOD("f64"), &NumCtx::f64);
	ClassDB::bind_static_method("NumCtx", D_METHOD("with_limbs", "n"), &NumCtx::with_limbs);
	ClassDB::bind_static_method("NumCtx", D_METHOD("dual"), &NumCtx::dual);
	ClassDB::bind_method(D_METHOD("is_f64"), &NumCtx::is_f64);
	ClassDB::bind_method(D_METHOD("is_dual"), &NumCtx::is_dual);
	ClassDB::bind_method(D_METHOD("bits"), &NumCtx::bits);
	ClassDB::bind_method(D_METHOD("eps"), &NumCtx::eps);
	ClassDB::bind_method(D_METHOD("num", "f"), &NumCtx::num);
	ClassDB::bind_method(D_METHOD("from_string", "s"), &NumCtx::from_string);
	ClassDB::bind_method(D_METHOD("inf"), &NumCtx::inf);
	ClassDB::bind_method(D_METHOD("zero"), &NumCtx::zero);
	ClassDB::bind_method(D_METHOD("one"), &NumCtx::one);
	ClassDB::bind_method(D_METHOD("cn", "x", "y"), &NumCtx::cn);
	ClassDB::bind_method(D_METHOD("name"), &NumCtx::name);
	ClassDB::bind_method(D_METHOD("get_limbs"), &NumCtx::get_limbs);
	ClassDB::bind_method(D_METHOD("set_limbs", "n"), &NumCtx::set_limbs);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "limbs"), "set_limbs", "get_limbs");
	BIND_CONSTANT(DUAL_LIMBS);
}

Ref<NumCtx> NumCtx::f64() {
	Ref<NumCtx> c;
	c.instantiate();
	c->limbs = 0;
	return c;
}
Ref<NumCtx> NumCtx::with_limbs(int n) {
	Ref<NumCtx> c;
	c.instantiate();
	c->limbs = n;
	return c;
}
Ref<NumCtx> NumCtx::dual() {
	Ref<NumCtx> c;
	c.instantiate();
	c->limbs = DUAL_LIMBS;
	return c;
}
double NumCtx::eps() const {
	return std::pow(2.0, -bits());
}
Ref<Num> NumCtx::num(double f) const {
	return Num::wrap(num_of(limbs, f));
}
Ref<Num> NumCtx::from_string(const godot::String &s) const {
	if (limbs <= 0) {
		return num(s.to_float());
	}
	return Num::wrap(rc::BigFloat::from_string(to_std(s), limbs));
}
Ref<Num> NumCtx::inf() const {
	if (limbs <= 0) {
		return num(std::numeric_limits<double>::infinity());
	}
	return Num::wrap(rc::BigFloat::infinity(limbs));
}
Ref<Num> NumCtx::zero() const {
	return num(0.0);
}
Ref<Num> NumCtx::one() const {
	return num(1.0);
}
Ref<CN> NumCtx::cn(double x, double y) const {
	if (limbs == 0) {
		return CN::wrap(rc::CN<rc::F64>{rc::F64{x}, rc::F64{y}});
	}
	if (limbs == DUAL_LIMBS) {
		return CN::wrap(rc::CN<rc::Dual>{rc::Dual{x, 0.0}, rc::Dual{y, 0.0}});
	}
	return CN::wrap(rc::CN<rc::BigFloat>{rc::BigFloat::from_float(x, limbs), rc::BigFloat::from_float(y, limbs)});
}
godot::String NumCtx::name() const {
	if (limbs == DUAL_LIMBS) {
		return "dual";
	}
	return limbs == 0 ? godot::String("f64") : to_gd("big" + std::to_string(bits()));
}

// ---------------------------------------------------------------- Num

void Num::_bind_methods() {
	ClassDB::bind_static_method("Num", D_METHOD("of", "x"), &Num::of);
	ClassDB::bind_static_method("Num", D_METHOD("dual_of", "x", "dx"), &Num::dual_of);
	ClassDB::bind_method(D_METHOD("is_inf"), &Num::is_inf);
	ClassDB::bind_method(D_METHOD("is_zero"), &Num::is_zero);
	ClassDB::bind_method(D_METHOD("is_negative"), &Num::is_negative);
	ClassDB::bind_method(D_METHOD("to_float"), &Num::to_float);
	ClassDB::bind_method(D_METHOD("neg"), &Num::neg);
	ClassDB::bind_method(D_METHOD("abs_val"), &Num::abs_val);
	ClassDB::bind_method(D_METHOD("add", "o"), &Num::add);
	ClassDB::bind_method(D_METHOD("sub", "o"), &Num::sub);
	ClassDB::bind_method(D_METHOD("mul", "o"), &Num::mul);
	ClassDB::bind_method(D_METHOD("div", "o"), &Num::div);
	ClassDB::bind_method(D_METHOD("sqrt_val"), &Num::sqrt_val);
	ClassDB::bind_method(D_METHOD("mul_pow2", "k"), &Num::mul_pow2);
	ClassDB::bind_method(D_METHOD("cmp", "o"), &Num::cmp);
	ClassDB::bind_method(D_METHOD("limbs"), &Num::limbs);
	ClassDB::bind_method(D_METHOD("get_v"), &Num::get_v);
	ClassDB::bind_method(D_METHOD("get_d"), &Num::get_d);
	ClassDB::bind_method(D_METHOD("get_sign"), &Num::get_sign);
	ClassDB::bind_method(D_METHOD("get_exp"), &Num::get_exp);
	ClassDB::bind_method(D_METHOD("get_limbs"), &Num::get_limbs);
	ClassDB::bind_method(D_METHOD("with_limbs", "n"), &Num::with_limbs);
	ClassDB::bind_method(D_METHOD("precision_limbs"), &Num::precision_limbs);
	ClassDB::bind_method(D_METHOD("top_exp"), &Num::top_exp);
	ClassDB::bind_method(D_METHOD("debug_string"), &Num::debug_string);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "v"), "", "get_v");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "d"), "", "get_d");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sign"), "", "get_sign");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "exp"), "", "get_exp");
}

Ref<Num> Num::wrap(NumV v) {
	Ref<Num> n;
	n.instantiate();
	n->value = std::move(v);
	return n;
}
Ref<Num> Num::of(double x) {
	return wrap(rc::F64{x});
}
Ref<Num> Num::dual_of(double x, double dx) {
	return wrap(rc::Dual{x, dx});
}
bool Num::is_inf() const {
	return std::visit([](const auto &x) { return x.is_inf(); }, value);
}
bool Num::is_zero() const {
	return std::visit([](const auto &x) { return x.is_zero(); }, value);
}
bool Num::is_negative() const {
	return std::visit([](const auto &x) { return x.is_negative(); }, value);
}
double Num::to_float() const {
	return rcgd::to_float(value);
}
Ref<Num> Num::neg() const {
	return std::visit([](const auto &x) { return wrap(x.neg()); }, value);
}
Ref<Num> Num::abs_val() const {
	return std::visit([](const auto &x) { return wrap(x.abs_val()); }, value);
}
namespace {
template <class F>
Ref<Num> binop(const NumV &a, const Ref<Num> &o, F &&f) {
	return std::visit([&](const auto &x) -> Ref<Num> {
		using T = std::decay_t<decltype(x)>;
		const T *y = std::get_if<T>(&o->value);
		ERR_FAIL_NULL_V_MSG(y, Ref<Num>(), "Num: operands of different tiers");
		return Num::wrap(f(x, *y));
	}, a);
}
} // namespace
Ref<Num> Num::add(const Ref<Num> &o) const {
	return binop(value, o, [](const auto &x, const auto &y) { return x.add(y); });
}
Ref<Num> Num::sub(const Ref<Num> &o) const {
	return binop(value, o, [](const auto &x, const auto &y) { return x.sub(y); });
}
Ref<Num> Num::mul(const Ref<Num> &o) const {
	return binop(value, o, [](const auto &x, const auto &y) { return x.mul(y); });
}
Ref<Num> Num::div(const Ref<Num> &o) const {
	return binop(value, o, [](const auto &x, const auto &y) { return x.div(y); });
}
Ref<Num> Num::sqrt_val() const {
	return std::visit([](const auto &x) { return wrap(x.sqrt_val()); }, value);
}
Ref<Num> Num::mul_pow2(int k) const {
	return std::visit([k](const auto &x) { return wrap(x.mul_pow2(k)); }, value);
}
int Num::cmp(const Ref<Num> &o) const {
	return std::visit([&](const auto &x) -> int {
		using T = std::decay_t<decltype(x)>;
		const T *y = std::get_if<T>(&o->value);
		ERR_FAIL_NULL_V_MSG(y, 0, "Num: operands of different tiers");
		return x.cmp(*y);
	}, value);
}
double Num::get_d() const {
	if (const rc::Dual *d = std::get_if<rc::Dual>(&value)) {
		return d->d;
	}
	return 0.0;
}
int Num::get_sign() const {
	if (const rc::BigFloat *b = std::get_if<rc::BigFloat>(&value)) {
		return b->sign;
	}
	const double v = to_float();
	return v == 0.0 ? 0 : (v > 0.0 ? 1 : -1);
}
int64_t Num::get_exp() const {
	if (const rc::BigFloat *b = std::get_if<rc::BigFloat>(&value)) {
		return b->exp;
	}
	return 0;
}
godot::PackedInt64Array Num::get_limbs() const {
	godot::PackedInt64Array out;
	if (const rc::BigFloat *b = std::get_if<rc::BigFloat>(&value)) {
		out.resize(int64_t(b->limbs.size()));
		for (size_t i = 0; i < b->limbs.size(); i++) {
			out[int64_t(i)] = b->limbs[i];
		}
	}
	return out;
}
Ref<Num> Num::with_limbs(int n) const {
	if (const rc::BigFloat *b = std::get_if<rc::BigFloat>(&value)) {
		return wrap(b->with_limbs(n));
	}
	return wrap(value);
}
int Num::precision_limbs() const {
	if (const rc::BigFloat *b = std::get_if<rc::BigFloat>(&value)) {
		return b->precision_limbs();
	}
	return 0;
}
int64_t Num::top_exp() const {
	if (const rc::BigFloat *b = std::get_if<rc::BigFloat>(&value)) {
		return b->top_exp();
	}
	return 0;
}
godot::String Num::debug_string() const {
	if (const rc::BigFloat *b = std::get_if<rc::BigFloat>(&value)) {
		return to_gd(b->debug_string());
	}
	return godot::String::num(to_float(), 17);
}

// ---------------------------------------------------------------- CN

void CN::_bind_methods() {
	ClassDB::bind_static_method("CN", D_METHOD("make", "re", "im"), &CN::make);
	ClassDB::bind_static_method("CN", D_METHOD("dot", "a", "b"), &CN::dot);
	ClassDB::bind_static_method("CN", D_METHOD("cross", "a", "b"), &CN::cross);
	ClassDB::bind_method(D_METHOD("add", "o"), &CN::add);
	ClassDB::bind_method(D_METHOD("sub", "o"), &CN::sub);
	ClassDB::bind_method(D_METHOD("neg"), &CN::neg);
	ClassDB::bind_method(D_METHOD("conj"), &CN::conj);
	ClassDB::bind_method(D_METHOD("mul", "o"), &CN::mul);
	ClassDB::bind_method(D_METHOD("scale", "s"), &CN::scale);
	ClassDB::bind_method(D_METHOD("mul_i"), &CN::mul_i);
	ClassDB::bind_method(D_METHOD("mul_neg_i"), &CN::mul_neg_i);
	ClassDB::bind_method(D_METHOD("abs2"), &CN::abs2);
	ClassDB::bind_method(D_METHOD("abs_val"), &CN::abs_val);
	ClassDB::bind_method(D_METHOD("div", "o"), &CN::div);
	ClassDB::bind_method(D_METHOD("div_real", "s"), &CN::div_real);
	ClassDB::bind_method(D_METHOD("unit"), &CN::unit);
	ClassDB::bind_method(D_METHOD("is_zero"), &CN::is_zero);
	ClassDB::bind_method(D_METHOD("to_vector2"), &CN::to_vector2);
	ClassDB::bind_method(D_METHOD("fx"), &CN::fx);
	ClassDB::bind_method(D_METHOD("fy"), &CN::fy);
	ClassDB::bind_method(D_METHOD("get_re"), &CN::get_re);
	ClassDB::bind_method(D_METHOD("get_im"), &CN::get_im);
	ClassDB::bind_method(D_METHOD("limbs"), &CN::limbs);
	// (EDITOR, not STORAGE: a default instance's value wrappers are not cached as "default values")
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "re", godot::PROPERTY_HINT_RESOURCE_TYPE, "Num", godot::PROPERTY_USAGE_NONE), "", "get_re");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "im", godot::PROPERTY_HINT_RESOURCE_TYPE, "Num", godot::PROPERTY_USAGE_NONE), "", "get_im");
}

Ref<CN> CN::wrap(CNV v) {
	Ref<CN> z;
	z.instantiate();
	z->value = std::move(v);
	return z;
}
Ref<CN> CN::make(const Ref<Num> &re, const Ref<Num> &im) {
	return std::visit([&](const auto &r) -> Ref<CN> {
		using T = std::decay_t<decltype(r)>;
		const T *i = std::get_if<T>(&im->value);
		ERR_FAIL_NULL_V_MSG(i, Ref<CN>(), "CN.make: parts of different tiers");
		return wrap(rc::CN<T>{r, *i});
	}, re->value);
}
namespace {
template <class F>
Ref<CN> cn_binop(const CNV &a, const Ref<CN> &o, F &&f) {
	return std::visit([&](const auto &x) -> Ref<CN> {
		using T = std::decay_t<decltype(x)>;
		const T *y = std::get_if<T>(&o->value);
		ERR_FAIL_NULL_V_MSG(y, Ref<CN>(), "CN: operands of different tiers");
		return CN::wrap(f(x, *y));
	}, a);
}
template <class F>
Ref<CN> cn_scalar(const CNV &a, const Ref<Num> &s, F &&f) {
	return std::visit([&](const auto &x) -> Ref<CN> {
		using N = std::decay_t<decltype(x.re)>;
		const N *y = std::get_if<N>(&s->value);
		ERR_FAIL_NULL_V_MSG(y, Ref<CN>(), "CN: scalar of a different tier");
		return CN::wrap(f(x, *y));
	}, a);
}
} // namespace
Ref<CN> CN::add(const Ref<CN> &o) const {
	return cn_binop(value, o, [](const auto &x, const auto &y) { return x.add(y); });
}
Ref<CN> CN::sub(const Ref<CN> &o) const {
	return cn_binop(value, o, [](const auto &x, const auto &y) { return x.sub(y); });
}
Ref<CN> CN::neg() const {
	return std::visit([](const auto &x) { return wrap(x.neg()); }, value);
}
Ref<CN> CN::conj() const {
	return std::visit([](const auto &x) { return wrap(x.conj()); }, value);
}
Ref<CN> CN::mul(const Ref<CN> &o) const {
	return cn_binop(value, o, [](const auto &x, const auto &y) { return x.mul(y); });
}
Ref<CN> CN::scale(const Ref<Num> &s) const {
	return cn_scalar(value, s, [](const auto &x, const auto &y) { return x.scale(y); });
}
Ref<CN> CN::mul_i() const {
	return std::visit([](const auto &x) { return wrap(x.mul_i()); }, value);
}
Ref<CN> CN::mul_neg_i() const {
	return std::visit([](const auto &x) { return wrap(x.mul_neg_i()); }, value);
}
Ref<Num> CN::abs2() const {
	return std::visit([](const auto &x) { return Num::wrap(x.abs2()); }, value);
}
Ref<Num> CN::abs_val() const {
	return std::visit([](const auto &x) { return Num::wrap(x.abs_val()); }, value);
}
Ref<CN> CN::div(const Ref<CN> &o) const {
	return cn_binop(value, o, [](const auto &x, const auto &y) { return x.div(y); });
}
Ref<CN> CN::div_real(const Ref<Num> &s) const {
	return cn_scalar(value, s, [](const auto &x, const auto &y) { return x.div_real(y); });
}
Ref<CN> CN::unit() const {
	return std::visit([](const auto &x) { return wrap(x.unit()); }, value);
}
bool CN::is_zero() const {
	return std::visit([](const auto &x) { return x.is_zero(); }, value);
}
Ref<Num> CN::dot(const Ref<CN> &a, const Ref<CN> &b) {
	return std::visit([&](const auto &x) -> Ref<Num> {
		using T = std::decay_t<decltype(x)>;
		const T *y = std::get_if<T>(&b->value);
		ERR_FAIL_NULL_V_MSG(y, Ref<Num>(), "CN.dot: operands of different tiers");
		return Num::wrap(T::dot(x, *y));
	}, a->value);
}
Ref<Num> CN::cross(const Ref<CN> &a, const Ref<CN> &b) {
	return std::visit([&](const auto &x) -> Ref<Num> {
		using T = std::decay_t<decltype(x)>;
		const T *y = std::get_if<T>(&b->value);
		ERR_FAIL_NULL_V_MSG(y, Ref<Num>(), "CN.cross: operands of different tiers");
		return Num::wrap(T::cross(x, *y));
	}, a->value);
}
godot::Vector2 CN::to_vector2() const {
	return godot::Vector2(real_t(fx()), real_t(fy()));
}
double CN::fx() const {
	return std::visit([](const auto &x) { return x.fx(); }, value);
}
double CN::fy() const {
	return std::visit([](const auto &x) { return x.fy(); }, value);
}
Ref<Num> CN::get_re() const {
	return std::visit([](const auto &x) { return Num::wrap(x.re); }, value);
}
Ref<Num> CN::get_im() const {
	return std::visit([](const auto &x) { return Num::wrap(x.im); }, value);
}
int CN::limbs() const {
	return std::visit([](const auto &x) { return rcgd::limbs_of(x.re); }, value);
}

// ---------------------------------------------------------------- BigFloat

void BigFloat::_bind_methods() {
	ClassDB::bind_static_method("BigFloat", D_METHOD("zero", "n"), &BigFloat::zero);
	ClassDB::bind_static_method("BigFloat", D_METHOD("infinity", "n"), &BigFloat::infinity);
	ClassDB::bind_static_method("BigFloat", D_METHOD("limbs_for_bits", "bits"), &BigFloat::limbs_for_bits);
	ClassDB::bind_static_method("BigFloat", D_METHOD("from_float", "f", "n"), &BigFloat::from_float);
	ClassDB::bind_static_method("BigFloat", D_METHOD("from_string", "s", "n"), &BigFloat::from_string);
	ClassDB::bind_static_method("BigFloat", D_METHOD("from_int", "v", "n"), &BigFloat::from_int);
	BIND_CONSTANT(BITS);
}
Ref<Num> BigFloat::zero(int n) {
	return Num::wrap(rc::BigFloat::zero(n));
}
Ref<Num> BigFloat::infinity(int n) {
	return Num::wrap(rc::BigFloat::infinity(n));
}
int BigFloat::limbs_for_bits(int bits) {
	return rc::BigFloat::limbs_for_bits(bits);
}
Ref<Num> BigFloat::from_float(double f, int n) {
	return Num::wrap(rc::BigFloat::from_float(f, n));
}
Ref<Num> BigFloat::from_string(const godot::String &s, int n) {
	return Num::wrap(rc::BigFloat::from_string(to_std(s), n));
}
Ref<Num> BigFloat::from_int(int64_t v, int n) {
	return Num::wrap(rc::BigFloat::from_int(v, n));
}

// ---------------------------------------------------------------- F64Num / DualNum

void F64Num::_bind_methods() {
	ClassDB::bind_static_method("F64Num", D_METHOD("of", "x"), &F64Num::of);
}
void DualNum::_bind_methods() {
	ClassDB::bind_static_method("DualNum", D_METHOD("of", "x", "dx"), &DualNum::of, DEFVAL(0.0));
}
