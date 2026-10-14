class_name GdDualNum
extends RefCounted

# Forward-mode dual number (value, derivative) in float64, with the same
# method API as GdF64Num/GdBigFloat. Running GdBeamSolver over GdDualNum propagates the
# sensitivity of every quantity to a seeded perturbation (tangent-linear
# model), which GdErrorEstimate turns into an a-priori rounding-error estimate.

var v: float
var d: float

static func of(x: float, dx: float = 0.0) -> GdDualNum:
	var n := GdDualNum.new()
	n.v = x
	n.d = dx
	return n

func is_inf() -> bool:
	return v == INF

func is_zero() -> bool:
	return v == 0.0

func is_negative() -> bool:
	return v < 0.0

func to_float() -> float:
	return v

func neg() -> GdDualNum:
	return GdDualNum.of(-v, -d)

func abs_val() -> GdDualNum:
	return GdDualNum.of(-v, -d) if v < 0.0 else self

func add(o: GdDualNum) -> GdDualNum:
	return GdDualNum.of(v + o.v, d + o.d)

func sub(o: GdDualNum) -> GdDualNum:
	return GdDualNum.of(v - o.v, d - o.d)

func mul(o: GdDualNum) -> GdDualNum:
	return GdDualNum.of(v * o.v, d * o.v + v * o.d)

func div(o: GdDualNum) -> GdDualNum:
	var q := v / o.v
	return GdDualNum.of(q, (d - q * o.d) / o.v)

func sqrt_val() -> GdDualNum:
	var s := sqrt(v)
	return GdDualNum.of(s, d / (2.0 * s) if s != 0.0 else 0.0)

func mul_pow2(k: int) -> GdDualNum:
	var f := pow(2.0, k)
	return GdDualNum.of(v * f, d * f)

func cmp(o: GdDualNum) -> int:
	if v == o.v:
		return 0
	return 1 if v > o.v else -1

func _to_string() -> String:
	return "GdDualNum(%s, d=%s)" % [String.num(v, 17), String.num(d, 6)]
