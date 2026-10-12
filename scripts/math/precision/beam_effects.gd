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
