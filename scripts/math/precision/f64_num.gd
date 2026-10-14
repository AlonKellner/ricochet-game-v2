class_name GdF64Num
extends RefCounted

# float64 scalar with the same method API as GdBigFloat, so precision-generic
# code (GdCN, GdHPBeam, AnchoredPlanner) runs unchanged at every precision tier.

var v: float

static func of(x: float) -> GdF64Num:
	var n := GdF64Num.new()
	n.v = x
	return n

# The projective parameter's t = +infinity (exact, never a rounding result
# other than IEEE overflow).
func is_inf() -> bool:
	return v == INF

func is_zero() -> bool:
	return v == 0.0

func is_negative() -> bool:
	return v < 0.0

func to_float() -> float:
	return v

func neg() -> GdF64Num:
	return GdF64Num.of(-v)

func abs_val() -> GdF64Num:
	return GdF64Num.of(absf(v))

func add(o: GdF64Num) -> GdF64Num:
	return GdF64Num.of(v + o.v)

func sub(o: GdF64Num) -> GdF64Num:
	return GdF64Num.of(v - o.v)

func mul(o: GdF64Num) -> GdF64Num:
	return GdF64Num.of(v * o.v)

func div(o: GdF64Num) -> GdF64Num:
	return GdF64Num.of(v / o.v)

func sqrt_val() -> GdF64Num:
	return GdF64Num.of(sqrt(v))

func mul_pow2(k: int) -> GdF64Num:
	return GdF64Num.of(v * pow(2.0, k))

func cmp(o: GdF64Num) -> int:
	if v == o.v:
		return 0
	return 1 if v > o.v else -1

func _to_string() -> String:
	return "GdF64Num(%s)" % String.num(v, 17)
