class_name NumCtx
extends RefCounted

# A precision tier. limbs == 0 -> float64 (F64Num); limbs == DUAL -> float64
# dual numbers (tangent-linear sensitivity); otherwise BigFloat with that many
# 28-bit limbs. All numbers produced by one context share a tier.

const DUAL := -1

var limbs: int = 0

static func f64() -> NumCtx:
	var c := NumCtx.new()
	c.limbs = 0
	return c

static func with_limbs(n: int) -> NumCtx:
	var c := NumCtx.new()
	c.limbs = n
	return c

static func dual() -> NumCtx:
	var c := NumCtx.new()
	c.limbs = DUAL
	return c

func is_f64() -> bool:
	return limbs == 0

func is_dual() -> bool:
	return limbs == DUAL

func bits() -> int:
	return 53 if limbs <= 0 else limbs * BigFloat.BITS

# Unit roundoff of this tier.
func eps() -> float:
	return pow(2.0, -bits())

func num(f: float):
	if limbs == 0:
		return F64Num.of(f)
	if limbs == DUAL:
		return DualNum.of(f)
	return BigFloat.from_float(f, limbs)

func from_string(s: String):
	if limbs <= 0:
		return num(s.to_float())
	return BigFloat.from_string(s, limbs)

func zero():
	return num(0.0)

func one():
	return num(1.0)

# Lift a number from another tier (exactly when this tier is wider).
func lift(x):
	if x is F64Num or x is DualNum:
		return num(x.v)
	if limbs <= 0:
		return num(x.to_float())
	return x.with_limbs(limbs)

func cn(x: float, y: float) -> CN:
	return CN.make(num(x), num(y))

func cn_v(v: Vector2) -> CN:
	return CN.make(num(v.x), num(v.y))

func name() -> String:
	if limbs == DUAL:
		return "dual"
	return "f64" if limbs == 0 else "big%d" % bits()
