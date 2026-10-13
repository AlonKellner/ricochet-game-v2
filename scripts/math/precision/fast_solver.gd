class_name FastSolver
extends RefCounted

# Raw-float backend of BeamSolver for the float64 tier, ~50x faster than the
# precision-generic code (which wraps every scalar in an object). The beam
# math performs the same operations in the same order as HPBeam / BeamGeom /
# BeamEffects, so float64 values are bit-identical to BeamSolver over F64Num,
# and it carries the tangent-linear derivative (DualNum semantics) inline for
# ErrorEstimate. The trace rules are BeamSolver's walker (one implementation);
# topology decisions run the shared BeamGeom code on the (bit-identical)
# float64 points. BigFloat tiers use BeamSolver.NumBackend.
#
# Beam: PackedFloat64Array [px, py, tx, ty, k, dpx, dpy, dtx, dty, dk]
# Parameters t on the walker side: DualNum (value, derivative).

const PX := 0
const PY := 1
const TX := 2
const TY := 3
const K := 4
const DPX := 5
const DPY := 6
const DTX := 7
const DTY := 8
const DK := 9

# A BeamScene at float64 (its raw geometry feeds the raw math).
class Scene extends RefCounted:
	var scene: BeamScene

	static func from_defs(defs: Array) -> Scene:
		var s := Scene.new()
		s.scene = BeamScene.from_defs(NumCtx.f64(), defs)
		return s

class Out extends RefCounted:
	var events: Array = []    # BeamSolver.Event (F64Num points / beams)
	var gain: Array = []      # per event |d point / d seed angle| (0 = unseeded)

# ---------------------------------------------------------------- beam math

static func beam(px: float, py: float, tx: float, ty: float, k: float) -> PackedFloat64Array:
	return PackedFloat64Array([px, py, tx, ty, k, 0.0, 0.0, 0.0, 0.0, 0.0])

static func seeded(b: PackedFloat64Array) -> PackedFloat64Array:
	return PackedFloat64Array([b[PX], b[PY], b[TX], b[TY], b[K], 0.0, 0.0, -b[TY], b[TX], 0.0])

# z(t) and dz: returns [x, y, dx, dy]
static func at(b: PackedFloat64Array, t: float, dt: float) -> PackedFloat64Array:
	if t == INF:
		return antipode(b)
	var k := b[K]
	var dk := b[DK]
	var tx := b[TX]
	var ty := b[TY]
	var dtx := b[DTX]
	var dty := b[DTY]
	var kt := k * t
	var dkt := dk * t + k * dt
	var u := kt * 0.5
	var du := dkt * 0.5
	var re1 := tx * 1.0 - ty * u
	var dre1 := (dtx * 1.0) - (dty * u + ty * du)
	var im1 := tx * u + ty * 1.0
	var dim1 := (dtx * u + tx * du) + (dty * 1.0)
	var re2 := re1 * t
	var dre2 := dre1 * t + re1 * dt
	var im2 := im1 * t
	var dim2 := dim1 * t + im1 * dt
	var den := 1.0 * 1.0 + u * u
	var dden := (u * du + u * du)
	var re3 := re2 / den
	var dre3 := (dre2 - re3 * dden) / den
	var im3 := im2 / den
	var dim3 := (dim2 - im3 * dden) / den
	return PackedFloat64Array([b[PX] + re3, b[PY] + im3, b[DPX] + dre3, b[DPY] + dim3])

# unit tangent at t: [tx, ty, dtx, dty]
# t = inf: p + 2 i tau / kappa (HPBeam.at_infinity twin) with its derivative.
static func antipode(b: PackedFloat64Array) -> PackedFloat64Array:
	var k := b[K]
	var dk := b[DK]
	var x := b[PX] + ((-b[TY]) * 2.0) / k
	var y := b[PY] + (b[TX] * 2.0) / k
	var dx := b[DPX] + (-2.0) * (b[DTY] * k - b[TY] * dk) / (k * k)
	var dy := b[DPY] + 2.0 * (b[DTX] * k - b[TX] * dk) / (k * k)
	return PackedFloat64Array([x, y, dx, dy])

# Point at t for display and margins only: no overflow for huge |t| (the
# parametrization is evaluated as tau / (1/t - i kappa/2) there).
static func at_view(b: PackedFloat64Array, t: float) -> PackedFloat64Array:
	if is_inf(t):
		return antipode(b)
	var u := b[K] * t * 0.5
	if absf(u) <= 1.0:
		return at(b, t, 0.0)
	var wr := 1.0 / t
	var wi := -b[K] * 0.5
	var d := wr * wr + wi * wi
	# tau / (wr + i wi) = tau * (wr - i wi) / d
	var x := (b[TX] * wr + b[TY] * wi) / d
	var y := (b[TY] * wr - b[TX] * wi) / d
	return PackedFloat64Array([b[PX] + x, b[PY] + y])

static func tangent_at(b: PackedFloat64Array, t: float, dt: float) -> PackedFloat64Array:
	if t == INF:
		return PackedFloat64Array([-b[TX], -b[TY], -b[DTX], -b[DTY]])
	var k := b[K]
	var tx := b[TX]
	var ty := b[TY]
	var u := k * t * 0.5
	var du := (b[DK] * t + k * dt) * 0.5
	# g = tau * f, f = (1, u)
	var gr := tx * 1.0 - ty * u
	var dgr := b[DTX] - (b[DTY] * u + ty * du)
	var gi := tx * u + ty * 1.0
	var dgi := (b[DTX] * u + tx * du) + b[DTY]
	# h = g * f
	var hr := gr * 1.0 - gi * u
	var dhr := dgr - (dgi * u + gi * du)
	var hi := gr * u + gi * 1.0
	var dhi := (dgr * u + gr * du) + dgi
	var a2 := hr * hr + hi * hi
	var da2 := (dhr * hr + hr * dhr) + (dhi * hi + hi * dhi)
	var a := sqrt(a2)
	var da := da2 / (2.0 * a) if a != 0.0 else 0.0
	var ox := hr / a
	var oy := hi / a
	return PackedFloat64Array([ox, oy, (dhr - ox * da) / a, (dhi - oy * da) / a])

static func param_of(b: PackedFloat64Array, zx: float, zy: float) -> float:
	var wx := zx - b[PX]
	var wy := zy - b[PY]
	var k := b[K]
	var denx := b[TX] + ((-wy) * k) * 0.5
	var deny := b[TY] + (wx * k) * 0.5
	var d := denx * denx + deny * deny
	if denx == 0.0 and deny == 0.0:
		return INF      # z is the antipode
	var ww := wx * wx + wy * wy
	if k != 0.0 and ww > d:
		# near the antipode: t = 1 / Re(den / w) (HPBeam.param_of twin)
		var qre := (denx * wx - deny * (-wy)) / ww
		if qre == 0.0:
			return INF
		return 1.0 / qre
	var nre := wx * denx - wy * (-deny)
	return nre / d

# Roots of beam ∩ circle (cx, cy, R²): [t0, dt0, t1, dt1, ...]
# (HPBeam.hits twin: same float64 operation order -> bit-identical values).
static func circle_hits(b: PackedFloat64Array, cx: float, cy: float, r2: float, depart_on: bool) -> PackedFloat64Array:
	var out := PackedFloat64Array()
	var qx := cx - b[PX]
	var qy := cy - b[PY]
	var dqx := -b[DPX]
	var dqy := -b[DPY]
	var tx := b[TX]
	var ty := b[TY]
	var dtx := b[DTX]
	var dty := b[DTY]
	var k := b[K]
	var dk := b[DK]
	var tq := qx * tx + qy * ty
	var dtq := (dqx * tx + qx * dtx) + (dqy * ty + qy * dty)
	var cr := tx * qy - ty * qx
	var dcr := (dtx * qy + tx * dqy) - (dty * qx + ty * dqx)
	if depart_on:
		var a2d := 1.0 - k * cr
		var da2d := -(dk * cr + k * dcr)
		if a2d != 0.0:
			var num := tq * 2.0
			var t := num / a2d
			out.append(t)
			out.append((dtq * 2.0 - t * da2d) / a2d)
		elif k != 0.0:
			out.append(INF)     # the other crossing is the antipode
			out.append(0.0)
		return out
	var qa2 := qx * qx + qy * qy
	var dqa2 := (dqx * qx + qx * dqx) + (dqy * qy + qy * dqy)
	var P := qa2 - r2
	var dP := dqa2
	var kcr := k * cr
	var dkcr := dk * cr + k * dcr
	var kk := k * k
	var dkk := dk * k + k * dk
	var kkP := kk * P
	var dkkP := dkk * P + kk * dP
	var a2 := (1.0 - kcr) + kkP * 0.25
	var da2 := -dkcr + dkkP * 0.25
	var tq2 := tq * tq
	var dtq2 := dtq * tq + tq * dtq
	var a2P := a2 * P
	var da2P := da2 * P + a2 * dP
	var disc := tq2 - a2P
	var ddisc := dtq2 - da2P
	if disc < 0.0:
		return out
	var s := sqrt(disc)
	var ds := ddisc / (2.0 * s) if s != 0.0 else 0.0
	var big: float
	var dbig: float
	if not (tq < 0.0):
		big = tq + s
		dbig = dtq + ds
	else:
		big = tq - s
		dbig = dtq - ds
	if big != 0.0:
		var t1 := P / big
		out.append(t1)
		out.append((dP - t1 * dbig) / big)
		if a2 != 0.0:
			var t2 := big / a2
			out.append(t2)
			out.append((dbig - t2 * da2) / a2)
		elif k != 0.0:
			out.append(INF)     # the other crossing is the antipode
			out.append(0.0)
	elif a2 != 0.0:
		out.append(0.0)
		out.append(0.0)
	return out

# Roots of beam ∩ line through (sx,sy)->(ex,ey) as [t0, dt0, t1, dt1, ...]:
# BeamGeom.hits twin (same float64 operation order -> bit-identical values)
# with the tangent-linear derivative carried inline.
static func line_hits(b: PackedFloat64Array, sx: float, sy: float, ex: float, ey: float, depart_on: bool) -> PackedFloat64Array:
	var out := PackedFloat64Array()
	var Dx := ex - sx
	var Dy := ey - sy
	var gx := -Dy
	var gy := Dx
	var wx := b[PX] - sx
	var wy := b[PY] - sy
	var dwx := b[DPX]
	var dwy := b[DPY]
	var tx := b[TX]
	var ty := b[TY]
	var dtx := b[DTX]
	var dty := b[DTY]
	var k := b[K]
	var dk := b[DK]
	var fp := Dx * wy - Dy * wx
	var dfp := Dx * dwy - Dy * dwx
	var c1 := gx * tx + gy * ty
	var dc1 := gx * dtx + gy * dty
	var kk := k * k
	var dkk := dk * k + k * dk
	var crg := tx * gy - ty * gx
	var dcrg := dtx * gy - dty * gx
	var a := (fp * kk) * 0.25
	var da := (dfp * kk + fp * dkk) * 0.25
	var h := k * 0.5
	var dh := dk * 0.5
	var bb := h * crg
	var dbb := dh * crg + h * dcrg
	var c2 := a + bb
	var dc2 := da + dbb
	# degree drop (c2 = 0): a curved beam meets the line at its antipode, t = inf
	var curved: bool = b[K] != 0.0
	if depart_on:
		if c2 == 0.0:
			if curved:
				out.append(INF)
				out.append(0.0)
			return out
		var t := (-c1) / c2
		out.append(t)
		out.append(((-dc1) - t * dc2) / c2)
		return out
	if c2 == 0.0:
		if c1 != 0.0:
			var t1 := (-fp) / c1
			out.append(t1)
			out.append(((-dfp) - t1 * dc1) / c1)
		if curved:
			out.append(INF)
			out.append(0.0)
		return out
	var c1s := c1 * c1
	var dc1s := dc1 * c1 + c1 * dc1
	var c2c0 := c2 * fp
	var dc2c0 := dc2 * fp + c2 * dfp
	var disc := c1s - c2c0 * 4.0
	var ddisc := dc1s - dc2c0 * 4.0
	if disc < 0.0:
		return out
	var sq := sqrt(disc)
	var dsq := ddisc / (2.0 * sq) if sq != 0.0 else 0.0
	var big: float
	var dbig: float
	if not (c1 < 0.0):
		big = -(c1 + sq)
		dbig = -(dc1 + dsq)
	else:
		big = -(c1 - sq)
		dbig = -(dc1 - dsq)
	if big == 0.0:
		out.append(fp - fp)
		out.append(0.0)
		return out
	var two_c2 := c2 * 2.0
	var r1 := big / two_c2
	out.append(r1)
	out.append((dbig - r1 * (dc2 * 2.0)) / two_c2)
	var two_c0 := fp * 2.0
	var r2 := two_c0 / big
	out.append(r2)
	out.append((dfp * 2.0 - r2 * dbig) / big)
	return out

# Inversion of the beam at t in the circle (cx, cy, R) (HPBeam.invert_at twin).
static func invert_circle(b: PackedFloat64Array, t: float, dt: float, cx: float, cy: float, r: float, r2: float,
		has_h: bool = false, hx: float = 0.0, hy: float = 0.0) -> PackedFloat64Array:
	var h := at(b, t, dt)
	if has_h:
		h[0] = hx
		h[1] = hy
	var th := tangent_at(b, t, dt)
	var ux := h[0] - cx
	var uy := h[1] - cy
	var dux := h[2]
	var duy := h[3]
	var nux := ux / r
	var nuy := uy / r
	var dnux := dux / r
	var dnuy := duy / r
	var thx := th[0]
	var thy := -th[1]           # conj(tau_h)
	var dthx := th[2]
	var dthy := -th[3]
	# m1 = conj(tau_h) * nu
	var m1r := thx * nux - thy * nuy
	var dm1r := (dthx * nux + thx * dnux) - (dthy * nuy + thy * dnuy)
	var m1i := thx * nuy + thy * nux
	var dm1i := (dthx * nuy + thx * dnuy) + (dthy * nux + thy * dnux)
	# m2 = m1 * nu
	var m2r := m1r * nux - m1i * nuy
	var dm2r := (dm1r * nux + m1r * dnux) - (dm1i * nuy + m1i * dnuy)
	var m2i := m1r * nuy + m1i * nux
	var dm2i := (dm1r * nuy + m1r * dnuy) + (dm1i * nux + m1i * dnux)
	# neg, unit
	var nr := -m2r
	var ni := -m2i
	var dnr := -dm2r
	var dni := -dm2i
	var a2 := nr * nr + ni * ni
	var da2 := (dnr * nr + nr * dnr) + (dni * ni + ni * dni)
	var a := sqrt(a2)
	var da := da2 / (2.0 * a) if a != 0.0 else 0.0
	var tx2 := nr / a
	var ty2 := ni / a
	var dtx2 := (dnr - tx2 * da) / a
	var dty2 := (dni - ty2 * da) / a
	# kappa' = -k - 2 cross(tau_h, u) / R^2
	var thx0 := th[0]
	var thy0 := th[1]
	var c := thx0 * uy - thy0 * ux
	var dc := (th[2] * uy + thx0 * duy) - (th[3] * ux + thy0 * dux)
	var c2 := c * 2.0
	var dc2 := dc * 2.0
	var q := c2 / r2
	var dq := dc2 / r2
	var k2 := (-b[K]) - q
	var dk2 := (-b[DK]) - dq
	return PackedFloat64Array([h[0], h[1], tx2, ty2, k2, h[2], h[3], dtx2, dty2, dk2])


# Reflection of the beam at t in the line s -> e (BeamEffects.reflect_line
# twin): h' = h, tau' = unit(u² conj(tau_h)), u² = D²/|D|², kappa' = -kappa.
static func reflect_line(b: PackedFloat64Array, t: float, dt: float, sx: float, sy: float, ex: float, ey: float,
		has_h: bool = false, hx: float = 0.0, hy: float = 0.0) -> PackedFloat64Array:
	var h := at(b, t, dt)
	if has_h:
		h[0] = hx
		h[1] = hy
	var th := tangent_at(b, t, dt)
	var Dx := ex - sx
	var Dy := ey - sy
	var dre := Dx * Dx - Dy * Dy
	var dim := Dx * Dy + Dy * Dx
	var n := Dx * Dx + Dy * Dy
	var u2r := dre / n
	var u2i := dim / n
	# conj(tau_h)
	var cx := th[0]
	var cy := -th[1]
	var dcx := th[2]
	var dcy := -th[3]
	var re := u2r * cx - u2i * cy
	var im := u2r * cy + u2i * cx
	var dre2 := u2r * dcx - u2i * dcy
	var dim2 := u2r * dcy + u2i * dcx
	var a2 := re * re + im * im
	var da2 := (dre2 * re + re * dre2) + (dim2 * im + im * dim2)
	var a := sqrt(a2)
	var da := da2 / (2.0 * a) if a != 0.0 else 0.0
	var tx2 := re / a
	var ty2 := im / a
	return PackedFloat64Array([h[0], h[1], tx2, ty2, -b[K], h[2], h[3], (dre2 - tx2 * da) / a, (dim2 - ty2 * da) / a, -b[DK]])

# ---------------------------------------------------------------- surfaces

# Roots of the beam with a scene surface's carrier: [t0, dt0, t1, dt1, ...].
static func surf_hits(b: PackedFloat64Array, s: BeamScene.Surf, depart_on: bool) -> PackedFloat64Array:
	if s.line:
		return line_hits(b, s.sx, s.sy, s.ex, s.ey, depart_on)
	return circle_hits(b, s.cx, s.cy, s.r2, depart_on)

# Portal (BeamEffects.portal twin): p' = a h + d, tau' = a tau_h, kappa' = kappa.
static func portal(b: PackedFloat64Array, t: float, dt: float, s: BeamScene.Surf,
		has_h: bool = false, hx: float = 0.0, hy: float = 0.0) -> PackedFloat64Array:
	var h := at(b, t, dt)
	if has_h:
		h[0] = hx
		h[1] = hy
	var th := tangent_at(b, t, dt)
	var ar := s.pa
	var ai := s.pb
	return PackedFloat64Array([
		(ar * h[0] - ai * h[1]) + s.dx, (ar * h[1] + ai * h[0]) + s.dy,
		ar * th[0] - ai * th[1], ar * th[1] + ai * th[0], b[K],
		ar * h[2] - ai * h[3], ar * h[3] + ai * h[2],
		ar * th[2] - ai * th[3], ar * th[3] + ai * th[2], b[DK]])

# Projective (BeamEffects.project twin): straight beam from h along +-axis.
static func project(b: PackedFloat64Array, t: float, dt: float, s: BeamScene.Surf, left: bool,
		has_h: bool = false, hx: float = 0.0, hy: float = 0.0) -> PackedFloat64Array:
	var h := at(b, t, dt)
	if has_h:
		h[0] = hx
		h[1] = hy
	var etype := s.effect(left)
	var pr: Array = s.left_proj if left else s.right_proj
	var gx: float
	var gy: float
	if s.line:
		gx = -(s.ey - s.sy)
		gy = s.ex - s.sx
	else:
		gx = (h[0] - s.cx) * 2.0
		gy = (h[1] - s.cy) * 2.0
	var ax: float
	var ay: float
	if etype == "line_normal_projection":
		ax = gx
		ay = gy
	elif etype == "circle_normal_projection":
		ax = h[0] - s.cx
		ay = h[1] - s.cy
	else:
		ax = pr[1]
		ay = pr[2]
	var ag := ax * gx + ay * gy
	var flip := false
	if ag != 0.0:
		var th := tangent_at(b, t, 0.0)
		var dg := th[0] * gx + th[1] * gy
		flip = (ag < 0.0) != (dg < 0.0)
	if pr[0]:
		flip = not flip
	if flip:
		ax = -ax
		ay = -ay
	var n := sqrt(ax * ax + ay * ay)
	var ux := ax / n
	var uy := ay / n
	# derivative of the exit direction: only the radius depends on the hit
	var dux := 0.0
	var duy := 0.0
	if etype == "circle_normal_projection":
		var sg := -1.0 if flip else 1.0
		var dax := sg * h[2]
		var day := sg * h[3]
		var proj := ux * dax + uy * day
		dux = (dax - ux * proj) / n
		duy = (day - uy * proj) / n
	return PackedFloat64Array([h[0], h[1], ux, uy, 0.0, h[2], h[3], dux, duy, 0.0])

# The surface's transformative effect at t (BeamEffects.apply twin).
# has_h: (hx, hy) is the hit point, exact by provenance (else at(t)).
static func apply_surf(b: PackedFloat64Array, t: float, dt: float, s: BeamScene.Surf,
		has_h: bool = false, hx: float = 0.0, hy: float = 0.0) -> PackedFloat64Array:
	if s.portal:
		return portal(b, t, dt, s, has_h, hx, hy)
	if s.line:
		return reflect_line(b, t, dt, s.sx, s.sy, s.ex, s.ey, has_h, hx, hy)
	return invert_circle(b, t, dt, s.cx, s.cy, s.r, s.r2, has_h, hx, hy)

# ---------------------------------------------------------------- decisions
# Raw twins of BeamGeom's topology decisions (same float64 operation order,
# hence the same decisions as the generic code over F64Num).

# BeamGeom.on_segment: Re(CR(S, P; E, V)) >= 0 (homogeneous; V at infinity
# for full lines).
static func on_segment(s: BeamScene.Surf, px: float, py: float) -> bool:
	if s.full:
		return true
	var svx: float
	var svy: float
	var evx: float
	var evy: float
	if s.has_v:
		svx = s.sx - s.vx
		svy = s.sy - s.vy
		evx = s.ex - s.vx
		evy = s.ey - s.vy
	else:
		# V = (1, 0) with weight 0: _hdet(z, 1, V, 0) = -V
		svx = -1.0
		svy = -0.0
		evx = -1.0
		evy = -0.0
	var epx := s.ex - px
	var epy := s.ey - py
	var spx := s.sx - px
	var spy := s.sy - py
	var nre := svx * epx - svy * epy
	var nim := svx * epy + svy * epx
	var dre := spx * evx - spy * evy
	var dim := spx * evy + spy * evx
	return not ((nre * dre + nim * dim) < 0.0)

# BeamGeom.side_left (winding sign cached on the generic segment).
static func side_left(s: BeamScene.Surf, px: float, py: float, dx: float, dy: float) -> bool:
	var gx: float
	var gy: float
	if s.line:
		gx = -(s.ey - s.sy)
		gy = s.ex - s.sx
	else:
		gx = (px - s.cx) * 2.0
		gy = (py - s.cy) * 2.0
	var approach := -(dx * gx + dy * gy)
	var approach_pos := not (approach < 0.0) and approach != 0.0
	if s.seg._wind_nonneg == null:
		s.seg._wind_nonneg = not BeamGeom.winding(s.seg).is_negative()
	return approach_pos if s.seg._wind_nonneg else not approach_pos

# BeamGeom.endpoint_at: 1 / 2 if the point is exactly the start / end.
static func endpoint_at(s: BeamScene.Surf, px: float, py: float) -> int:
	if s.full:
		return 0
	if px == s.sx and py == s.sy:
		return 1
	if px == s.ex and py == s.ey:
		return 2
	return 0

# BeamGeom.blocked_sides: [blocked_left, blocked_right] through endpoint `which`.
static func blocked_sides(s: BeamScene.Surf, which: int, dx: float, dy: float) -> Array:
	var tx: float
	var ty: float
	if s.line:
		tx = (s.ex - s.sx) if which == 1 else (s.sx - s.ex)
		ty = (s.ey - s.sy) if which == 1 else (s.sy - s.ey)
	else:
		var epx: float = s.sx if which == 1 else s.ex
		var epy: float = s.sy if which == 1 else s.ey
		var rx := epx - s.cx
		var ry := epy - s.cy
		var refx := s.vx - epx
		var refy := s.vy - epy
		# ccw = (-ry, rx), cw = (ry, -rx)
		var dccw := (-ry) * refx + rx * refy
		var dcw := ry * refx + (-rx) * refy
		if not (dccw < dcw):
			tx = -ry
			ty = rx
		else:
			tx = ry
			ty = -rx
	var c := dx * ty - dy * tx
	if c == 0.0:
		return [true, true]
	if not (c < 0.0):
		return [false, true]
	return [true, false]

# ---------------------------------------------------------------- backend

static func _f64cn(x: float, y: float) -> CN:
	return CN.make(F64Num.of(x), F64Num.of(y))

# BeamSolver walker backend over raw beams.
class Backend extends RefCounted:
	static func make() -> Backend:
		return Backend.new()

	func roots(b: PackedFloat64Array, surf: BeamScene.Surf, depart_on: bool) -> Array:
		var raw := FastSolver.surf_hits(b, surf, depart_on)
		var out: Array = []
		var i := 0
		while i < raw.size():
			out.append(DualNum.of(raw[i], raw[i + 1]))
			i += 2
		return out
	func point(b: PackedFloat64Array, t: DualNum) -> CN:
		var h := FastSolver.at(b, t.v, 0.0)
		return FastSolver._f64cn(h[0], h[1])
	func tangent(b: PackedFloat64Array, t: DualNum) -> CN:
		var g := FastSolver.tangent_at(b, t.v, 0.0)
		return FastSolver._f64cn(g[0], g[1])
	func origin(b: PackedFloat64Array) -> CN:
		return FastSolver._f64cn(b[PX], b[PY])
	func param_of(b: PackedFloat64Array, z: CN) -> DualNum:
		return DualNum.of(FastSolver.param_of(b, z.fx(), z.fy()))
	func apply(b: PackedFloat64Array, t: DualNum, surf: BeamScene.Surf, _etype: String, h: CN = null) -> PackedFloat64Array:
		if h == null:
			return FastSolver.apply_surf(b, t.v, t.d, surf)
		return FastSolver.apply_surf(b, t.v, t.d, surf, true, h.fx(), h.fy())
	func straight(b: PackedFloat64Array) -> bool:
		return b[K] == 0.0
	func project(b: PackedFloat64Array, t: DualNum, surf: BeamScene.Surf, left: bool, h: CN = null) -> PackedFloat64Array:
		if h == null:
			return FastSolver.project(b, t.v, t.d, surf, left)
		return FastSolver.project(b, t.v, t.d, surf, left, true, h.fx(), h.fy())
	func straightened(b: PackedFloat64Array) -> PackedFloat64Array:
		var o := PackedFloat64Array(b)
		o[K] = 0.0
		return o
	func zero() -> DualNum:
		return DualNum.of(0.0)
	func seed(b: PackedFloat64Array) -> PackedFloat64Array:
		return FastSolver.seeded(b)
	func reseed(b: PackedFloat64Array) -> PackedFloat64Array:
		if b[DPX] == 0.0 and b[DPY] == 0.0 and b[DTX] == 0.0 and b[DTY] == 0.0 and b[DK] == 0.0:
			return FastSolver.seeded(b)
		return b
	func from_hp(hb: HPBeam) -> PackedFloat64Array:
		return FastSolver.beam(hb.p.fx(), hb.p.fy(), hb.tau.fx(), hb.tau.fy(), hb.kappa.to_float())
	func hp(b: PackedFloat64Array) -> HPBeam:
		return HPBeam.make(FastSolver._f64cn(b[PX], b[PY]), FastSolver._f64cn(b[TX], b[TY]), F64Num.of(b[K]))
	func raw(b: PackedFloat64Array):
		return b
	func ev_t(t: DualNum) -> F64Num:
		return F64Num.of(t.v)
	func gain(b: PackedFloat64Array, t: DualNum) -> float:
		var h := FastSolver.at(b, t.v, t.d)
		return sqrt(h[2] * h[2] + h[3] * h[3])
	func classify(scene: BeamScene, b: PackedFloat64Array, t: DualNum, surf: BeamScene.Surf, at_origin: bool, known_pt = null) -> Array:
		var px: float
		var py: float
		if known_pt != null:
			px = known_pt.fx()
			py = known_pt.fy()
		elif at_origin:
			px = b[PX]
			py = b[PY]
		else:
			var h := FastSolver.at(b, t.v, 0.0)
			px = h[0]
			py = h[1]
		var g := FastSolver.tangent_at(b, t.v, 0.0)
		var on_seg := FastSolver.on_segment(surf, px, py)
		var left := FastSolver.side_left(surf, px, py, g[0], g[1])
		var fb := false
		if on_seg:
			if FastSolver.endpoint_at(surf, px, py) == 0:
				fb = true
			else:
				var bl := false
				var br := false
				for s in scene.surfaces:
					var which := FastSolver.endpoint_at(s, px, py)
					if which == 0:
						continue
					var bs := FastSolver.blocked_sides(s, which, g[0], g[1])
					bl = bl or bs[0]
					br = br or bs[1]
				fb = bl and br
		return [FastSolver._f64cn(px, py), on_seg, left, fb]
	func prefix_gain(b: PackedFloat64Array, t: DualNum) -> float:
		var h := FastSolver.at(FastSolver.seeded(b), t.v, 0.0)
		return sqrt(h[2] * h[2] + h[3] * h[3])
	func f64_point(b: PackedFloat64Array, t: DualNum) -> PackedFloat64Array:
		var h := FastSolver.at_view(b, t.v)
		return PackedFloat64Array([h[0], h[1]])

# ---------------------------------------------------------------- entry points

static func _out(w) -> Out:
	var o := Out.new()
	if w != null:
		o.events = w.events
		o.gain = w.gains
	return o

static func _cn(a) -> CN:
	return _f64cn(float(a[0]), float(a[1]))

static func trace_direction(sc: Scene, player: PackedFloat64Array, dir: PackedFloat64Array, plan: Array, cursor, mode: int,
		max_events: int = BeamSolver.MAX_HITS, _flags: Dictionary = {}) -> Out:
	return _out(BeamSolver.run_direction(Backend.make(), sc.scene, _cn(player), _cn(dir), plan,
		null if cursor == null else _cn(cursor), mode, max_events))

# Planned trace (anchored chain built at float64, generic) with gains.
static func planned(sc: Scene, player: PackedFloat64Array, cursor: PackedFloat64Array, plan: Array,
		max_events: int = BeamSolver.MAX_HITS, _flags: Dictionary = {}) -> Out:
	return _out(BeamSolver.run(Backend.make(), sc.scene, _cn(player), _cn(cursor), plan, BeamSolver.Mode.PLANNED, max_events))

static func physical(sc: Scene, player: PackedFloat64Array, cursor: PackedFloat64Array, plan: Array,
		max_events: int = BeamSolver.MAX_HITS, _flags: Dictionary = {}) -> Out:
	return _out(BeamSolver.run(Backend.make(), sc.scene, _cn(player), _cn(cursor), plan, BeamSolver.Mode.PHYSICAL, max_events))
