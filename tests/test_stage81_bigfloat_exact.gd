extends GutTest

# Stage 81: BigFloat division and square root are correctly truncated, so a
# representable result is exact (as in IEEE float64). Provenance decisions
# (a beam built through an endpoint hits it exactly) must hold at every tier.
# Found while greening test_y_junction_blocks_both_sides at 112 bits:
# 80000 / 200 came out as 399.99..., sqrt(1) as 0.99....

const TIERS := [2, 4, 16]

func _b(x: float, n: int) -> Num:
	return BigFloat.from_float(x, n)

func test_exact_quotients():
	for n in TIERS:
		for c in [[80000.0, 200.0, 400.0], [1.0, 4.0, 0.25], [-3.0, 8.0, -0.375], [1e10, -1e5, -1e5],
				[7.0, 7.0, 1.0], [0.75, 0.25, 3.0], [400.0, 1.0, 400.0]]:
			var q := _b(c[0], n).div(_b(c[1], n))
			assert_eq(q.cmp(_b(c[2], n)), 0, "%d limbs: %s / %s = %s exactly (got %s)" % [n, c[0], c[1], c[2], q])

func test_exact_square_roots():
	for n in TIERS:
		for c in [[1.0, 1.0], [3600.0, 60.0], [2.25, 1.5], [0.0625, 0.25], [4.0, 2.0]]:
			var r := _b(c[0], n).sqrt_val()
			assert_eq(r.cmp(_b(c[1], n)), 0, "%d limbs: sqrt(%s) = %s exactly (got %s)" % [n, c[0], c[1], r])

func test_truncated_not_above():
	# inexact results are truncated toward zero: q·b <= a < (q + ulp)·b
	var rng := RandomNumberGenerator.new()
	rng.seed = 81
	for i in 200:
		var n: int = TIERS[i % TIERS.size()]
		var a := _b(rng.randf_range(0.001, 1000.0), n)
		var b := _b(rng.randf_range(0.001, 1000.0), n)
		var q := a.div(b)
		assert_true(q.with_limbs(n * 2).mul(b.with_limbs(n * 2)).cmp(a.with_limbs(n * 2)) <= 0, "case %d: q·b <= a" % i)
		var s := a.sqrt_val()
		assert_true(s.with_limbs(n * 2).mul(s.with_limbs(n * 2)).cmp(a.with_limbs(n * 2)) <= 0, "case %d: s² <= a" % i)
		assert_almost_eq(q.to_float(), a.to_float() / b.to_float(), 1e-12 * absf(q.to_float()))
		assert_almost_eq(s.to_float(), sqrt(a.to_float()), 1e-12 * s.to_float())
