class_name FastSolver
extends RefCounted

# Raw-float twin of BeamSolver for the float64 tier, ~50x faster than the
# precision-generic code (which wraps every scalar in an object). It performs
# the same operations in the same order, so float64 values are bit-identical
# to BeamSolver over F64Num, and it carries the tangent-linear derivative
# (DualNum semantics) inline for ErrorEstimate. BigFloat tiers use BeamSolver.
#
# Beam: PackedFloat64Array [px, py, tx, ty, k, dpx, dpy, dtx, dty, dk]
# Circles: parallel arrays (see Scene).

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

class Scene extends RefCounted:
	var ids: PackedInt64Array
	var cx: PackedFloat64Array
	var cy: PackedFloat64Array
	var r: PackedFloat64Array
	var r2: PackedFloat64Array
	var index_of: Dictionary = {}

	static func make(defs: Array) -> Scene:
		var s := Scene.new()
		for d in defs:
			s.index_of[int(d[0])] = s.ids.size()
			s.ids.append(int(d[0]))
			s.cx.append(float(d[1]))
			s.cy.append(float(d[2]))
			s.r.append(float(d[3]))
			s.r2.append(float(d[3]) * float(d[3]))
		return s

	func size() -> int:
		return ids.size()

	# STUB (Stage 81 red): scene-dump defs
	static func from_defs(_defs: Array) -> Scene:
		return Scene.new()

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
static func tangent_at(b: PackedFloat64Array, t: float, dt: float) -> PackedFloat64Array:
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
	var nre := wx * denx - wy * (-deny)
	return nre / d

# Roots of beam ∩ circle i: [t0, dt0, t1, dt1, ...]
static func hits(b: PackedFloat64Array, sc: Scene, i: int, depart_on: bool) -> PackedFloat64Array:
	var out := PackedFloat64Array()
	var qx := sc.cx[i] - b[PX]
	var qy := sc.cy[i] - b[PY]
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
		return out
	var qa2 := qx * qx + qy * qy
	var dqa2 := (dqx * qx + qx * dqx) + (dqy * qy + qy * dqy)
	var P := qa2 - sc.r2[i]
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
	if depart_on:
		if c2 == 0.0:
			return out
		var t := (-c1) / c2
		out.append(t)
		out.append(((-dc1) - t * dc2) / c2)
		return out
	if c2 == 0.0:
		if c1 == 0.0:
			return out
		var t1 := (-fp) / c1
		out.append(t1)
		out.append(((-dfp) - t1 * dc1) / c1)
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

static func is_entering(b: PackedFloat64Array, t: float, sc: Scene, i: int) -> bool:
	var h := at(b, t, 0.0)
	var d := tangent_at(b, t, 0.0)
	return (d[0] * (h[0] - sc.cx[i]) + d[1] * (h[1] - sc.cy[i])) < 0.0

static func invert_at(b: PackedFloat64Array, t: float, dt: float, sc: Scene, i: int) -> PackedFloat64Array:
	var h := at(b, t, dt)
	var th := tangent_at(b, t, dt)
	var ux := h[0] - sc.cx[i]
	var uy := h[1] - sc.cy[i]
	var dux := h[2]
	var duy := h[3]
	var r := sc.r[i]
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
	var q := c2 / sc.r2[i]
	var dq := dc2 / sc.r2[i]
	var k2 := (-b[K]) - q
	var dk2 := (-b[DK]) - dq
	return PackedFloat64Array([h[0], h[1], tx2, ty2, k2, h[2], h[3], dtx2, dty2, dk2])

static func rebased_at(b: PackedFloat64Array, t: float, dt: float) -> PackedFloat64Array:
	var h := at(b, t, dt)
	var th := tangent_at(b, t, dt)
	return PackedFloat64Array([h[0], h[1], th[0], th[1], b[K], h[2], h[3], th[2], th[3], b[DK]])

# Oriented circle P -> X -> Q (Q = P - delta), anchored at P. has_delta false
# -> Q is infinity (a line). delta is passed as float64 components: it can be
# as small as 1e-40 and must keep full relative precision.
static func through(Px: float, Py: float, Xx: float, Xy: float, has_delta: bool, dlx: float, dly: float) -> PackedFloat64Array:
	var ax := Xx - Px
	var ay := Xy - Py
	if not has_delta:
		var a := sqrt(ax * ax + ay * ay)
		return beam(Px, Py, ax / a, ay / a, ax - ax)
	var bx: float = -dlx
	var by: float = -dly
	var aa := ax * ax + ay * ay
	var bb := bx * bx + by * by
	# g = |a|^2 J(b) - |b|^2 J(a), J(v) = (v.y, -v.x); tau = -i * unit(g)
	var gx := by * aa - ay * bb
	var gy := (-bx) * aa - (-ax) * bb
	var tx := gy
	var ty := -gx
	var tl := sqrt(tx * tx + ty * ty)
	var cross := ax * by - ay * bx
	var bax := bx - ax
	var bay := by - ay
	var den := (sqrt(aa) * sqrt(bb)) * sqrt(bax * bax + bay * bay)
	return beam(Px, Py, tx / tl, ty / tl, (cross * 2.0) / den)

static func _inv(z: PackedFloat64Array, sc: Scene, i: int) -> PackedFloat64Array:
	var ux := z[0] - sc.cx[i]
	var uy := z[1] - sc.cy[i]
	var s := (ux * ux + uy * uy)
	return PackedFloat64Array([sc.cx[i] + (ux * sc.r2[i]) / s, sc.cy[i] + (uy * sc.r2[i]) / s])

# STUB (Stage 81 red): reflection of beam b at t in the line s -> e
static func reflect_line(_b: PackedFloat64Array, _t: float, _dt: float, _sx: float, _sy: float, _ex: float, _ey: float) -> PackedFloat64Array:
	return PackedFloat64Array()

# STUB (Stage 81 red): BeamSolver.trace_direction twin
static func trace_direction(_sc: Scene, _player: PackedFloat64Array, _dir: PackedFloat64Array, _plan: Array, _cursor, _mode: int) -> Out:
	return Out.new()

# ---------------------------------------------------------------- events

static func _before(a: float, b: float, t0: float) -> bool:
	var ka: int = 0 if a > t0 else 1
	var kb: int = 0 if b > t0 else 1
	if ka != kb:
		return ka < kb
	return a < b

static func _mk_event(kind: String, sid: int, left: bool, applied: bool, b: PackedFloat64Array,
		t_from: float, t_to, margin: float) -> BeamSolver.Event:
	var e := BeamSolver.Event.new()
	e.kind = kind
	e.surface_id = sid
	e.left = left
	e.applied = applied
	e.beam = HPBeam.make(CN.make(F64Num.of(b[PX]), F64Num.of(b[PY])), CN.make(F64Num.of(b[TX]), F64Num.of(b[TY])), F64Num.of(b[K]))
	e.t_from = F64Num.of(t_from)
	e.t_to = null if t_to == null else F64Num.of(t_to)
	if t_to != null:
		var h := at(b, t_to, 0.0)
		e.point = CN.make(F64Num.of(h[0]), F64Num.of(h[1]))
	e.margin = margin
	e.raw = b
	return e

static func _gain(b: PackedFloat64Array, t, dt: float) -> float:
	if t == null:
		return 0.0
	var h := at(b, t, dt)
	return sqrt(h[2] * h[2] + h[3] * h[3])

static func _dist(ax: float, ay: float, bx: float, by: float) -> float:
	var dx := ax - bx
	var dy := ay - by
	return sqrt(dx * dx + dy * dy)

# ---------------------------------------------------------------- forward

# Roots of beam ∩ circle i that are candidates, as [t, dt, ...] (BeamSolver._roots).
static func _cand_roots(b: PackedFloat64Array, sc: Scene, i: int, fresh: bool, depart: int, t0: float) -> PackedFloat64Array:
	if fresh:
		var hs := hits(b, sc, i, depart == sc.ids[i])
		var o := PackedFloat64Array()
		var j := 0
		while j < hs.size():
			if hs[j] != 0.0:
				o.append(hs[j])
				o.append(hs[j + 1])
			j += 2
		return o
	var hs2 := hits(b, sc, i, false)
	if sc.ids[i] != depart:
		return hs2
	var o2 := PackedFloat64Array()
	var q := 0
	while q < hs2.size():
		if hs2[q] != t0:
			o2.append(hs2[q])
			o2.append(hs2[q + 1])
		q += 2
	return o2

# BeamSolver._resume_param twin.
static func _resume_param(top: BeamSolver.StackEntry, cur: PackedFloat64Array, t: float, born_at_hit: bool) -> float:
	if born_at_hit:
		return top.other
	var p := at(cur, t, 0.0)
	var h := at(top.beam, top.t_hit, 0.0)
	var o := at(top.beam, top.other, 0.0)
	var d_hit := (h[0] - p[0]) * (h[0] - p[0]) + (h[1] - p[1]) * (h[1] - p[1])
	var d_other := (o[0] - p[0]) * (o[0] - p[0]) + (o[1] - p[1]) * (o[1] - p[1])
	return top.t_hit if d_hit < d_other else top.other

# The other intersection of b with circle i, given the applied one t.
static func _other_root(b: PackedFloat64Array, sc: Scene, i: int, depart: int, t0: float, t: float) -> float:
	if sc.ids[i] == depart:
		return t0
	var hs := hits(b, sc, i, false)
	var q := 0
	while q < hs.size():
		if hs[q] != t:
			return hs[q]
		q += 2
	return t

# Forward trace (BeamSolver.continue_from twin). t_start null: fresh start at
# the beam's own point. stack: Array of BeamSolver.StackEntry with raw beams.
static func continue_from(sc: Scene, b: PackedFloat64Array, depart: int, plan: Array, plan_index: int,
		plan_matched: bool, cursor, cursor_done: bool, mode: int, post_mode: int, max_events: int, out: Out,
		stack: Array = [], t_start = null) -> void:
	var cur_mode := mode
	var n0 := out.events.size()
	var fresh: bool = t_start == null
	var t0: float = 0.0 if fresh else float(t_start)
	while out.events.size() - n0 < max_events:
		# candidates: [t, dt, circle index or -1 for cursor]
		var cands: Array = []
		for i in sc.size():
			var hs := _cand_roots(b, sc, i, fresh, depart, t0)
			var j := 0
			while j < hs.size():
				cands.append([hs[j], hs[j + 1], i])
				j += 2
		if not cursor_done and plan_index >= plan.size() and plan_matched and cursor != null:
			cands.append([param_of(b, cursor.x, cursor.y), 0.0, -1])
		var t_origin := t0
		cands.sort_custom(func(x, y): return _before(x[0], y[0], t_origin))
		var pts: Array = []
		for c in cands:
			pts.append(at(b, c[0], 0.0))
		var applied := false
		var t_prev := t0
		for ci in cands.size():
			if out.events.size() - n0 >= max_events:
				break
			var t: float = cands[ci][0]
			var dt: float = cands[ci][1]
			var i: int = cands[ci][2]
			# decision margin vs. neighbouring SURFACE candidates (the cursor is not
			# an effect: its order relative to a coincident hit changes nothing)
			var margin := INF
			if i >= 0:
				var cl := ci - 1
				while cl >= 0 and cands[cl][2] < 0:
					cl -= 1
				if cl >= 0:
					margin = minf(margin, _dist(pts[ci][0], pts[ci][1], pts[cl][0], pts[cl][1]))
				var cr := ci + 1
				while cr < cands.size() and cands[cr][2] < 0:
					cr += 1
				if cr < cands.size():
					margin = minf(margin, _dist(pts[ci][0], pts[ci][1], pts[cr][0], pts[cr][1]))
			if i < 0:
				out.events.append(_mk_event("cursor", -1, false, false, b, t_prev, t, margin))
				out.gain.append(_gain(b, t, dt))
				t_prev = t
				cursor_done = true
				cur_mode = post_mode
				continue
			var sid: int = sc.ids[i]
			var left := is_entering(b, t, sc, i)
			var do_apply := false
			if cur_mode == BeamSolver.Mode.PHYSICAL:
				do_apply = true
				if plan_index < plan.size():
					if sid == plan[plan_index].surface_id:
						plan_index += 1
					else:
						plan_matched = false
				else:
					plan_matched = false
			elif plan_index < plan.size() and sid == plan[plan_index].surface_id and left == plan[plan_index].left:
				do_apply = true
				plan_index += 1
			out.events.append(_mk_event("hit", sid, left, do_apply, b, t_prev, t, margin))
			out.gain.append(_gain(b, t, dt))
			t_prev = t
			if do_apply:
				if not stack.is_empty() and stack.back().sid == sid:
					# cancelling re-hit: restore the earlier beam exactly
					var top: BeamSolver.StackEntry = stack.pop_back()
					t0 = _resume_param(top, b, t, fresh and depart == sid)
					b = top.beam
					# a beam restored from the unseeded (planned) prefix carries its
					# own rounding error into the sensitive continuation: seed it
					if b[DPX] == 0.0 and b[DPY] == 0.0 and b[DTX] == 0.0 and b[DTY] == 0.0 and b[DK] == 0.0:
						b = seeded(b)
					fresh = false
				else:
					var other := _other_root(b, sc, i, depart, t0, t)
					stack.append(BeamSolver.StackEntry.make(b, sid, t, other))
					b = invert_at(b, t, dt, sc, i)
					t0 = 0.0
					fresh = true
				depart = sid
				applied = true
				break
		if not applied:
			if out.events.size() - n0 < max_events:
				if b[K] == 0.0:
					out.events.append(_mk_event("escape", -1, false, false, b, t_prev, null, INF))
				else:
					out.events.append(_mk_event("origin", -1, false, false, b, t_prev, t0, INF))
				out.gain.append(0.0)
			break

# ---------------------------------------------------------------- planned

# player / cursor: PackedFloat64Array [x, y] (float64)
static func anchored_planned(sc: Scene, player: PackedFloat64Array, cursor: PackedFloat64Array, plan: Array, post_events: int) -> Out:
	var out := Out.new()
	var K_ := plan.size()
	var stacks: Array = [[]]
	var st: Array = []
	for item in plan:
		if not st.is_empty() and st.back() == item.surface_id:
			st.pop_back()
		else:
			st.append(item.surface_id)
		stacks.append(st.duplicate())
	# pole chain: key -> [Px, Py, Qx, Qy, dx, dy, has_q]
	var memo := {"": PackedFloat64Array([player[0], player[1], 0.0, 0.0, 0.0, 0.0, 0.0])}
	for s in stacks:
		for n in range(1, s.size() + 1):
			var key := BeamSolver._stack_key(s.slice(0, n))
			if memo.has(key):
				continue
			var parent: PackedFloat64Array = memo[BeamSolver._stack_key(s.slice(0, n - 1))]
			memo[key] = _push_pole(parent, sc, sc.index_of[s[n - 1]])
	var X: Array = []
	X.resize(K_ + 1)
	var seen := {}
	for j in range(K_, -1, -1):
		var key := BeamSolver._stack_key(stacks[j])
		if seen.has(key):
			X[j] = seen[key]
		elif j == K_:
			X[j] = cursor
		else:
			X[j] = _inv(X[j + 1], sc, sc.index_of[plan[j].surface_id])
		seen[key] = X[j]
	var beam_of := {}
	var beams: Array = []
	for j in K_ + 1:
		var key := BeamSolver._stack_key(stacks[j])
		if not beam_of.has(key):
			var pc: PackedFloat64Array = memo[key]
			var xj: PackedFloat64Array = X[j]
			beam_of[key] = through(pc[0], pc[1], xj[0], xj[1], pc[6] != 0.0, pc[4], pc[5])
		beams.append(beam_of[key])
	var astack: Array = []
	var t_start := 0.0
	var prev_i := -1
	var prev_t = null       # exact departure parameter after a pop
	var prev_x := 0.0
	var prev_y := 0.0
	for j in K_ + 1:
		var B: PackedFloat64Array = beams[j]
		var t_end: float
		var end_i := -1
		if j < K_:
			end_i = sc.index_of[plan[j].surface_id]
			var roots := _roots_excluding(B, sc, end_i, prev_i, prev_x, prev_y, prev_t)
			var cand: Array = []
			for t in roots:
				if is_entering(B, t, sc, end_i) == plan[j].left:
					cand.append(t)
			if cand.is_empty():
				# plan cannot be followed: planned mode passes everything and ends
				# (BeamSolver.anchored_planned)
				var t_dep = null   # null: fresh start at B's own point (the player)
				if j > 0:
					t_dep = prev_t if prev_t != null else _departure_root(B, sc, prev_i, prev_x, prev_y)
				continue_from(sc, B, sc.ids[prev_i] if prev_i >= 0 else -1, plan, j, false, null, true,
					BeamSolver.Mode.PLANNED, BeamSolver.Mode.PLANNED, post_events + 64, out, astack, t_dep)
				return out
			t_end = cand[0]
			for t in cand:
				if _before(t, t_end, t_start):
					t_end = t
		else:
			t_end = param_of(B, cursor[0], cursor[1])
		var passes: Array = []
		for i in sc.size():
			for t in _roots_excluding(B, sc, i, prev_i, prev_x, prev_y, prev_t):
				if i == end_i and t == t_end:
					continue
				if t != t_start and _before(t, t_end, t_start):
					passes.append([t, i])
		passes.sort_custom(func(x, y): return _before(x[0], y[0], t_start))
		var ep := at(B, t_end, 0.0)
		var end_margin := INF
		var end_is_hit := end_i >= 0
		if end_i >= 0:
			var hs := hits(B, sc, end_i, false)
			var q := 0
			while q < hs.size():
				if hs[q] != t_end:
					var o := at(B, hs[q], 0.0)
					end_margin = minf(end_margin, _dist(o[0], o[1], ep[0], ep[1]))
				q += 2
		if end_is_hit:
			for pr in passes:
				var o := at(B, pr[0], 0.0)
				end_margin = minf(end_margin, _dist(o[0], o[1], ep[0], ep[1]))
		var t_prev := t_start
		for pr in passes:
			var o := at(B, pr[0], 0.0)
			out.events.append(_mk_event("hit", sc.ids[pr[1]], is_entering(B, pr[0], sc, pr[1]), false, B, t_prev, pr[0], _dist(o[0], o[1], ep[0], ep[1]) if end_is_hit else INF))
			out.gain.append(0.0)
			t_prev = pr[0]
		if j < K_:
			out.events.append(_mk_event("hit", plan[j].surface_id, plan[j].left, true, B, t_prev, t_end, end_margin))
			out.gain.append(0.0)
			var sid: int = plan[j].surface_id
			prev_i = end_i
			if not astack.is_empty() and astack.back().sid == sid:
				var top: BeamSolver.StackEntry = astack.pop_back()
				t_start = top.other
				prev_t = top.other
			else:
				var other := t_end
				var hs := hits(B, sc, end_i, false)
				var q := 0
				while q < hs.size():
					if hs[q] != t_end:
						other = hs[q]
					q += 2
				astack.append(BeamSolver.StackEntry.make(B, sid, t_end, other))
				prev_t = null
				prev_x = ep[0]
				prev_y = ep[1]
				t_start = param_of(beams[j + 1], prev_x, prev_y)
		else:
			out.events.append(_mk_event("cursor", -1, false, false, B, t_prev, t_end, end_margin))
			out.gain.append(0.0)
			if post_events > 0:
				var cur := seeded(rebased_at(B, t_end, 0.0))
				continue_from(sc, cur, -1, plan, K_, true, null, true,
					BeamSolver.Mode.PHYSICAL, BeamSolver.Mode.PHYSICAL, post_events, out, astack.duplicate())
	return out

static func _push_pole(parent: PackedFloat64Array, sc: Scene, i: int) -> PackedFloat64Array:
	var P1 := _inv(PackedFloat64Array([parent[0], parent[1]]), sc, i)
	var cx := sc.cx[i]
	var cy := sc.cy[i]
	if parent[6] == 0.0:
		var ux := parent[0] - cx
		var uy := parent[1] - cy
		var s := ux * ux + uy * uy
		return PackedFloat64Array([P1[0], P1[1], cx, cy, (ux * sc.r2[i]) / s, (uy * sc.r2[i]) / s, 1.0])
	var uqx := parent[2] - cx
	var uqy := parent[3] - cy
	if uqx == 0.0 and uqy == 0.0:
		return PackedFloat64Array([P1[0], P1[1], 0.0, 0.0, 0.0, 0.0, 0.0])
	var Q1 := _inv(PackedFloat64Array([parent[2], parent[3]]), sc, i)
	# delta' = -R^2 conj(delta / ((P0 - c)(Q0 - c)))
	var apx := parent[0] - cx
	var apy := parent[1] - cy
	var mr := apx * uqx - apy * uqy
	var mi := apx * uqy + apy * uqx
	var dx := parent[4]
	var dy := parent[5]
	var mm := mr * mr + mi * mi
	var nr := dx * mr - dy * (-mi)
	var ni := dx * (-mi) + dy * mr
	var qr := nr / mm
	var qi := ni / mm
	var d1x := -(qr * sc.r2[i])
	var d1y := -((-qi) * sc.r2[i])
	return PackedFloat64Array([Q1[0] + d1x, Q1[1] + d1y, Q1[0], Q1[1], d1x, d1y, 1.0])

static func _departure_root(B: PackedFloat64Array, sc: Scene, i: int, px: float, py: float) -> float:
	var hs := hits(B, sc, i, false)
	var best := 0.0
	var best_d := INF
	var q := 0
	while q < hs.size():
		var h := at(B, hs[q], 0.0)
		var d := (h[0] - px) * (h[0] - px) + (h[1] - py) * (h[1] - py)
		if d < best_d:
			best_d = d
			best = hs[q]
		q += 2
	return best

static func _roots_excluding(B: PackedFloat64Array, sc: Scene, i: int, prev_i: int, px: float, py: float, prev_t = null) -> Array:
	var hs := hits(B, sc, i, false)
	var ts: Array = []
	var q := 0
	while q < hs.size():
		ts.append(hs[q])
		q += 2
	if prev_i == i and prev_t != null:
		return ts.filter(func(t): return t != prev_t)
	if prev_i == i and not ts.is_empty():
		var best := 0
		var best_d := INF
		for k in ts.size():
			var h := at(B, ts[k], 0.0)
			var d := (h[0] - px) * (h[0] - px) + (h[1] - py) * (h[1] - py)
			if d < best_d:
				best_d = d
				best = k
		ts.remove_at(best)
	return ts

# ---------------------------------------------------------------- physical

static func physical(sc: Scene, player: PackedFloat64Array, cursor: PackedFloat64Array, plan: Array, max_events: int) -> Out:
	if plan.is_empty():
		var dx := cursor[0] - player[0]
		var dy := cursor[1] - player[1]
		var l := sqrt(dx * dx + dy * dy)
		var out0 := Out.new()
		continue_from(sc, seeded(beam(player[0], player[1], dx / l, dy / l, 0.0)), -1, plan, 0, true,
			_CursorXY.make(cursor), false,
			BeamSolver.Mode.PHYSICAL, BeamSolver.Mode.PHYSICAL, max_events, out0)
		return out0
	var planned := anchored_planned(sc, player, cursor, plan, max_events)
	var out := Out.new()
	if planned.events.is_empty():
		return out
	var plan_index := 0
	var stack: Array = []
	for n in planned.events.size():
		if out.events.size() >= max_events:
			break
		var e: BeamSolver.Event = planned.events[n]
		var i: int = sc.index_of.get(e.surface_id, -1)
		if e.kind == "hit" and not e.applied:
			var Bv: PackedFloat64Array = e.raw
			var d := _mk_event("hit", e.surface_id, e.left, true, Bv, e.t_from.v, e.t_to.v, e.margin)
			out.events.append(d)
			out.gain.append(0.0)
			var matched: bool = plan_index < plan.size() and plan[plan_index].surface_id == e.surface_id
			if not stack.is_empty() and stack.back().sid == e.surface_id:
				var top: BeamSolver.StackEntry = stack.pop_back()
				var t_res := _resume_param(top, Bv, e.t_to.v, false)
				continue_from(sc, seeded(top.beam), e.surface_id, plan, plan_index + (1 if matched else 0), matched,
					_CursorXY.make(cursor), false, BeamSolver.Mode.PHYSICAL, BeamSolver.Mode.PHYSICAL,
					max_events - out.events.size(), out, stack, t_res)
			else:
				stack.append(BeamSolver.StackEntry.make(Bv, e.surface_id, e.t_to.v, _other_of(Bv, sc, i, e.t_to.v)))
				var nb := seeded(invert_at(Bv, e.t_to.v, 0.0, sc, i))
				continue_from(sc, nb, e.surface_id, plan, plan_index + (1 if matched else 0), matched,
					_CursorXY.make(cursor), false, BeamSolver.Mode.PHYSICAL, BeamSolver.Mode.PHYSICAL,
					max_events - out.events.size(), out, stack)
			return out
		out.events.append(e)
		out.gain.append(planned.gain[n])
		if e.kind == "hit":
			plan_index += 1
			if not stack.is_empty() and stack.back().sid == e.surface_id:
				stack.pop_back()
			else:
				stack.append(BeamSolver.StackEntry.make(e.raw, e.surface_id, e.t_to.v, _other_of(e.raw, sc, i, e.t_to.v)))
	return out

static func _other_of(b: PackedFloat64Array, sc: Scene, i: int, t: float) -> float:
	var other := t
	var hs := hits(b, sc, i, false)
	var q := 0
	while q < hs.size():
		if hs[q] != t:
			other = hs[q]
		q += 2
	return other

static func _beam_of(e: BeamSolver.Event) -> PackedFloat64Array:
	var b: HPBeam = e.beam
	return beam(b.p.re.v, b.p.im.v, b.tau.re.v, b.tau.im.v, b.kappa.v)

class _CursorXY extends RefCounted:
	var x: float
	var y: float
	static func make(c: PackedFloat64Array) -> _CursorXY:
		var o := _CursorXY.new()
		o.x = c[0]
		o.y = c[1]
		return o
