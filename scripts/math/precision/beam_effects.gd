class_name BeamEffects
extends RefCounted

# Beam maps of the transformative effects, precision-generic (any Num tier).
# The beam is the visual-space oriented generalized circle (HPBeam).
#   reflection in a line (direction D = E - S, u² = D²/|D|²):
#       h' = h, τ' = u²·conj(τ_h), κ' = −κ
#   reflection on a circle carrier = inversion in the carrier;
#   inversion: HPBeam.invert_at.
# Point maps (anchored chains): the same transforms on points, with null for
# the point at infinity.

# h: the hit point when known exactly by provenance (snapped endpoint,
# departure point); the new beam departs from it. null: beam.at(t).
static func apply(beam: HPBeam, t, surf: BeamScene.Surf, _etype: String, h: CN = null) -> HPBeam:
	if surf.portal:
		return portal(beam, t, surf, h)
	if surf.seg.kind == BeamGeom.Kind.LINE:
		return reflect_line(beam, t, surf.seg, h)
	return beam.invert_at(t, surf.seg.circ, h)

# Portal: the rigid image of the beam, departing the paired surface at T(h):
# p' = a h + d, tau' = a tau_h, kappa' = kappa.
static func portal(beam: HPBeam, t, surf: BeamScene.Surf, h_exact: CN = null) -> HPBeam:
	var h := beam.at(t) if h_exact == null else h_exact
	return HPBeam.make(surf.alpha.mul(h).add(surf.dvec), surf.alpha.mul(beam.tangent_at(t)), beam.kappa)

# Projective effect of the side `left` at h: a straight beam from h along
# +-axis (carrier normal / radius h - c / fixed normal). The sign is exact:
# "through" exits to the far side, sign(exit . grad) = sign(dir . grad);
# "back" returns to the approach side; an axis tangent to the carrier exits
# along +axis (through) / -axis (back).
static func project(beam: HPBeam, t, surf: BeamScene.Surf, left: bool, h_exact: CN = null) -> HPBeam:
	var h := beam.at(t) if h_exact == null else h_exact
	var etype := surf.effect(left)
	var pr: Array = surf.left_proj if left else surf.right_proj
	var g := BeamGeom._grad(surf.seg, h)
	var axis: CN
	if etype == "line_normal_projection":
		axis = g
	elif etype == "circle_normal_projection":
		axis = h.sub(surf.seg.circ.c)
	else:
		axis = CN.make(h.re.sub(h.re).add(_num_like(h.re, pr[1])), h.re.sub(h.re).add(_num_like(h.re, pr[2])))
	var ag = CN.dot(axis, g)
	var flip := false
	if not ag.is_zero():
		var dg = CN.dot(beam.tangent_at(t), g)
		flip = ag.is_negative() != dg.is_negative()
	if pr[0]:
		flip = not flip
	var ex := axis.neg() if flip else axis
	return HPBeam.make(h, ex.unit(), beam.kappa.sub(beam.kappa))

# The exit direction at h of a projective side entered from `left` (the
# exact rule of `project`, with the approach side given instead of a beam).
static func projective_exit(surf: BeamScene.Surf, left: bool, h: CN) -> CN:
	var etype := surf.effect(left)
	var pr: Array = surf.left_proj if left else surf.right_proj
	var g := BeamGeom._grad(surf.seg, h)
	var axis := _projective_axis(surf, etype, pr, h, g)
	var ag = CN.dot(axis, g)
	var flip := false
	if not ag.is_zero():
		# sign(dir . grad): approach = -dir . grad > 0 iff LEFT == (winding >= 0)
		var wind_nonneg: bool = not BeamGeom.winding(surf.seg).is_negative()
		var dg_negative: bool = left == wind_nonneg
		flip = ag.is_negative() != dg_negative
	if pr[0]:
		flip = not flip
	return axis.neg() if flip else axis

static func _projective_axis(surf: BeamScene.Surf, etype: String, pr: Array, h: CN, g: CN) -> CN:
	if etype == "line_normal_projection":
		return g
	if etype == "circle_normal_projection":
		return h.sub(surf.seg.circ.c)
	return CN.make(h.re.sub(h.re).add(_num_like(h.re, pr[1])), h.re.sub(h.re).add(_num_like(h.re, pr[2])))

# The hit point H on a projective side from which the projected beam passes
# through X (GAME_SPEC 13.3; oracle trace2.back_propagate), on the carrier:
# line normal -> the foot of X; circle normal -> c +- R u; directional -> the
# carrier crossings of the line through X along the normal. Of two
# candidates: the one whose exit ray reaches X directly, then the one nearer
# X. null if there is none. x_on_carrier (provenance: X is the hit point of a
# later projective entry on this surface, or the cursor exactly on its
# carrier): X itself is a candidate and is excluded.
# The hit point (oracle back_propagate): the candidate nearest the point the
# player clicked (click: Vector2 or null) -- by default the segment's midpoint
# (its via point). A circle projective has two candidates (both exit lines
# pass through X; one may reach X only through infinity): a fork, settled at
# planning time by the click.
static func back_propagate(surf: BeamScene.Surf, left: bool, X: CN, x_on_carrier: bool = false, click = null):
	var c := back_propagate_candidates(surf, left, X, x_on_carrier)
	if c.is_empty():
		return null
	var pool: Array = c.map(func(k): return k[0])
	if pool.size() == 1:
		return pool[0]
	var point: Vector2 = click if click != null else Vector2(surf.vx, surf.vy)
	var best = pool[0]
	var best_d := INF
	for H in pool:
		var d := Vector2(H.fx(), H.fy()).distance_squared_to(point)
		if d < best_d:
			best_d = d
			best = H
	return best

# The candidates [[H, direct]] (oracle back_propagate_candidates; direct: the
# exit ray from H reaches X directly).
static func back_propagate_candidates(surf: BeamScene.Surf, left: bool, X: CN, x_on_carrier: bool = false) -> Array:
	var etype := surf.effect(left)
	var pr: Array = surf.left_proj if left else surf.right_proj
	var seg: BeamGeom.Seg = surf.seg
	var zero = X.re.sub(X.re)
	var cands: Array = []
	if etype == "line_normal_projection":
		var D := seg.e.sub(seg.s)
		cands = [seg.s.add(D.scale(CN.dot(X.sub(seg.s), D).div(D.abs2())))]
	elif etype == "circle_normal_projection":
		var u := X.sub(seg.circ.c)
		if u.is_zero():
			return []
		cands = [seg.circ.c.add(u.unit().scale(seg.circ.r)), seg.circ.c.sub(u.unit().scale(seg.circ.r))]
	else:
		var n := CN.make(zero.add(_num_like(X.re, pr[1])), zero.add(_num_like(X.re, pr[2])))
		if seg.kind == BeamGeom.Kind.LINE:
			var D := seg.e.sub(seg.s)
			var den = CN.cross(D, n)
			if den.is_zero():
				return []
			cands = [X.sub(n.scale(CN.cross(D, X.sub(seg.s)).div(den)))]
		else:
			# |X - s n - c|^2 = R^2
			var dc := X.sub(seg.circ.c)
			var a = CN.dot(n, n)
			var b = CN.dot(n, dc).mul_pow2(1).neg()
			var c0 = CN.dot(dc, dc).sub(seg.circ.r.mul(seg.circ.r))
			var disc = b.mul(b).sub(a.mul(c0).mul_pow2(2))
			if disc.is_negative():
				return []
			var sq = disc.sqrt_val()
			var two_a = a.mul_pow2(1)
			cands = [X.sub(n.scale(b.neg().add(sq).div(two_a))), X.sub(n.scale(b.neg().sub(sq).div(two_a)))]
	if x_on_carrier and cands.size() == 2:
		# provenance: X is itself a candidate -- the beam reaches it from the other
		var d0: float = X.sub(cands[0]).abs2().to_float()
		var d1: float = X.sub(cands[1]).abs2().to_float()
		cands = [cands[0] if d0 > d1 else cands[1]]
	var out: Array = []
	for H in cands:
		var d = CN.dot(X.sub(H), projective_exit(surf, left, H))
		out.append([H, not d.is_negative() and not d.is_zero()])
	return out

static func _num_like(x, f: float):
	if x is F64Num:
		return F64Num.of(f)
	if x is DualNum:
		return DualNum.of(f)
	return BigFloat.from_float(f, x.limbs.size())

# T(z) of a portal surface (inverse: T^-1(z) = conj(a)(z - d)).
static func portal_point(z: CN, surf: BeamScene.Surf, inverse: bool = false) -> CN:
	if inverse:
		return surf.alpha.conj().mul(z.sub(surf.dvec))
	return surf.alpha.mul(z).add(surf.dvec)

static func u2(seg: BeamGeom.Seg) -> CN:
	var D := seg.e.sub(seg.s)
	return D.mul(D).div_real(D.abs2())

static func reflect_line(beam: HPBeam, t, seg: BeamGeom.Seg, h_exact: CN = null) -> HPBeam:
	var h := beam.at(t) if h_exact == null else h_exact
	var th := beam.tangent_at(t)
	var tau2 := u2(seg).mul(th.conj()).unit()
	return HPBeam.make(h, tau2, beam.kappa.neg())

# The surface's transform (or its inverse) applied to a point (null =
# infinity). Reflections and inversions are their own inverses.
static func map_point(z, surf: BeamScene.Surf, inverse: bool = false):
	if surf.portal:
		return null if z == null else portal_point(z, surf, inverse)
	var seg := surf.seg
	if seg.kind == BeamGeom.Kind.LINE:
		if z == null:
			return null
		return seg.s.add(u2(seg).mul(z.sub(seg.s).conj()))
	if z == null:
		return seg.circ.c
	if z.sub(seg.circ.c).is_zero():
		return null
	return HPBeam.invert_point(z, seg.circ)

# Pole chain step: [P, Q or null (infinity), delta = P - Q or null]. delta is
# tracked exactly (near-coincident P, Q lose nothing); P is re-derived as
# Q + delta so the triple stays consistent.
static func map_pole(parent: Array, surf: BeamScene.Surf) -> Array:
	var P0: CN = parent[0]
	var Q0 = parent[1]
	var d0 = parent[2]
	var seg := surf.seg
	if surf.portal:
		# rigid: infinity is fixed, differences rotate
		var Pp := portal_point(P0, surf)
		if Q0 == null:
			return [Pp, null, null]
		var Qp := portal_point(Q0, surf)
		var dp: CN = surf.alpha.mul(d0)
		return [Qp.add(dp), Qp, dp]
	if seg.kind == BeamGeom.Kind.LINE:
		var P1: CN = map_point(P0, surf)
		if Q0 == null:
			return [P1, null, null]
		var d1: CN = u2(seg).mul(d0.conj())
		var Q1: CN = map_point(Q0, surf)
		return [Q1.add(d1), Q1, d1]
	var circ := seg.circ
	if P0.sub(circ.c).is_zero():
		return [null, null, null]   # P at infinity (Stage 83)
	var P1c := HPBeam.invert_point(P0, circ)
	if Q0 == null:
		var u := P0.sub(circ.c)
		return [P1c, circ.c, u.scale(circ.r2).div_real(u.abs2())]
	var uq: CN = Q0.sub(circ.c)
	if uq.is_zero():
		return [P1c, null, null]
	var Q1c := HPBeam.invert_point(Q0, circ)
	# delta' = -R^2 conj(delta / ((P0 - c)(Q0 - c)))
	var d1c: CN = d0.div(P0.sub(circ.c).mul(uq)).conj().scale(circ.r2).neg()
	return [Q1c.add(d1c), Q1c, d1c]
