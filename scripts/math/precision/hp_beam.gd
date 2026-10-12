class_name HPBeam
extends RefCounted

# Oriented generalized circle in visual space, precision-generic.
#   point p, unit tangent tau, signed curvature kappa (w.r.t. normal i*tau).
# Local projective parametrization (same wrap order as Tracer's ray t):
#   z(t) = p + t*tau / (1 - i*kappa*t/2)
# t = 0 is p; t -> +inf continues forward, wraps to -inf, returns to 0-.
# The parameter is projective: t = inf (exact, Num.is_inf) is the antipode of
# p on a circle beam, where a root of degree-dropping equations lies.
# Inversion of the beam at a point h on circle (c, R):
#   tau'   = -conj(tau_h) * ((h - c)/R)^2
#   kappa' = -kappa - 2 cross(tau_h, h - c) / R^2
# (derivation: docs/precision_benchmark_findings.md)

class Circle extends RefCounted:
	var id: int
	var c: CN
	var r
	var r2

	static func make(p_id: int, p_c: CN, p_r) -> Circle:
		var o := Circle.new()
		o.id = p_id
		o.c = p_c
		o.r = p_r
		o.r2 = p_r.mul(p_r)
		return o

var p: CN
var tau: CN
var kappa

static func make(p_p: CN, p_tau: CN, p_kappa) -> HPBeam:
	var b := HPBeam.new()
	b.p = p_p
	b.tau = p_tau
	b.kappa = p_kappa
	return b

# 1 + i*kappa*t/2
func _f(t) -> CN:
	return CN.make(_one_like(t), kappa.mul(t).mul_pow2(-1))

# +infinity of t's tier (the antipode's parameter)
static func inf_like(x):
	if x is F64Num:
		return F64Num.of(INF)
	if x is DualNum:
		return DualNum.of(INF)
	return BigFloat.infinity(x.limbs.size())

func _one_like(t):
	if t is F64Num:
		return F64Num.of(1.0)
	if t is DualNum:
		return DualNum.of(1.0)
	return BigFloat.from_int(1, t.limbs.size())

func at(t) -> CN:
	if t.is_inf():
		return at_infinity()
	# t*tau / (1 - i k t/2) = t*tau*(1 + i k t/2) / (1 + k^2 t^2 / 4)
	var f := _f(t)
	var den = f.abs2()
	return p.add(tau.mul(f).scale(t).div_real(den))

# Point at t = infinity (antipode of p); null for a line.
func at_infinity():
	if kappa.is_zero():
		return null
	# p + 2 i tau / kappa
	return p.add(tau.mul_i().scale(_one_like(kappa).mul_pow2(1)).div_real(kappa))

func tangent_at(t) -> CN:
	if t.is_inf():
		return tau.neg()
	var f := _f(t)
	return tau.mul(f).mul(f).unit()

# t = Re(w / den), w = z - p, den = tau + i kappa w / 2 (real for z on the
# beam; for a point off a straight beam: its orthogonal projection). Near the
# antipode of a circle beam den -> 0: there t = 1 / Re(den / w), which stays
# well-conditioned -- the same exact algebra for points on the beam (circle
# beams are only asked about such points); the branch only picks the stable
# form.
func param_of(z: CN):
	var w := z.sub(p)
	var den := tau.add(w.mul_i().scale(kappa).scale(_one_like(kappa).mul_pow2(-1)))
	if den.is_zero():
		return inf_like(kappa)      # z is the antipode
	if not kappa.is_zero() and w.abs2().cmp(den.abs2()) > 0:
		var qre = den.div(w).re
		if qre.is_zero():
			return inf_like(kappa)
		return _one_like(kappa).div(qre)
	return w.div(den).re

# Real roots of beam ∩ circle. depart_on: p lies exactly on the circle
# (provenance) and the t = 0 root is dropped.
func hits(circ: Circle, depart_on: bool) -> Array:
	var q := circ.c.sub(p)
	var tq = CN.dot(q, tau)
	var k = kappa
	var one = _one_like(k)
	var roots: Array = []
	if depart_on:
		var a2_d = one.sub(k.mul(CN.cross(tau, q)))
		if not a2_d.is_zero():
			roots.append(tq.mul_pow2(1).div(a2_d))
		elif not k.is_zero():
			roots.append(inf_like(k))   # the other crossing is the antipode
		return roots
	# power of p w.r.t. the circle, without a square root: exact whenever the
	# inputs are (e.g. p exactly on the circle gives exactly 0 at every tier)
	var P = q.abs2().sub(circ.r2)
	var a2 = one.sub(k.mul(CN.cross(tau, q))).add(k.mul(k).mul(P).mul_pow2(-2))
	var disc = tq.mul(tq).sub(a2.mul(P))
	if disc.is_negative():
		return roots
	var s = disc.sqrt_val()
	var big = tq.add(s) if not tq.is_negative() else tq.sub(s)
	if not big.is_zero():
		roots.append(P.div(big))
		if not a2.is_zero():
			roots.append(big.div(a2))
		elif not k.is_zero():
			roots.append(inf_like(k))   # the other crossing is the antipode
	elif not a2.is_zero():
		roots.append(k.sub(k))
	return roots

func is_entering(t, circ: Circle) -> bool:
	var h := at(t)
	var d := tangent_at(t)
	return CN.dot(d, h.sub(circ.c)).is_negative()

# h_exact: the hit point when known exactly by provenance (else at(t)).
func invert_at(t, circ: Circle, h_exact: CN = null) -> HPBeam:
	var h := at(t) if h_exact == null else h_exact
	var tau_h := tangent_at(t)
	var u := h.sub(circ.c)
	var nu := u.div_real(circ.r)
	var tau2 := tau_h.conj().mul(nu).mul(nu).neg().unit()
	var k2 = kappa.neg().sub(CN.cross(tau_h, u).mul_pow2(1).div(circ.r2))
	return HPBeam.make(h, tau2, k2)

# Beam starting at its own point h = at(t), same circle and orientation.
func rebased_at(t) -> HPBeam:
	return HPBeam.make(at(t), tangent_at(t), kappa)

# Oriented circle through P -> X -> Q (Q = P - delta), anchored at P.
# delta = P - Q is passed exactly so near-coincident P, Q lose nothing.
static func through(P: CN, X: CN, delta) -> HPBeam:
	var a := X.sub(P)
	if delta == null:
		return HPBeam.make(P, a.unit(), a.re.sub(a.re))
	var b: CN = delta.neg()
	# g = |a|^2 J(b) - |b|^2 J(a), J = rotate by -90 deg; tau = -i * unit(g)
	var g := b.mul_neg_i().scale(a.abs2()).sub(a.mul_neg_i().scale(b.abs2()))
	var tau_p := g.mul_neg_i().unit()
	var ba := b.sub(a)
	var kap = CN.cross(a, b).mul_pow2(1).div(a.abs_val().mul(b.abs_val()).mul(ba.abs_val()))
	return HPBeam.make(P, tau_p, kap)

static func invert_point(z: CN, circ: Circle) -> CN:
	var u := z.sub(circ.c)
	# c + R^2 / conj(u) = c + R^2 * u / |u|^2
	return circ.c.add(u.scale(circ.r2).div_real(u.abs2()))
