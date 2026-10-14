#include "gd_num.hpp"

#include <godot_cpp/core/class_db.hpp>

using godot::ClassDB;
using godot::D_METHOD;
using godot::PropertyInfo;
using godot::Ref;
using godot::Variant;
using namespace rcgd;

// ---------------------------------------------------------------- NNumCtx

void NNumCtx::_bind_methods() {
	ClassDB::bind_static_method("NNumCtx", D_METHOD("f64"), &NNumCtx::f64);
	ClassDB::bind_static_method("NNumCtx", D_METHOD("with_limbs", "n"), &NNumCtx::with_limbs);
	ClassDB::bind_static_method("NNumCtx", D_METHOD("dual"), &NNumCtx::dual);
	ClassDB::bind_method(D_METHOD("is_f64"), &NNumCtx::is_f64);
	ClassDB::bind_method(D_METHOD("is_dual"), &NNumCtx::is_dual);
	ClassDB::bind_method(D_METHOD("bits"), &NNumCtx::bits);
	ClassDB::bind_method(D_METHOD("eps"), &NNumCtx::eps);
	ClassDB::bind_method(D_METHOD("num", "f"), &NNumCtx::num);
	ClassDB::bind_method(D_METHOD("from_string", "s"), &NNumCtx::from_string);
	ClassDB::bind_method(D_METHOD("inf"), &NNumCtx::inf);
	ClassDB::bind_method(D_METHOD("zero"), &NNumCtx::zero);
	ClassDB::bind_method(D_METHOD("one"), &NNumCtx::one);
	ClassDB::bind_method(D_METHOD("cn", "x", "y"), &NNumCtx::cn);
	ClassDB::bind_method(D_METHOD("name"), &NNumCtx::name);
	ClassDB::bind_method(D_METHOD("get_limbs"), &NNumCtx::get_limbs);
	ClassDB::bind_method(D_METHOD("set_limbs", "n"), &NNumCtx::set_limbs);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "limbs"), "set_limbs", "get_limbs");
	BIND_CONSTANT(DUAL_LIMBS);
}

Ref<NNumCtx> NNumCtx::f64() {
	Ref<NNumCtx> c;
	c.instantiate();
	c->limbs = 0;
	return c;
}
Ref<NNumCtx> NNumCtx::with_limbs(int n) {
	Ref<NNumCtx> c;
	c.instantiate();
	c->limbs = n;
	return c;
}
Ref<NNumCtx> NNumCtx::dual() {
	Ref<NNumCtx> c;
	c.instantiate();
	c->limbs = DUAL_LIMBS;
	return c;
}
double NNumCtx::eps() const {
	return std::pow(2.0, -bits());
}
Ref<NNum> NNumCtx::num(double f) const {
	return NNum::wrap(num_of(limbs, f));
}
Ref<NNum> NNumCtx::from_string(const godot::String &s) const {
	if (limbs <= 0) {
		return num(s.to_float());
	}
	return NNum::wrap(rc::BigFloat::from_string(to_std(s), limbs));
}
Ref<NNum> NNumCtx::inf() const {
	if (limbs <= 0) {
		return num(std::numeric_limits<double>::infinity());
	}
	return NNum::wrap(rc::BigFloat::infinity(limbs));
}
Ref<NNum> NNumCtx::zero() const {
	return num(0.0);
}
Ref<NNum> NNumCtx::one() const {
	return num(1.0);
}
Ref<NCN> NNumCtx::cn(double x, double y) const {
	if (limbs == 0) {
		return NCN::wrap(rc::CN<rc::F64>{rc::F64{x}, rc::F64{y}});
	}
	if (limbs == DUAL_LIMBS) {
		return NCN::wrap(rc::CN<rc::Dual>{rc::Dual{x, 0.0}, rc::Dual{y, 0.0}});
	}
	return NCN::wrap(rc::CN<rc::BigFloat>{rc::BigFloat::from_float(x, limbs), rc::BigFloat::from_float(y, limbs)});
}
godot::String NNumCtx::name() const {
	if (limbs == DUAL_LIMBS) {
		return "dual";
	}
	return limbs == 0 ? godot::String("f64") : to_gd("big" + std::to_string(bits()));
}

// ---------------------------------------------------------------- NNum

void NNum::_bind_methods() {
	ClassDB::bind_static_method("NNum", D_METHOD("of", "x"), &NNum::of);
	ClassDB::bind_static_method("NNum", D_METHOD("dual_of", "x", "dx"), &NNum::dual_of);
	ClassDB::bind_method(D_METHOD("is_inf"), &NNum::is_inf);
	ClassDB::bind_method(D_METHOD("is_zero"), &NNum::is_zero);
	ClassDB::bind_method(D_METHOD("is_negative"), &NNum::is_negative);
	ClassDB::bind_method(D_METHOD("to_float"), &NNum::to_float);
	ClassDB::bind_method(D_METHOD("neg"), &NNum::neg);
	ClassDB::bind_method(D_METHOD("abs_val"), &NNum::abs_val);
	ClassDB::bind_method(D_METHOD("add", "o"), &NNum::add);
	ClassDB::bind_method(D_METHOD("sub", "o"), &NNum::sub);
	ClassDB::bind_method(D_METHOD("mul", "o"), &NNum::mul);
	ClassDB::bind_method(D_METHOD("div", "o"), &NNum::div);
	ClassDB::bind_method(D_METHOD("sqrt_val"), &NNum::sqrt_val);
	ClassDB::bind_method(D_METHOD("mul_pow2", "k"), &NNum::mul_pow2);
	ClassDB::bind_method(D_METHOD("cmp", "o"), &NNum::cmp);
	ClassDB::bind_method(D_METHOD("limbs"), &NNum::limbs);
	ClassDB::bind_method(D_METHOD("get_v"), &NNum::get_v);
	ClassDB::bind_method(D_METHOD("get_d"), &NNum::get_d);
	ClassDB::bind_method(D_METHOD("get_sign"), &NNum::get_sign);
	ClassDB::bind_method(D_METHOD("get_exp"), &NNum::get_exp);
	ClassDB::bind_method(D_METHOD("get_limbs"), &NNum::get_limbs);
	ClassDB::bind_method(D_METHOD("with_limbs", "n"), &NNum::with_limbs);
	ClassDB::bind_method(D_METHOD("precision_limbs"), &NNum::precision_limbs);
	ClassDB::bind_method(D_METHOD("top_exp"), &NNum::top_exp);
	ClassDB::bind_method(D_METHOD("debug_string"), &NNum::debug_string);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "v"), "", "get_v");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "d"), "", "get_d");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sign"), "", "get_sign");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "exp"), "", "get_exp");
}

Ref<NNum> NNum::wrap(NumV v) {
	Ref<NNum> n;
	n.instantiate();
	n->value = std::move(v);
	return n;
}
Ref<NNum> NNum::of(double x) {
	return wrap(rc::F64{x});
}
Ref<NNum> NNum::dual_of(double x, double dx) {
	return wrap(rc::Dual{x, dx});
}
bool NNum::is_inf() const {
	return std::visit([](const auto &x) { return x.is_inf(); }, value);
}
bool NNum::is_zero() const {
	return std::visit([](const auto &x) { return x.is_zero(); }, value);
}
bool NNum::is_negative() const {
	return std::visit([](const auto &x) { return x.is_negative(); }, value);
}
double NNum::to_float() const {
	return rcgd::to_float(value);
}
Ref<NNum> NNum::neg() const {
	return std::visit([](const auto &x) { return wrap(x.neg()); }, value);
}
Ref<NNum> NNum::abs_val() const {
	return std::visit([](const auto &x) { return wrap(x.abs_val()); }, value);
}
namespace {
template <class F>
Ref<NNum> binop(const NumV &a, const Ref<NNum> &o, F &&f) {
	return std::visit([&](const auto &x) -> Ref<NNum> {
		using T = std::decay_t<decltype(x)>;
		const T *y = std::get_if<T>(&o->value);
		ERR_FAIL_NULL_V_MSG(y, Ref<NNum>(), "NNum: operands of different tiers");
		return NNum::wrap(f(x, *y));
	}, a);
}
} // namespace
Ref<NNum> NNum::add(const Ref<NNum> &o) const {
	return binop(value, o, [](const auto &x, const auto &y) { return x.add(y); });
}
Ref<NNum> NNum::sub(const Ref<NNum> &o) const {
	return binop(value, o, [](const auto &x, const auto &y) { return x.sub(y); });
}
Ref<NNum> NNum::mul(const Ref<NNum> &o) const {
	return binop(value, o, [](const auto &x, const auto &y) { return x.mul(y); });
}
Ref<NNum> NNum::div(const Ref<NNum> &o) const {
	return binop(value, o, [](const auto &x, const auto &y) { return x.div(y); });
}
Ref<NNum> NNum::sqrt_val() const {
	return std::visit([](const auto &x) { return wrap(x.sqrt_val()); }, value);
}
Ref<NNum> NNum::mul_pow2(int k) const {
	return std::visit([k](const auto &x) { return wrap(x.mul_pow2(k)); }, value);
}
int NNum::cmp(const Ref<NNum> &o) const {
	return std::visit([&](const auto &x) -> int {
		using T = std::decay_t<decltype(x)>;
		const T *y = std::get_if<T>(&o->value);
		ERR_FAIL_NULL_V_MSG(y, 0, "NNum: operands of different tiers");
		return x.cmp(*y);
	}, value);
}
double NNum::get_d() const {
	if (const rc::Dual *d = std::get_if<rc::Dual>(&value)) {
		return d->d;
	}
	return 0.0;
}
int NNum::get_sign() const {
	if (const rc::BigFloat *b = std::get_if<rc::BigFloat>(&value)) {
		return b->sign;
	}
	const double v = to_float();
	return v == 0.0 ? 0 : (v > 0.0 ? 1 : -1);
}
int64_t NNum::get_exp() const {
	if (const rc::BigFloat *b = std::get_if<rc::BigFloat>(&value)) {
		return b->exp;
	}
	return 0;
}
godot::PackedInt64Array NNum::get_limbs() const {
	godot::PackedInt64Array out;
	if (const rc::BigFloat *b = std::get_if<rc::BigFloat>(&value)) {
		out.resize(int64_t(b->limbs.size()));
		for (size_t i = 0; i < b->limbs.size(); i++) {
			out[int64_t(i)] = b->limbs[i];
		}
	}
	return out;
}
Ref<NNum> NNum::with_limbs(int n) const {
	if (const rc::BigFloat *b = std::get_if<rc::BigFloat>(&value)) {
		return wrap(b->with_limbs(n));
	}
	return wrap(value);
}
int NNum::precision_limbs() const {
	if (const rc::BigFloat *b = std::get_if<rc::BigFloat>(&value)) {
		return b->precision_limbs();
	}
	return 0;
}
int64_t NNum::top_exp() const {
	if (const rc::BigFloat *b = std::get_if<rc::BigFloat>(&value)) {
		return b->top_exp();
	}
	return 0;
}
godot::String NNum::debug_string() const {
	if (const rc::BigFloat *b = std::get_if<rc::BigFloat>(&value)) {
		return to_gd(b->debug_string());
	}
	return godot::String::num(to_float(), 17);
}

// ---------------------------------------------------------------- NCN

void NCN::_bind_methods() {
	ClassDB::bind_static_method("NCN", D_METHOD("make", "re", "im"), &NCN::make);
	ClassDB::bind_static_method("NCN", D_METHOD("dot", "a", "b"), &NCN::dot);
	ClassDB::bind_static_method("NCN", D_METHOD("cross", "a", "b"), &NCN::cross);
	ClassDB::bind_method(D_METHOD("add", "o"), &NCN::add);
	ClassDB::bind_method(D_METHOD("sub", "o"), &NCN::sub);
	ClassDB::bind_method(D_METHOD("neg"), &NCN::neg);
	ClassDB::bind_method(D_METHOD("conj"), &NCN::conj);
	ClassDB::bind_method(D_METHOD("mul", "o"), &NCN::mul);
	ClassDB::bind_method(D_METHOD("scale", "s"), &NCN::scale);
	ClassDB::bind_method(D_METHOD("mul_i"), &NCN::mul_i);
	ClassDB::bind_method(D_METHOD("mul_neg_i"), &NCN::mul_neg_i);
	ClassDB::bind_method(D_METHOD("abs2"), &NCN::abs2);
	ClassDB::bind_method(D_METHOD("abs_val"), &NCN::abs_val);
	ClassDB::bind_method(D_METHOD("div", "o"), &NCN::div);
	ClassDB::bind_method(D_METHOD("div_real", "s"), &NCN::div_real);
	ClassDB::bind_method(D_METHOD("unit"), &NCN::unit);
	ClassDB::bind_method(D_METHOD("is_zero"), &NCN::is_zero);
	ClassDB::bind_method(D_METHOD("to_vector2"), &NCN::to_vector2);
	ClassDB::bind_method(D_METHOD("fx"), &NCN::fx);
	ClassDB::bind_method(D_METHOD("fy"), &NCN::fy);
	ClassDB::bind_method(D_METHOD("get_re"), &NCN::get_re);
	ClassDB::bind_method(D_METHOD("get_im"), &NCN::get_im);
	ClassDB::bind_method(D_METHOD("limbs"), &NCN::limbs);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "re", godot::PROPERTY_HINT_RESOURCE_TYPE, "NNum"), "", "get_re");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "im", godot::PROPERTY_HINT_RESOURCE_TYPE, "NNum"), "", "get_im");
}

Ref<NCN> NCN::wrap(CNV v) {
	Ref<NCN> z;
	z.instantiate();
	z->value = std::move(v);
	return z;
}
Ref<NCN> NCN::make(const Ref<NNum> &re, const Ref<NNum> &im) {
	return std::visit([&](const auto &r) -> Ref<NCN> {
		using T = std::decay_t<decltype(r)>;
		const T *i = std::get_if<T>(&im->value);
		ERR_FAIL_NULL_V_MSG(i, Ref<NCN>(), "NCN.make: parts of different tiers");
		return wrap(rc::CN<T>{r, *i});
	}, re->value);
}
namespace {
template <class F>
Ref<NCN> cn_binop(const CNV &a, const Ref<NCN> &o, F &&f) {
	return std::visit([&](const auto &x) -> Ref<NCN> {
		using T = std::decay_t<decltype(x)>;
		const T *y = std::get_if<T>(&o->value);
		ERR_FAIL_NULL_V_MSG(y, Ref<NCN>(), "NCN: operands of different tiers");
		return NCN::wrap(f(x, *y));
	}, a);
}
template <class F>
Ref<NCN> cn_scalar(const CNV &a, const Ref<NNum> &s, F &&f) {
	return std::visit([&](const auto &x) -> Ref<NCN> {
		using N = std::decay_t<decltype(x.re)>;
		const N *y = std::get_if<N>(&s->value);
		ERR_FAIL_NULL_V_MSG(y, Ref<NCN>(), "NCN: scalar of a different tier");
		return NCN::wrap(f(x, *y));
	}, a);
}
} // namespace
Ref<NCN> NCN::add(const Ref<NCN> &o) const {
	return cn_binop(value, o, [](const auto &x, const auto &y) { return x.add(y); });
}
Ref<NCN> NCN::sub(const Ref<NCN> &o) const {
	return cn_binop(value, o, [](const auto &x, const auto &y) { return x.sub(y); });
}
Ref<NCN> NCN::neg() const {
	return std::visit([](const auto &x) { return wrap(x.neg()); }, value);
}
Ref<NCN> NCN::conj() const {
	return std::visit([](const auto &x) { return wrap(x.conj()); }, value);
}
Ref<NCN> NCN::mul(const Ref<NCN> &o) const {
	return cn_binop(value, o, [](const auto &x, const auto &y) { return x.mul(y); });
}
Ref<NCN> NCN::scale(const Ref<NNum> &s) const {
	return cn_scalar(value, s, [](const auto &x, const auto &y) { return x.scale(y); });
}
Ref<NCN> NCN::mul_i() const {
	return std::visit([](const auto &x) { return wrap(x.mul_i()); }, value);
}
Ref<NCN> NCN::mul_neg_i() const {
	return std::visit([](const auto &x) { return wrap(x.mul_neg_i()); }, value);
}
Ref<NNum> NCN::abs2() const {
	return std::visit([](const auto &x) { return NNum::wrap(x.abs2()); }, value);
}
Ref<NNum> NCN::abs_val() const {
	return std::visit([](const auto &x) { return NNum::wrap(x.abs_val()); }, value);
}
Ref<NCN> NCN::div(const Ref<NCN> &o) const {
	return cn_binop(value, o, [](const auto &x, const auto &y) { return x.div(y); });
}
Ref<NCN> NCN::div_real(const Ref<NNum> &s) const {
	return cn_scalar(value, s, [](const auto &x, const auto &y) { return x.div_real(y); });
}
Ref<NCN> NCN::unit() const {
	return std::visit([](const auto &x) { return wrap(x.unit()); }, value);
}
bool NCN::is_zero() const {
	return std::visit([](const auto &x) { return x.is_zero(); }, value);
}
Ref<NNum> NCN::dot(const Ref<NCN> &a, const Ref<NCN> &b) {
	return std::visit([&](const auto &x) -> Ref<NNum> {
		using T = std::decay_t<decltype(x)>;
		const T *y = std::get_if<T>(&b->value);
		ERR_FAIL_NULL_V_MSG(y, Ref<NNum>(), "NCN.dot: operands of different tiers");
		return NNum::wrap(T::dot(x, *y));
	}, a->value);
}
Ref<NNum> NCN::cross(const Ref<NCN> &a, const Ref<NCN> &b) {
	return std::visit([&](const auto &x) -> Ref<NNum> {
		using T = std::decay_t<decltype(x)>;
		const T *y = std::get_if<T>(&b->value);
		ERR_FAIL_NULL_V_MSG(y, Ref<NNum>(), "NCN.cross: operands of different tiers");
		return NNum::wrap(T::cross(x, *y));
	}, a->value);
}
godot::Vector2 NCN::to_vector2() const {
	return godot::Vector2(real_t(fx()), real_t(fy()));
}
double NCN::fx() const {
	return std::visit([](const auto &x) { return x.fx(); }, value);
}
double NCN::fy() const {
	return std::visit([](const auto &x) { return x.fy(); }, value);
}
Ref<NNum> NCN::get_re() const {
	return std::visit([](const auto &x) { return NNum::wrap(x.re); }, value);
}
Ref<NNum> NCN::get_im() const {
	return std::visit([](const auto &x) { return NNum::wrap(x.im); }, value);
}
int NCN::limbs() const {
	return std::visit([](const auto &x) { return rcgd::limbs_of(x.re); }, value);
}

// ---------------------------------------------------------------- NBigFloat

void NBigFloat::_bind_methods() {
	ClassDB::bind_static_method("NBigFloat", D_METHOD("zero", "n"), &NBigFloat::zero);
	ClassDB::bind_static_method("NBigFloat", D_METHOD("infinity", "n"), &NBigFloat::infinity);
	ClassDB::bind_static_method("NBigFloat", D_METHOD("limbs_for_bits", "bits"), &NBigFloat::limbs_for_bits);
	ClassDB::bind_static_method("NBigFloat", D_METHOD("from_float", "f", "n"), &NBigFloat::from_float);
	ClassDB::bind_static_method("NBigFloat", D_METHOD("from_string", "s", "n"), &NBigFloat::from_string);
	ClassDB::bind_static_method("NBigFloat", D_METHOD("from_int", "v", "n"), &NBigFloat::from_int);
	BIND_CONSTANT(BITS);
}
Ref<NNum> NBigFloat::zero(int n) {
	return NNum::wrap(rc::BigFloat::zero(n));
}
Ref<NNum> NBigFloat::infinity(int n) {
	return NNum::wrap(rc::BigFloat::infinity(n));
}
int NBigFloat::limbs_for_bits(int bits) {
	return rc::BigFloat::limbs_for_bits(bits);
}
Ref<NNum> NBigFloat::from_float(double f, int n) {
	return NNum::wrap(rc::BigFloat::from_float(f, n));
}
Ref<NNum> NBigFloat::from_string(const godot::String &s, int n) {
	return NNum::wrap(rc::BigFloat::from_string(to_std(s), n));
}
Ref<NNum> NBigFloat::from_int(int64_t v, int n) {
	return NNum::wrap(rc::BigFloat::from_int(v, n));
}
