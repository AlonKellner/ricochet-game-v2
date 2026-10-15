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

// ---------------------------------------------------------------- B2

#include "goldens.hpp"

TEST_CASE("B2: the replay of a seeded float64 walk reproduces its tangent-linear gains, projectives included") {
	// The ladder re-estimates BigFloat trajectories with estimate::replay. On a
	// walk seeded from its first event (a physical direction trace: no anchored
	// prefix, whose gains the replay does not model) it must give the gains the
	// walk computed: both push the same derivatives through the same maps. Before
	// the B2 fix a projective hit got the surface's reflection / inversion map (15
	// of these 24 walks differed; 11 of them through a projective). Where a walk
	// knows a hit point exactly by provenance (endpoints, departure re-hits) its
	// derivative there is exact data while the replay re-derives the root: the
	// replay may only be larger there (conservative), never smaller.
	int walks = 0;
	int with_projective = 0;
	int mismatched = 0;
	int mismatched_projective = 0;
	for (const char *f : {"stage83_projective", "stage87_projective_plans", "stage87_click_forks", "stage81_rules"}) {
		const ojson fxs = test::load_json(std::string("bench/fixtures/") + f + ".json");
		for (const ojson &fx : fxs["fixtures"]) {
			if (fx["direction"].is_null() || fx["mode"].get<std::string>() != "physical") {
				continue;
			}
			const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
			Scene<Dual> scene = Scene<Dual>::from_defs(defs, Dual{1.0, 0.0});
			scene.emit_infinity = true;
			Solver<Dual> solver(scene, nullptr);
			const CN<Dual> P{Dual{fx["player"][0].get<double>(), 0.0}, Dual{fx["player"][1].get<double>(), 0.0}};
			const CN<Dual> D{Dual{fx["direction"][0].get<double>(), 0.0}, Dual{fx["direction"][1].get<double>(), 0.0}};
			const Walk<Dual> w = solver.run_direction(P, D, plan_from_json(fx["plan"]), std::nullopt, Mode::Physical, MAX_HITS, Flags());
			if (w.events.empty()) {
				continue;
			}
			const estimate::Sensitivity walked = estimate::collect(w.events, w.gains);
			const estimate::Sensitivity replayed = estimate::replay(w.events, scene);
			REQUIRE(walked.gain.size() == replayed.gain.size());
			bool proj = false;
			bool bad = false;
			for (size_t i = 0; i < w.events.size(); i++) {
				proj = proj || w.events[i].projective;
				const double a = walked.gain[i];
				const double b = replayed.gain[i];
				if (!((std::isnan(a) && std::isnan(b)) || std::fabs(a - b) <= 1e-6 * std::max(1.0, std::fabs(a)))) {
					bad = true;
					CHECK_MESSAGE(b >= a * (1.0 - 1e-9), fx["name"].get<std::string>() << " event " << i << ": the replay may not be smaller (walked " << a << ", replayed " << b << ")");
				}
			}
			mismatched += bad ? 1 : 0;
			mismatched_projective += (bad && proj) ? 1 : 0;
			with_projective += proj ? 1 : 0;
			walks++;
		}
	}
	MESSAGE("walks " << walks << " with a projective " << with_projective << " mismatched " << mismatched);
	CHECK(mismatched_projective == 0);
	CHECK(mismatched <= 4); // the provenance cases above
	CHECK(with_projective >= 10);
}
