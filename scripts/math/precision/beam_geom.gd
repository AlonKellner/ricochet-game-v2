class_name GdBeamGeom
extends RefCounted

# Precision-generic segment geometry for the beam solver (any Num tier:
# GdF64Num, GdDualNum, GdBigFloat). Every decision here (on-segment, side, endpoint
# blockage) is computed at the tier's precision, following the definitions of
# the game's original definitions (the legacy intersection module, segment.gd):
#   - side: approach = -dir·∇f(P), winding = -(E - S) × ∇f(V);
#           LEFT iff (winding >= 0) == (approach > 0). Invariant under any
#           nonzero scaling of the carrier function f.
#   - on-segment: Re(CR(S, P; E, V)) >= 0 in homogeneous coordinates
#           (V = the point at infinity for full lines).
#   - endpoint blocked sides: sign of dir × (tangent into the segment).
# Line carriers: f(z) = cross(E - S, z - S), ∇f = (-(E-S).y, (E-S).x).
# Beam ∩ line, beam z(t) = p + tτ/(1 - iκt/2):
#   f(p)(1 + κ²t²/4) + (∇f·τ) t + (κ/2) cross(τ, ∇f) t² = 0
# Circle carriers reuse GdHPBeam.hits.

enum Kind { LINE, CIRCLE }

class Seg extends RefCounted:
	var id: int
	var kind: int = Kind.LINE
	var full: bool = false
	var s: GdCN
	var e: GdCN
	var v = null                        # GdCN, or null = the point at infinity
	var circ: GdHPBeam.Circle = null      # circle carriers: center c, radius r, r²
	var _wind_nonneg = null             # cached sign of winding(seg) (constant)

	func c() -> GdCN:
		return circ.c

# ---------------------------------------------------------------- construction

static func _cn(ctx: GdNumCtx, a) -> GdCN:
	return ctx.cn(float(a[0]), float(a[1]))

# {kind: line|arc|full_circle, id, start, end, via, center, radius, full}
static func seg_from_dict(ctx: GdNumCtx, d: Dictionary) -> Seg:
	var sg := Seg.new()
	sg.id = int(d.get("id", 0))
	sg.s = _cn(ctx, d["start"])
	sg.e = _cn(ctx, d["end"])
	sg.v = null if d.get("via") == null else _cn(ctx, d["via"])
	sg.full = bool(d.get("full", false))
	match String(d["kind"]):
		"line":
			sg.kind = Kind.LINE
		"arc":
			sg.kind = Kind.CIRCLE
			var c := circumcenter(sg.s, sg.v, sg.e)
			sg.circ = GdHPBeam.Circle.make(sg.id, c, sg.s.sub(c).abs_val())
		"full_circle":
			sg.kind = Kind.CIRCLE
			sg.full = true
			sg.circ = GdHPBeam.Circle.make(sg.id, _cn(ctx, d["center"]), ctx.num(float(d["radius"])))
	return sg

# From a game Segment. Line vs circle by construction provenance (the
# segment's carrier); full circles use the carrier's exact center/radius,
# arcs the circle through their three exact points.
static func seg_from_segment(ctx: GdNumCtx, segment: Segment, sid: int) -> Seg:
	var carrier := segment.get_carrier()
	var via = segment.via.coords
	var d := {"id": sid, "start": [segment.start.coords.x, segment.start.coords.y],
		"end": [segment.end.coords.x, segment.end.coords.y],
		"via": null if (is_inf(via.x) or is_inf(via.y)) else [via.x, via.y], "full": segment.full}
	if carrier.is_line():
		d["kind"] = "line"
	elif segment.full:
		d["kind"] = "full_circle"
		d["center"] = [-carrier.b / (2.0 * carrier.a), -carrier.c / (2.0 * carrier.a)]
		d["radius"] = carrier.radius()
	else:
		d["kind"] = "arc"
	return seg_from_dict(ctx, d)

# Center of the circle through A, B, C (at the tier's precision).
static func circumcenter(A: GdCN, B: GdCN, C: GdCN) -> GdCN:
	var ab := B.sub(A)
	var ac := C.sub(A)
	var d = GdCN.cross(ab, ac).mul_pow2(1)
	var ab2 = ab.abs2()
	var ac2 = ac.abs2()
	# A + (|ab|² J(ac) ... ) / d with J the +90° rotation: standard closed form
	var ux = ac.im.mul(ab2).sub(ab.im.mul(ac2)).div(d)
	var uy = ab.re.mul(ac2).sub(ac.re.mul(ab2)).div(d)
	return A.add(GdCN.make(ux, uy))

# ---------------------------------------------------------------- hits

static func _line_coeffs(beam: GdHPBeam, seg: Seg) -> Array:
	var D := seg.e.sub(seg.s)
	var g := GdCN.make(D.im.neg(), D.re)
	var fp = GdCN.cross(D, beam.p.sub(seg.s))
	var k = beam.kappa
	var c1 = GdCN.dot(g, beam.tau)
	var c2 = fp.mul(k.mul(k)).mul_pow2(-2).add(k.mul_pow2(-1).mul(GdCN.cross(beam.tau, g)))
	return [fp, c1, c2]

# Real roots t of beam ∩ seg's carrier. depart_on: beam.p lies exactly on it
# (provenance), so the root t = 0 is the departure and is not returned.
static func hits(beam: GdHPBeam, seg: Seg, depart_on: bool) -> Array:
	if seg.kind == Kind.CIRCLE:
		return beam.hits(seg.circ, depart_on)
	var co := _line_coeffs(beam, seg)
	var c0 = co[0]
	var c1 = co[1]
	var c2 = co[2]
	# degree drop (c2 = 0): a curved beam meets the line at its antipode, t = inf
	var at_inf: Array = [] if beam.kappa.is_zero() else [GdHPBeam.inf_like(beam.kappa)]
	if depart_on:
		if c2.is_zero():
			return at_inf
		return [c1.neg().div(c2)]
	if c2.is_zero():
		if c1.is_zero():
			return at_inf
		return [c0.neg().div(c1)] + at_inf
	var disc = c1.mul(c1).sub(c2.mul(c0).mul_pow2(2))
	if disc.is_negative():
		return []
	var s = disc.sqrt_val()
	var big = c1.add(s).neg() if not c1.is_negative() else c1.sub(s).neg()
	if big.is_zero():
		return [c0.sub(c0)]
	return [big.div(c2.mul_pow2(1)), c0.mul_pow2(1).div(big)]

# ---------------------------------------------------------------- decisions

static func _hdet(zA: GdCN, wA: int, zB: GdCN, wB: int) -> GdCN:
	# zA·wB - zB·wA with integer homogeneous weights (0 or 1)
	if wA == 1 and wB == 1:
		return zA.sub(zB)
	if wB == 1:
		return zA            # wA == 0
	return zB.neg()          # wB == 0, wA == 1

# Re(cross ratio); on-segment iff >= 0.
static func on_segment_value(seg: Seg, p: GdCN):
	var zv: GdCN
	var wv: int
	if seg.v == null:
		zv = GdCN.make(p.re.sub(p.re).add(_one(p.re)), p.re.sub(p.re))
		wv = 0
	else:
		zv = seg.v
		wv = 1
	var sv := _hdet(seg.s, 1, zv, wv)
	var ep := _hdet(seg.e, 1, p, 1)
	var sp := _hdet(seg.s, 1, p, 1)
	var ev := _hdet(seg.e, 1, zv, wv)
	var num := sv.mul(ep)
	var den := sp.mul(ev)
	return num.re.mul(den.re).add(num.im.mul(den.im))

static func on_segment(seg: Seg, p: GdCN) -> bool:
	if seg.full:
		return true
	return not on_segment_value(seg, p).is_negative()

static func _eq(a: GdCN, b: GdCN) -> bool:
	return a.re.cmp(b.re) == 0 and a.im.cmp(b.im) == 0

# 1 / 2 if p is exactly the start / end endpoint (never for full segments).
static func endpoint_at(seg: Seg, p: GdCN) -> int:
	if seg.full:
		return 0
	if _eq(p, seg.s):
		return 1
	if _eq(p, seg.e):
		return 2
	return 0

static func _grad(seg: Seg, p: GdCN) -> GdCN:
	if seg.kind == Kind.LINE:
		var D := seg.e.sub(seg.s)
		return GdCN.make(D.im.neg(), D.re)
	return p.sub(seg.circ.c).scale(_one(p.re).mul_pow2(1))

static func winding(seg: Seg):
	var g: GdCN
	if seg.kind == Kind.LINE:
		g = _grad(seg, seg.s)
	else:
		g = _grad(seg, seg.v)
	return GdCN.cross(seg.e.sub(seg.s), g).neg()

# Side the beam (travel direction dir at p) comes from: true = LEFT.
static func side_left(seg: Seg, p: GdCN, dir: GdCN) -> bool:
	var approach = GdCN.dot(dir, _grad(seg, p)).neg()
	var approach_pos: bool = not approach.is_negative() and not approach.is_zero()
	if seg._wind_nonneg == null:
		seg._wind_nonneg = not winding(seg).is_negative()
	if seg._wind_nonneg:
		return approach_pos
	return not approach_pos

static func tangent_into(seg: Seg, which: int) -> GdCN:
	var ep: GdCN = seg.s if which == 1 else seg.e
	if seg.kind == Kind.LINE:
		return seg.e.sub(seg.s) if which == 1 else seg.s.sub(seg.e)
	var rad := ep.sub(seg.circ.c)
	var ccw := GdCN.make(rad.im.neg(), rad.re)
	var cw := GdCN.make(rad.im, rad.re.neg())
	var ref: GdCN = seg.v.sub(ep)
	return ccw if GdCN.dot(ccw, ref).cmp(GdCN.dot(cw, ref)) >= 0 else cw

# [blocked_left, blocked_right] for a beam through endpoint `which`.
static func blocked_sides(seg: Seg, which: int, dir: GdCN) -> Array:
	var c = GdCN.cross(dir, tangent_into(seg, which))
	if c.is_zero():
		return [true, true]
	if not c.is_negative():
		return [false, true]
	return [true, false]

# ---------------------------------------------------------------- records

# Hit records with provenance. `known` = [[point GdCN, t], ...]: exact points the
# beam is constructed through (e.g. an anchor or the cursor). A known point
# that is exactly an endpoint of `seg` lies on both the beam and the carrier,
# so it IS one of the roots: that root (the nearer one) snaps to it exactly.
# Record: {t, point, on_segment, at_endpoint}.
static func hit_records(beam: GdHPBeam, seg: Seg, depart_on: bool, known: Array = []) -> Array:
	var recs: Array = []
	for t in hits(beam, seg, depart_on):
		var pt := beam.at(t)
		recs.append({"t": t, "point": pt, "at_endpoint": endpoint_at(seg, pt)})
	for kp in known:
		var which := endpoint_at(seg, kp[0])
		if which == 0 or recs.is_empty():
			continue
		var best := 0
		var best_d := INF
		for i in recs.size():
			var d: float = recs[i]["point"].sub(kp[0]).abs2().to_float()
			if d < best_d:
				best_d = d
				best = i
		recs[best]["t"] = kp[1]
		recs[best]["point"] = kp[0]
		recs[best]["at_endpoint"] = which
	for r in recs:
		r["on_segment"] = on_segment(seg, r["point"])
	return recs

static func _one(x):
	if x is GdF64Num:
		return GdF64Num.of(1.0)
	if x is GdDualNum:
		return GdDualNum.of(1.0)
	return GdBigFloat.from_int(1, x.limbs.size())
