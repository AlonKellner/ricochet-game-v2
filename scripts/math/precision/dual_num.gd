class_name DualNum
extends RefCounted

# Forward-mode dual number (value, derivative) in float64, with the same
# method API as F64Num/BigFloat. Running BeamSolver over DualNum propagates the
# sensitivity of every quantity to a seeded perturbation (tangent-linear
# model), which ErrorEstimate turns into an a-priori rounding-error estimate.

var v: float
var d: float

static func of(x: float, dx: float = 0.0) -> DualNum:
	var n := DualNum.new()
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

func neg() -> DualNum:
	return DualNum.of(-v, -d)

func abs_val() -> DualNum:
	return DualNum.of(-v, -d) if v < 0.0 else self

func add(o: DualNum) -> DualNum:
	return DualNum.of(v + o.v, d + o.d)

func sub(o: DualNum) -> DualNum:
	return DualNum.of(v - o.v, d - o.d)

func mul(o: DualNum) -> DualNum:
	return DualNum.of(v * o.v, d * o.v + v * o.d)

func div(o: DualNum) -> DualNum:
	var q := v / o.v
	return DualNum.of(q, (d - q * o.d) / o.v)

func sqrt_val() -> DualNum:
	var s := sqrt(v)
	return DualNum.of(s, d / (2.0 * s) if s != 0.0 else 0.0)

func mul_pow2(k: int) -> DualNum:
	var f := pow(2.0, k)
	return DualNum.of(v * f, d * f)

func cmp(o: DualNum) -> int:
	if v == o.v:
		return 0
	return 1 if v > o.v else -1

func _to_string() -> String:
	return "DualNum(%s, d=%s)" % [String.num(v, 17), String.num(d, 6)]
