class_name F64Num
extends RefCounted

# float64 scalar with the same method API as BigFloat, so precision-generic
# code (CN, HPBeam, AnchoredPlanner) runs unchanged at every precision tier.

var v: float

static func of(x: float) -> F64Num:
	var n := F64Num.new()
	n.v = x
	return n

func is_zero() -> bool:
	return v == 0.0

func is_negative() -> bool:
	return v < 0.0

func to_float() -> float:
	return v

func neg() -> F64Num:
	return F64Num.of(-v)

func abs_val() -> F64Num:
	return F64Num.of(absf(v))

func add(o: F64Num) -> F64Num:
	return F64Num.of(v + o.v)

func sub(o: F64Num) -> F64Num:
	return F64Num.of(v - o.v)

func mul(o: F64Num) -> F64Num:
	return F64Num.of(v * o.v)

func div(o: F64Num) -> F64Num:
	return F64Num.of(v / o.v)

func sqrt_val() -> F64Num:
	return F64Num.of(sqrt(v))

func mul_pow2(k: int) -> F64Num:
	return F64Num.of(v * pow(2.0, k))

func cmp(o: F64Num) -> int:
	if v == o.v:
		return 0
	return 1 if v > o.v else -1

func _to_string() -> String:
	return "F64Num(%s)" % String.num(v, 17)
