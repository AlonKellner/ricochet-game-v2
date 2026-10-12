class_name CN
extends RefCounted

# Complex number over a precision-generic scalar (F64Num or BigFloat).

var re
var im

static func make(p_re, p_im) -> CN:
	var z := CN.new()
	z.re = p_re
	z.im = p_im
	return z

func add(o: CN) -> CN:
	return CN.make(re.add(o.re), im.add(o.im))

func sub(o: CN) -> CN:
	return CN.make(re.sub(o.re), im.sub(o.im))

func neg() -> CN:
	return CN.make(re.neg(), im.neg())

func conj() -> CN:
	return CN.make(re, im.neg())

func mul(o: CN) -> CN:
	return CN.make(re.mul(o.re).sub(im.mul(o.im)), re.mul(o.im).add(im.mul(o.re)))

# multiply by a real scalar
func scale(s) -> CN:
	return CN.make(re.mul(s), im.mul(s))

# i * z
func mul_i() -> CN:
	return CN.make(im.neg(), re)

# -i * z
func mul_neg_i() -> CN:
	return CN.make(im, re.neg())

func abs2():
	return re.mul(re).add(im.mul(im))

func abs_val():
	return abs2().sqrt_val()

func div(o: CN) -> CN:
	var d = o.abs2()
	var n := mul(o.conj())
	return CN.make(n.re.div(d), n.im.div(d))

func div_real(s) -> CN:
	return CN.make(re.div(s), im.div(s))

func unit() -> CN:
	return div_real(abs_val())

func is_zero() -> bool:
	return re.is_zero() and im.is_zero()

static func dot(a: CN, b: CN):
	return a.re.mul(b.re).add(a.im.mul(b.im))

# a.x * b.y - a.y * b.x
static func cross(a: CN, b: CN):
	return a.re.mul(b.im).sub(a.im.mul(b.re))

func to_vector2() -> Vector2:
	return Vector2(re.to_float(), im.to_float())

func fx() -> float:
	return re.to_float()

func fy() -> float:
	return im.to_float()

func lift(ctx: NumCtx) -> CN:
	return CN.make(ctx.lift(re), ctx.lift(im))
