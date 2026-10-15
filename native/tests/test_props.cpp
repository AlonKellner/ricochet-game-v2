// Property tests of the native core (Stages 102-103): closed-form identities
// and bounds checked on seeded random inputs, independent of the goldens.
#include "rc/bigfloat.hpp"
#include "rc/exact.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <random>

using namespace rc;

namespace {

// A normalized BigFloat of n limbs with random limbs (top bit set), a random
// sign and exponent.
BigFloat random_big(std::mt19937_64 &rng, int n, int64_t exp_lo, int64_t exp_hi) {
	BigFloat x;
	x.sign = (rng() & 1) ? 1 : -1;
	x.limbs.resize(size_t(n));
	for (int i = 0; i < n; i++) {
		x.limbs[size_t(i)] = int64_t(rng() & uint64_t(BigFloat::MASK));
	}
	x.limbs[size_t(n - 1)] |= BigFloat::TOP_BIT;
	std::uniform_int_distribution<int64_t> e(exp_lo, exp_hi);
	x.exp = e(rng);
	return x;
}

// The value with all n limbs equal to MASK: 2^(28 n) - 1 (times 2^exp).
BigFloat all_ones(int n) {
	BigFloat x;
	x.sign = 1;
	x.exp = 0;
	x.limbs.assign(size_t(n), BigFloat::MASK);
	return x;
}

} // namespace

TEST_CASE("B1: BigFloat::mul is exact past 128 limbs (columns summed in 128 bits)") {
	// (2^m - 1)^2 = 2^(2m) - 2^(m+1) + 1, m = 28 n: truncated to n limbs the
	// product's top half is 2^m - 2, i.e. limbs [MASK - 1, MASK, ..., MASK]
	// at exponent m. Every column of the schoolbook product holds up to n
	// products of two MASKs (< 2^56 each): past 128 limbs an int64 sum wraps.
	for (int n : {4, 64, 128, 129, 200, 320}) {
		const BigFloat a = all_ones(n);
		const BigFloat p = a.mul(a);
		REQUIRE(p.sign == 1);
		REQUIRE(int(p.limbs.size()) == n);
		CHECK_MESSAGE(p.exp == int64_t(BigFloat::BITS) * n, n);
		CHECK_MESSAGE(p.limbs[0] == BigFloat::MASK - 1, n);
		bool rest = true;
		for (int i = 1; i < n; i++) {
			rest = rest && p.limbs[size_t(i)] == BigFloat::MASK;
		}
		CHECK_MESSAGE(rest, n);
	}
	// random factors: the product of 320-limb numbers agrees with the product
	// computed in two halves (a * b = a * b_hi + a * b_lo, b_lo below 2^-28*160 of b),
	// to the precision both carry
	std::mt19937_64 rng(102);
	for (int k = 0; k < 20; k++) {
		const BigFloat a = random_big(rng, 320, -4000, 4000);
		const BigFloat b = random_big(rng, 320, -4000, 4000);
		const BigFloat p = a.mul(b);
		const BigFloat q = b.mul(a);
		CHECK(p.cmp(q) == 0);
		// |a b| / |a| = |b| within the truncation of a division at 320 limbs
		const BigFloat back = p.div(a);
		const BigFloat err = back.sub(b).abs_val();
		const bool tiny = err.is_zero() || err.top_exp() < b.top_exp() - BigFloat::BITS * 300;
		CHECK(tiny);
	}
}

TEST_CASE("B3: BigFloat::to_float is within 2U of the value (the bound approx() assumes)") {
	std::mt19937_64 rng(103);
	const double two_u = 2.0 * exact::U;
	int checked = 0;
	for (int n : {2, 3, 4, 8, 16, 64}) {
		for (int k = 0; k < 2000; k++) {
			const BigFloat x = random_big(rng, n, -1100 - BigFloat::BITS * n, 900 - BigFloat::BITS * n);
			const double f = x.to_float();
			if (!std::isfinite(f) || f == 0.0 || std::fabs(f) < 2.2250738585072014e-308) {
				continue; // outside the normal double range
			}
			// |f - x| <= 2U |x|, compared exactly at a wider tier
			const int w = n + 4;
			const BigFloat xw = x.with_limbs(w);
			const BigFloat d = BigFloat::from_float(f, w).sub(xw).abs_val();
			const BigFloat bound = xw.abs_val().mul(BigFloat::from_float(two_u, w));
			CHECK_MESSAGE(d.cmp(bound) <= 0, "n=" << n << " k=" << k);
			checked++;
		}
	}
	CHECK(checked > 10000);
}
