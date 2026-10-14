class_name GdCN
extends RefCounted

# Complex number over a precision-generic scalar (GdF64Num or GdBigFloat).

var re
var im

static func make(p_re, p_im) -> GdCN:
	var z := GdCN.new()
	z.re = p_re
	z.im = p_im
	return z

func add(o: GdCN) -> GdCN:
	return GdCN.make(re.add(o.re), im.add(o.im))

func sub(o: GdCN) -> GdCN:
	return GdCN.make(re.sub(o.re), im.sub(o.im))

func neg() -> GdCN:
	return GdCN.make(re.neg(), im.neg())

func conj() -> GdCN:
	return GdCN.make(re, im.neg())

func mul(o: GdCN) -> GdCN:
	return GdCN.make(re.mul(o.re).sub(im.mul(o.im)), re.mul(o.im).add(im.mul(o.re)))

# multiply by a real scalar
func scale(s) -> GdCN:
	return GdCN.make(re.mul(s), im.mul(s))

# i * z
func mul_i() -> GdCN:
	return GdCN.make(im.neg(), re)

# -i * z
func mul_neg_i() -> GdCN:
	return GdCN.make(im, re.neg())

func abs2():
	return re.mul(re).add(im.mul(im))

func abs_val():
	return abs2().sqrt_val()

func div(o: GdCN) -> GdCN:
	var d = o.abs2()
	var n := mul(o.conj())
	return GdCN.make(n.re.div(d), n.im.div(d))

func div_real(s) -> GdCN:
	return GdCN.make(re.div(s), im.div(s))

func unit() -> GdCN:
	return div_real(abs_val())

func is_zero() -> bool:
	return re.is_zero() and im.is_zero()

static func dot(a: GdCN, b: GdCN):
	return a.re.mul(b.re).add(a.im.mul(b.im))

# a.x * b.y - a.y * b.x
static func cross(a: GdCN, b: GdCN):
	return a.re.mul(b.im).sub(a.im.mul(b.re))

func to_vector2() -> Vector2:
	return Vector2(re.to_float(), im.to_float())

func fx() -> float:
	return re.to_float()

func fy() -> float:
	return im.to_float()

func lift(ctx: GdNumCtx) -> GdCN:
	return GdCN.make(ctx.lift(re), ctx.lift(im))
