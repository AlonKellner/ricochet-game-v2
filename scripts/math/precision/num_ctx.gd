class_name GdNumCtx
extends RefCounted

# A precision tier. limbs == 0 -> float64 (GdF64Num); limbs == DUAL -> float64
# dual numbers (tangent-linear sensitivity); otherwise GdBigFloat with that many
# 28-bit limbs. All numbers produced by one context share a tier.

const DUAL := -1

var limbs: int = 0

static func f64() -> GdNumCtx:
	var c := GdNumCtx.new()
	c.limbs = 0
	return c

static func with_limbs(n: int) -> GdNumCtx:
	var c := GdNumCtx.new()
	c.limbs = n
	return c

static func dual() -> GdNumCtx:
	var c := GdNumCtx.new()
	c.limbs = DUAL
	return c

func is_f64() -> bool:
	return limbs == 0

func is_dual() -> bool:
	return limbs == DUAL

func bits() -> int:
	return 53 if limbs <= 0 else limbs * GdBigFloat.BITS

# Unit roundoff of this tier.
func eps() -> float:
	return pow(2.0, -bits())

func num(f: float):
	if limbs == 0:
		return GdF64Num.of(f)
	if limbs == DUAL:
		return GdDualNum.of(f)
	return GdBigFloat.from_float(f, limbs)

func from_string(s: String):
	if limbs <= 0:
		return num(s.to_float())
	return GdBigFloat.from_string(s, limbs)

func inf():
	if limbs <= 0:
		return num(INF)
	return GdBigFloat.infinity(limbs)

func zero():
	return num(0.0)

func one():
	return num(1.0)

# Lift a number from another tier (exactly when this tier is wider).
func lift(x):
	if x is GdF64Num or x is GdDualNum:
		return num(x.v)
	if limbs <= 0:
		return num(x.to_float())
	return x.with_limbs(limbs)

func cn(x: float, y: float) -> GdCN:
	return GdCN.make(num(x), num(y))

func cn_v(v: Vector2) -> GdCN:
	return GdCN.make(num(v.x), num(v.y))

func name() -> String:
	if limbs == DUAL:
		return "dual"
	return "f64" if limbs == 0 else "big%d" % bits()
