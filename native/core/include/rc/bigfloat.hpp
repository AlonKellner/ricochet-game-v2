// Arbitrary-precision binary floating point: a line-faithful port of
// scripts/math/precision/big_float.gd (Stage 90). Bit-identical limbs for
// every operation, the shipped semantics included:
//   value = sign * M * 2^exp, M = sum(limbs[i] * 2^(BITS*i)), limbs little-endian,
//   normalized so the top limb has bit (BITS-1) set; precision = BITS * limbs.size().
//   Rounding is toward zero (truncation); add keeps one guard limb; mul
//   accumulates carry-free column sums in wrapping int64 (B1: can wrap above
//   ~128 non-zero limbs per factor, exactly as GDScript's int wraps).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rc {

class BigFloat {
public:
	static constexpr int BITS = 28;
	static constexpr int64_t BASE = int64_t(1) << 28;
	static constexpr int64_t MASK = (int64_t(1) << 28) - 1;
	static constexpr int64_t TOP_BIT = int64_t(1) << 27;
	// +infinity (the projective parameter's t = inf): a reserved exponent
	static constexpr int64_t INF_EXP = int64_t(1) << 40;

	using Limbs = std::vector<int64_t>;

	int sign = 0;
	int64_t exp = 0;
	Limbs limbs;

	BigFloat() = default;

	static BigFloat zero(int n);
	static BigFloat infinity(int n);
	static int limbs_for_bits(int bits);
	static BigFloat from_float(double f, int n);
	static BigFloat from_string(const std::string &s, int n);
	static BigFloat from_int(int64_t v, int n);

	bool is_inf() const { return sign > 0 && exp >= INF_EXP; }
	// Values of this tier (the same limb count): 1, +infinity, a float64.
	BigFloat one_like() const { return from_int(1, int(limbs.size())); }
	BigFloat inf_like() const { return infinity(int(limbs.size())); }
	BigFloat like(double f) const { return from_float(f, int(limbs.size())); }
	int precision_limbs() const { return int(limbs.size()); }
	bool is_zero() const { return sign == 0; }
	bool is_negative() const { return sign < 0; }
	BigFloat neg() const;
	BigFloat abs_val() const;
	BigFloat mul_pow2(int64_t k) const;
	BigFloat with_limbs(int n) const;
	double to_float() const;
	// Exponent of the leading bit: value in [2^top_exp, 2^(top_exp+1))
	int64_t top_exp() const { return exp + BITS * int64_t(limbs.size()) - 1; }
	int cmp_abs(const BigFloat &o) const;
	int cmp(const BigFloat &o) const;
	BigFloat add(const BigFloat &o) const;
	BigFloat sub(const BigFloat &o) const;
	BigFloat mul(const BigFloat &o) const;
	BigFloat div(const BigFloat &o) const;
	BigFloat sqrt_val() const;

	// Debug form "sign:exp:limb,limb,..." (the goldens' encoding).
	std::string debug_string() const;

	// Exact non-negative integers as little-endian limb arrays (base 2^BITS):
	// the correction steps of div / sqrt_val (public for the tests).
	static Limbs itrim(const Limbs &a);
	static Limbs ishl(const Limbs &a, int64_t bits);
	static Limbs imul(const Limbs &a, const Limbs &b);
	static int icmp(const Limbs &a, const Limbs &b);
	static Limbs iadd(const Limbs &a, const Limbs &b);
	static Limbs isub(const Limbs &a, const Limbs &b); // a - b, a >= b

private:
	static Limbs place(const Limbs &src, int64_t src_exp, int64_t frame_exp, int len);
	static BigFloat normalize(const Limbs &arr, int64_t e, int s, int n);
};

} // namespace rc
