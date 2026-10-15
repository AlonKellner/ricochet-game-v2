#include "rc/bigfloat.hpp"

#include "rc/error.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace rc {

namespace {

// Leading-digit integer parse of a decimal string: GDScript String.to_int()
// (optional sign, digits; anything else stops the parse; no digits -> 0).
int64_t to_int_prefix(const std::string &s) {
	size_t i = 0;
	bool neg = false;
	if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
		neg = s[i] == '-';
		i++;
	}
	int64_t v = 0;
	while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
		v = v * 10 + (s[i] - '0');
		i++;
	}
	return neg ? -v : v;
}

} // namespace

BigFloat BigFloat::zero(int n) {
	BigFloat z;
	z.limbs.assign(size_t(n), 0);
	return z;
}

BigFloat BigFloat::infinity(int n) {
	BigFloat r = from_int(1, n);
	r.exp = INF_EXP;
	return r;
}

int BigFloat::limbs_for_bits(int bits) {
	return std::max(2, (bits + BITS - 1) / BITS);
}

BigFloat BigFloat::from_float(double f, int n) {
	BigFloat r = zero(n);
	if (f == 0.0) {
		return r;
	}
	if (std::isinf(f)) {
		// the exact point at infinity of a tier (Stage 88: the wrap candidate's parameter)
		r = infinity(n);
		r.sign = f > 0.0 ? 1 : -1;
		return r;
	}
	r.sign = f > 0.0 ? 1 : -1;
	double m = std::fabs(f);
	// scale m into [2^52, 2^53): exact power-of-two scaling
	int64_t e = int64_t(std::floor(std::log(m) / std::log(2.0))) - 52;
	m = e > -1000 ? m / std::pow(2.0, double(e)) : (m * std::pow(2.0, 600.0)) / std::pow(2.0, double(e + 600));
	while (m >= 9007199254740992.0) {
		m *= 0.5;
		e += 1;
	}
	while (m < 4503599627370496.0) {
		m *= 2.0;
		e -= 1;
	}
	const int64_t mi = int64_t(m); // 53-bit integer
	// place the 53-bit integer at the top of n limbs (bit 27 of top limb set)
	// 53 bits = top limb 28 bits + next 25 bits
	const int64_t top = mi >> 25;
	const int64_t low = (mi & ((int64_t(1) << 25) - 1)) << 3;
	r.limbs[size_t(n - 1)] = top;
	r.limbs[size_t(n - 2)] = low;
	r.exp = e - 3 - BITS * int64_t(n - 2);
	return r;
}

BigFloat BigFloat::from_string(const std::string &s, int n) {
	// decimal string "[-]digits[.digits][e[+-]digits]"
	std::string str = s;
	auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
	while (!str.empty() && is_space(str.front())) {
		str.erase(str.begin());
	}
	while (!str.empty() && is_space(str.back())) {
		str.pop_back();
	}
	bool neg = false;
	if (!str.empty() && str[0] == '-') {
		neg = true;
		str = str.substr(1);
	} else if (!str.empty() && str[0] == '+') {
		str = str.substr(1);
	}
	int64_t e10 = 0;
	size_t epos = std::string::npos;
	for (size_t i = 0; i < str.size(); i++) {
		if (str[i] == 'e' || str[i] == 'E') {
			epos = i;
			break;
		}
	}
	if (epos != std::string::npos) {
		e10 = to_int_prefix(str.substr(epos + 1));
		str = str.substr(0, epos);
	}
	const size_t dot = str.find('.');
	if (dot != std::string::npos) {
		e10 -= int64_t(str.size() - dot - 1);
		str = str.substr(0, dot) + str.substr(dot + 1);
	}
	const int wp = n + 1;
	BigFloat acc = zero(wp);
	const BigFloat ten = from_int(10, wp);
	// process 6 digits at a time
	size_t i = 0;
	while (i < str.size()) {
		const size_t chunk_len = std::min<size_t>(6, str.size() - i);
		const int64_t chunk = to_int_prefix(str.substr(i, chunk_len));
		acc = acc.mul(from_int(int64_t(std::pow(10.0, double(chunk_len))), wp)).add(from_int(chunk, wp));
		i += chunk_len;
	}
	if (e10 != 0) {
		BigFloat p = from_int(1, wp);
		BigFloat base = ten;
		int64_t k = std::llabs(e10);
		while (k > 0) {
			if (k & 1) {
				p = p.mul(base);
			}
			base = base.mul(base);
			k >>= 1;
		}
		acc = e10 > 0 ? acc.mul(p) : acc.div(p);
	}
	if (neg) {
		acc = acc.neg();
	}
	return acc.with_limbs(n);
}

BigFloat BigFloat::from_int(int64_t v, int n) {
	if (v == 0) {
		return zero(n);
	}
	Limbs a;
	int64_t x = std::llabs(v);
	while (x > 0) {
		a.push_back(x & MASK);
		x >>= BITS;
	}
	return normalize(a, 0, v > 0 ? 1 : -1, n);
}

BigFloat BigFloat::neg() const {
	BigFloat r;
	r.sign = -sign;
	r.exp = exp;
	r.limbs = limbs;
	return r;
}

BigFloat BigFloat::abs_val() const {
	if (sign >= 0) {
		return *this;
	}
	return neg();
}

BigFloat BigFloat::mul_pow2(int64_t k) const {
	if (sign == 0) {
		return *this;
	}
	BigFloat r;
	r.sign = sign;
	r.exp = exp + k;
	r.limbs = limbs;
	return r;
}

BigFloat BigFloat::with_limbs(int n) const {
	if (int(limbs.size()) == n) {
		return *this;
	}
	if (sign == 0) {
		return zero(n);
	}
	return normalize(limbs, exp, sign, n);
}

double BigFloat::to_float() const {
	if (sign == 0) {
		return 0.0;
	}
	const size_t n = limbs.size();
	double v = double(limbs[n - 1]) * 268435456.0 + double(limbs[n - 2]);
	if (n >= 3) {
		v = v * 268435456.0 + double(limbs[n - 3]);
		const int64_t e = exp + BITS * int64_t(n - 3);
		return e > -1000 ? sign * v * std::pow(2.0, double(e)) : sign * (v * std::pow(2.0, double(e + 600))) * std::pow(2.0, -600.0);
	}
	return sign * v * std::pow(2.0, double(exp + BITS * int64_t(n - 2)));
}

int BigFloat::cmp_abs(const BigFloat &o) const {
	if (sign == 0) {
		return o.sign == 0 ? 0 : -1;
	}
	if (o.sign == 0) {
		return 1;
	}
	const int64_t te = top_exp();
	const int64_t to = o.top_exp();
	if (te != to) {
		return te > to ? 1 : -1;
	}
	const Limbs &a = limbs;
	const Limbs &b = o.limbs;
	const size_t na = a.size();
	const size_t nb = b.size();
	size_t k = 0;
	while (k < std::max(na, nb)) {
		const int64_t x = k < na ? a[na - 1 - k] : 0;
		const int64_t y = k < nb ? b[nb - 1 - k] : 0;
		if (x != y) {
			return x > y ? 1 : -1;
		}
		k++;
	}
	return 0;
}

int BigFloat::cmp(const BigFloat &o) const {
	if (sign != o.sign) {
		return sign > o.sign ? 1 : -1;
	}
	if (sign == 0) {
		return 0;
	}
	return cmp_abs(o) * sign;
}

BigFloat BigFloat::add(const BigFloat &o) const {
	if (o.sign == 0) {
		return *this;
	}
	if (sign == 0) {
		return o.with_limbs(int(limbs.size()));
	}
	const int n = int(limbs.size());
	const BigFloat *big = this;
	const BigFloat *small = &o;
	if (cmp_abs(o) < 0) {
		big = &o;
		small = this;
	}
	// working buffer: one guard limb below the n limbs of `big`
	Limbs wb(size_t(n + 2), 0);
	const Limbs &bl = big->limbs;
	const int bn = int(bl.size());
	for (int i = 0; i < n; i++) {
		const int src = bn - n + i;
		wb[size_t(i + 1)] = src >= 0 ? bl[size_t(src)] : 0;
	}
	const int64_t w_exp = big->exp + BITS * int64_t(bn - n) - BITS;
	// align small into the same frame (bit p of ws has exponent w_exp + p)
	const Limbs ws = place(small->limbs, small->exp, w_exp, n + 2);
	if (big->sign == small->sign) {
		int64_t carry = 0;
		for (int i = 0; i < n + 2; i++) {
			const int64_t s = wb[size_t(i)] + ws[size_t(i)] + carry;
			wb[size_t(i)] = s & MASK;
			carry = s >> BITS;
		}
		if (carry > 0) {
			wb.push_back(carry);
		}
		return normalize(wb, w_exp, big->sign, n);
	}
	int64_t borrow = 0;
	for (int i = 0; i < n + 2; i++) {
		int64_t d = wb[size_t(i)] - ws[size_t(i)] - borrow;
		if (d < 0) {
			d += BASE;
			borrow = 1;
		} else {
			borrow = 0;
		}
		wb[size_t(i)] = d;
	}
	return normalize(wb, w_exp, big->sign, n);
}

BigFloat BigFloat::sub(const BigFloat &o) const {
	return add(o.neg());
}

BigFloat BigFloat::mul(const BigFloat &o) const {
	const int n = int(limbs.size());
	if (sign == 0 || o.sign == 0) {
		return zero(n);
	}
	const Limbs &a = limbs;
	const Limbs &b = o.limbs;
	const size_t na = a.size();
	const size_t nb = b.size();
	// column sums in 128 bits (B1, fixed in Stage 102): each product is below
	// 2^56, so any realistic limb count (< 2^70 terms) cannot overflow; the
	// GDScript original summed in int64 and wrapped above ~128 limbs per factor
	__extension__ typedef __int128 i128;
	std::vector<i128> acc(na + nb, 0);
	for (size_t i = 0; i < na; i++) {
		const int64_t ai = a[i];
		if (ai == 0) {
			continue;
		}
		for (size_t j = 0; j < nb; j++) {
			acc[i + j] += i128(ai) * i128(b[j]);
		}
	}
	Limbs cols(na + nb, 0);
	i128 carry = 0;
	for (size_t k = 0; k < na + nb; k++) {
		const i128 s = acc[k] + carry;
		cols[k] = int64_t(s & i128(MASK));
		carry = s >> BITS;
	}
	return normalize(cols, exp + o.exp, sign * o.sign, n);
}

BigFloat BigFloat::div(const BigFloat &o) const {
	const int n = int(limbs.size());
	RC_ASSERT(o.sign != 0, "BigFloat division by zero");
	if (sign == 0) {
		return zero(n);
	}
	const int wp = n + 1;
	const BigFloat b = o.with_limbs(wp);
	// Newton reciprocal: x <- x (2 - b x), seeded from float64
	const int64_t e_b = b.top_exp();
	const BigFloat b_scaled = b.mul_pow2(-e_b); // in [1, 2)
	BigFloat x = from_float(1.0 / b_scaled.to_float(), wp);
	const BigFloat two = from_int(2, wp);
	int correct_bits = 50;
	while (correct_bits < BITS * wp + 8) {
		x = x.mul(two.sub(b_scaled.mul(x)));
		correct_bits *= 2;
	}
	const BigFloat r = with_limbs(wp).mul(x).mul_pow2(-e_b);
	// exact correction: the magnitude becomes floor(|a| / |b|) at r's
	// exponent, so the result is the correctly truncated quotient
	const int64_t sh = exp - o.exp - r.exp;
	const Limbs N = ishl(limbs, std::max<int64_t>(sh, 0));
	const Limbs D = ishl(o.limbs, std::max<int64_t>(-sh, 0));
	Limbs Q = itrim(r.limbs);
	Limbs QD = imul(Q, D);
	const Limbs one = {1};
	while (icmp(QD, N) > 0) {
		Q = isub(Q, one);
		QD = isub(QD, D);
	}
	Limbs QD1 = iadd(QD, D);
	while (icmp(QD1, N) <= 0) {
		Q = iadd(Q, one);
		QD1 = iadd(QD1, D);
	}
	return normalize(Q, r.exp, sign * o.sign, n);
}

BigFloat BigFloat::sqrt_val() const {
	const int n = int(limbs.size());
	if (sign == 0) {
		return *this;
	}
	RC_ASSERT(sign > 0, "BigFloat sqrt of negative");
	const int wp = n + 1;
	const BigFloat a = with_limbs(wp);
	// scale to even exponent near 1
	int64_t e = a.top_exp();
	if (e % 2 != 0) {
		e -= 1;
	}
	const BigFloat a_s = a.mul_pow2(-e); // in [1, 4)
	// Newton on inverse sqrt: y <- y (3 - a y^2) / 2
	BigFloat y = from_float(1.0 / std::sqrt(a_s.to_float()), wp);
	const BigFloat three = from_int(3, wp);
	int correct_bits = 50;
	while (correct_bits < BITS * wp + 8) {
		y = y.mul(three.sub(a_s.mul(y).mul(y))).mul_pow2(-1);
		correct_bits *= 2;
	}
	const BigFloat r = a_s.mul(y).mul_pow2(e / 2);
	// exact correction: the magnitude becomes floor(sqrt(a)) at r's exponent
	const int64_t sh = exp - 2 * r.exp;
	const Limbs A = ishl(limbs, std::max<int64_t>(sh, 0));
	Limbs R = itrim(r.limbs);
	const Limbs one = {1};
	while (icmp(ishl(imul(R, R), std::max<int64_t>(-sh, 0)), A) > 0) {
		R = isub(R, one);
	}
	Limbs R1 = iadd(R, one);
	while (icmp(ishl(imul(R1, R1), std::max<int64_t>(-sh, 0)), A) <= 0) {
		R = R1;
		R1 = iadd(R, one);
	}
	return normalize(R, r.exp, 1, n);
}

// Bits of the integer `src` (exponent src_exp) re-expressed in a frame whose
// bit 0 has exponent frame_exp, truncated to `len` limbs.
BigFloat::Limbs BigFloat::place(const Limbs &src, int64_t src_exp, int64_t frame_exp, int len) {
	Limbs out(size_t(len), 0);
	const int64_t sn = int64_t(src.size());
	const int64_t b0 = frame_exp - src_exp; // source bit index for target bit 0
	int64_t q = b0 / BITS;
	int64_t r = b0 - q * BITS;
	if (r < 0) {
		r += BITS;
		q -= 1;
	}
	for (int64_t i = 0; i < len; i++) {
		const int64_t j = q + i;
		if (j >= sn) {
			break;
		}
		int64_t lo = 0;
		if (j >= 0) {
			lo = src[size_t(j)] >> r;
		}
		int64_t hi = 0;
		if (r > 0 && j + 1 >= 0 && j + 1 < sn) {
			hi = (src[size_t(j + 1)] << (BITS - r)) & MASK;
		}
		out[size_t(i)] = (lo | hi) & MASK;
	}
	return out;
}

BigFloat BigFloat::normalize(const Limbs &arr, int64_t e, int s, int n) {
	int64_t top = int64_t(arr.size()) - 1;
	while (top >= 0 && arr[size_t(top)] == 0) {
		top -= 1;
	}
	if (top < 0) {
		return zero(n);
	}
	// bit length of top limb
	int64_t t = arr[size_t(top)];
	int bl = 0;
	while (t > 0) {
		bl += 1;
		t >>= 1;
	}
	const int lshift = BITS - bl; // shift left so top limb has bit BITS-1 set
	BigFloat r;
	r.sign = s;
	r.limbs.assign(size_t(n), 0);
	// result limb k (0..n-1) takes source position top - (n-1) + k, shifted
	for (int64_t k = 0; k < n; k++) {
		const int64_t src = top - (n - 1) + k;
		const int64_t cur = src >= 0 ? arr[size_t(src)] : 0;
		const int64_t below = src - 1 >= 0 ? arr[size_t(src - 1)] : 0;
		int64_t v = cur << lshift;
		if (lshift > 0) {
			v |= below >> (BITS - lshift);
		}
		r.limbs[size_t(k)] = v & MASK;
	}
	r.exp = e + BITS * (top - (n - 1)) - lshift;
	return r;
}

// ---------------------------------------------------------------- exact integers

BigFloat::Limbs BigFloat::itrim(const Limbs &a) {
	Limbs r = a;
	size_t k = r.size();
	while (k > 0 && r[k - 1] == 0) {
		k--;
	}
	r.resize(k);
	return r;
}

BigFloat::Limbs BigFloat::ishl(const Limbs &a, int64_t bits) {
	const int64_t q = bits / BITS;
	const int rb = int(bits - q * BITS);
	Limbs out(a.size() + size_t(q) + 1, 0);
	int64_t carry = 0;
	for (size_t i = 0; i < a.size(); i++) {
		const int64_t v = (a[i] << rb) | carry;
		out[i + size_t(q)] = v & MASK;
		carry = v >> BITS;
	}
	out[a.size() + size_t(q)] = carry;
	return itrim(out);
}

BigFloat::Limbs BigFloat::imul(const Limbs &a, const Limbs &b) {
	Limbs cols(a.size() + b.size() + 1, 0);
	for (size_t i = 0; i < a.size(); i++) {
		const int64_t ai = a[i];
		if (ai == 0) {
			continue;
		}
		int64_t carry = 0;
		for (size_t j = 0; j < b.size(); j++) {
			const int64_t v = cols[i + j] + ai * b[j] + carry;
			cols[i + j] = v & MASK;
			carry = v >> BITS;
		}
		size_t k = i + b.size();
		while (carry > 0) {
			const int64_t v2 = cols[k] + carry;
			cols[k] = v2 & MASK;
			carry = v2 >> BITS;
			k++;
		}
	}
	return itrim(cols);
}

int BigFloat::icmp(const Limbs &a, const Limbs &b) {
	const Limbs x = itrim(a);
	const Limbs y = itrim(b);
	if (x.size() != y.size()) {
		return x.size() > y.size() ? 1 : -1;
	}
	for (size_t k = x.size(); k-- > 0;) {
		if (x[k] != y[k]) {
			return x[k] > y[k] ? 1 : -1;
		}
	}
	return 0;
}

BigFloat::Limbs BigFloat::iadd(const Limbs &a, const Limbs &b) {
	const size_t n = std::max(a.size(), b.size());
	Limbs out(n + 1, 0);
	int64_t carry = 0;
	for (size_t i = 0; i < n; i++) {
		const int64_t v = (i < a.size() ? a[i] : 0) + (i < b.size() ? b[i] : 0) + carry;
		out[i] = v & MASK;
		carry = v >> BITS;
	}
	out[n] = carry;
	return itrim(out);
}

BigFloat::Limbs BigFloat::isub(const Limbs &a, const Limbs &b) {
	Limbs out(a.size(), 0);
	int64_t borrow = 0;
	for (size_t i = 0; i < a.size(); i++) {
		int64_t v = a[i] - (i < b.size() ? b[i] : 0) - borrow;
		if (v < 0) {
			v += BASE;
			borrow = 1;
		} else {
			borrow = 0;
		}
		out[i] = v;
	}
	return itrim(out);
}

std::string BigFloat::debug_string() const {
	std::string s = std::to_string(sign) + ":" + std::to_string(exp) + ":";
	for (size_t i = 0; i < limbs.size(); i++) {
		if (i) {
			s += ",";
		}
		s += std::to_string(limbs[i]);
	}
	return s;
}

} // namespace rc
