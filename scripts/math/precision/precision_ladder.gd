class_name PrecisionLadder
extends RefCounted

# Dynamic-precision driver for BeamSolver.
#
# 1. One float64 pass over DualNum yields the float64 trace AND its a-priori
#    accumulated-error estimate (ErrorEstimate: tangent-linear envelope; the
#    value parts are bit-identical to plain float64), plus a reliability flag:
#    every ordering/root decision's margin must exceed its error estimate,
#    otherwise the float64 trajectory may be on a different branch and its
#    gains do not describe the true one.
# 2. float64 is accepted when the estimate is <= tol and the trace is reliable.
# 3. Otherwise BigFloat: start at the tier the estimate asks for (at least
#    112 bits) and climb until two consecutive tiers agree within tol with
#    identical event structure (a-posteriori convergence). The higher tier is
#    returned; its estimated error is the measured gap scaled by eps ratio.
# verify = true also cross-checks float64 acceptances against 112 bits.

const TIERS := [0, 4, 8, 16, 32, 64]   # limbs; 0 = float64 (53 bits) .. 1792 bits

class Result extends RefCounted:
	var planned: Array = []
	var planned_err: Array = []       # per-event estimated error (px)
	var planned_tier: int = 0         # limbs of the accepted tier (0 = f64)
	var physical: Array = []
	var physical_err: Array = []
	var physical_tier: int = 0
	var converged: bool = false
	var tiers_tried: Array = []
	var usec: int = 0
	var usec_planned: int = 0
	var usec_physical: int = 0
	var physical_sens: ErrorEstimate.Sensitivity = null   # float64 tangent-linear gains of the physical pass

class Climb extends RefCounted:
	var events: Array = []
	var err: Array = []
	var limbs: int = 0
	var converged: bool = false
	var tried: Array = []

# defs: scene defs (BeamScene). player / cursor: [x, y] float64 (Vector2 would
# round them to float32). max_planned / max_physical: event limits (MAX_HITS).
# Degenerate inputs (no anchored construction) return an empty Result.
# fsc: the float64 scene of defs (FastSolver.Scene.from_defs), if cached.
# flags: the trace's initial state flags (the game state's).
static func solve(defs: Array, player: Array, cursor: Array, plan: Array,
		max_planned: int, max_physical: int, tol: float, verify: bool = false, fsc: FastSolver.Scene = null,
		flags: Dictionary = {}) -> Result:
	var t0 := Time.get_ticks_usec()
	var r := Result.new()
	if fsc == null:
		fsc = FastSolver.Scene.from_defs(defs)
	# ---- planned (float64 pass)
	var sq := ErrorEstimate.planned_sensitivity(defs, player, cursor, plan, max_planned, fsc, flags)
	if sq.events.is_empty():
		return r
	var est_q := ErrorEstimate.estimate(sq, NumCtx.f64().eps())
	var res_q := planned_residuals(sq.events, fsc.scene)
	for i in est_q.size():
		est_q[i] = maxf(est_q[i], res_q[i])
	# ---- physical
	var t1 := Time.get_ticks_usec()
	var cp := _solve_physical(r, defs, player, cursor, plan, max_physical, tol, verify, fsc, flags)
	var t2 := Time.get_ticks_usec()
	r.usec_physical = t2 - t1
	# ---- planned
	# Physical follows the anchored planned path until its first divergence;
	# if that is after the cursor, both continue identically from it (same
	# beam, same stack, both PHYSICAL after the cursor): one trajectory. The
	# planned prefix must be reliable in float64 and the accepted physical
	# trace must still agree with it through the cursor.
	var ci := _cursor_index(sq.events)
	if not verify and ci >= 0 and (sq.unreliable_from < 0 or sq.unreliable_from > ci) \
			and _same_structure(sq.events, cp.events, ci + 1):
		r.planned = cp.events
		r.planned_err = cp.err
		r.planned_tier = cp.limbs
	else:
		var planned_fn := func(limbs: int) -> Array:
			var ctx := NumCtx.with_limbs(limbs)
			return BeamSolver.planned(ctx, BeamScene.from_defs(ctx, defs),
				ctx.cn(player[0], player[1]), ctx.cn(cursor[0], cursor[1]), plan, max_planned, flags)
		var cq := _decide(sq, est_q, tol, verify, planned_fn, defs, fsc)
		r.planned = cq.events
		r.planned_err = cq.err
		r.planned_tier = cq.limbs
	r.usec_planned = (t1 - t0) + (Time.get_ticks_usec() - t2)
	r.usec = Time.get_ticks_usec() - t0
	return r

# The physical trace alone (no planned pass): physical, physical_err,
# physical_tier, converged, tiers_tried and physical_sens of the Result.
static func solve_physical(defs: Array, player: Array, cursor: Array, plan: Array,
		max_physical: int, tol: float, fsc: FastSolver.Scene = null, flags: Dictionary = {}) -> Result:
	var t0 := Time.get_ticks_usec()
	var r := Result.new()
	if fsc == null:
		fsc = FastSolver.Scene.from_defs(defs)
	_solve_physical(r, defs, player, cursor, plan, max_physical, tol, false, fsc, flags)
	r.usec_physical = Time.get_ticks_usec() - t0
	r.usec = r.usec_physical
	return r

static func _solve_physical(r: Result, defs: Array, player: Array, cursor: Array, plan: Array,
		max_physical: int, tol: float, verify: bool, fsc: FastSolver.Scene, flags: Dictionary = {}) -> Climb:
	var sp := ErrorEstimate.physical_sensitivity(defs, player, cursor, plan, max_physical, fsc, flags)
	var est_p := ErrorEstimate.estimate(sp, NumCtx.f64().eps())
	var physical_fn := func(limbs: int) -> Array:
		return _physical_at(limbs, defs, player, cursor, plan, max_physical, flags)
	var cp := _decide(sp, est_p, tol, verify, physical_fn, defs, fsc)
	r.physical = cp.events
	r.physical_err = cp.err
	r.physical_tier = cp.limbs
	r.converged = cp.converged
	r.tiers_tried = cp.tried
	r.physical_sens = sp
	return cp

static func _cursor_index(events: Array) -> int:
	for i in events.size():
		if events[i].kind == "cursor":
			return i
	return -1

# Same event structure (kind, surface, side, applied) for the first n events.
static func _same_structure(a: Array, b: Array, n: int) -> bool:
	if a.size() < n or b.size() < n:
		return false
	for i in n:
		var x: BeamSolver.Event = a[i]
		var y: BeamSolver.Event = b[i]
		if x.kind != y.kind or x.surface_id != y.surface_id or x.left != y.left or x.applied != y.applied or x.state != y.state:
			return false
	return true

static func _decide(sens: ErrorEstimate.Sensitivity, est: Array, tol: float, verify: bool, compute: Callable,
		defs: Array = [], fsc: FastSolver.Scene = null) -> Climb:
	var reliable := sens.unreliable_from < 0 and not sens.events.is_empty()
	if reliable and _worst(est) <= tol and not verify:
		var c := Climb.new()
		c.events = sens.events
		c.err = est
		c.limbs = 0
		c.converged = true
		c.tried = [0]
		return c
	var start_limbs := 4
	if reliable or sens.unreliable_from > 0:
		start_limbs = maxi(4, _required_for_prefix(sens, sens.gain.size() if reliable else sens.unreliable_from, tol))
	if verify or defs.is_empty():
		var start_events: Array = sens.events if (verify and reliable and _worst(est) <= tol) else []
		return climb(start_limbs if start_events.is_empty() else 0, start_events, tol, compute)
	return replay_climb(start_limbs, tol, compute, defs, fsc)

static func _required_for_prefix(sens: ErrorEstimate.Sensitivity, n: int, tol: float) -> int:
	var prefix := ErrorEstimate.Sensitivity.new()
	prefix.gain = sens.gain.slice(0, n)
	for g in prefix.gain:
		prefix.max_gain = maxf(prefix.max_gain, g)
	return ErrorEstimate.required_limbs(prefix, tol, TIERS.slice(1))

# Compute at `limbs`, re-estimate along that (high-precision) trajectory with
# ErrorEstimate.replay, accept when the estimate meets tol and every decision
# is reliable at that tier; otherwise jump to the tier the replay asks for.
static func replay_climb(limbs: int, tol: float, compute: Callable, defs: Array, fsc: FastSolver.Scene = null) -> Climb:
	var c := Climb.new()
	var ti := TIERS.find(limbs)
	while ti < TIERS.size():
		var L: int = TIERS[ti]
		var ev: Array = compute.call(L)
		c.tried.append(L)
		c.events = ev
		c.limbs = L
		var eps := NumCtx.with_limbs(L).eps()
		var rs := ErrorEstimate.replay(ev, defs, fsc)
		c.err = ErrorEstimate.estimate(rs, eps)
		var bad := ErrorEstimate.unreliable_index(rs, eps)
		if bad < 0 and _worst(c.err) <= tol and not ev.is_empty():
			c.converged = true
			return c
		# next tier: what the (reliable prefix of the) replay asks for, at least +1
		var need := _required_for_prefix(rs, rs.gain.size() if bad < 0 else maxi(bad, 1), tol)
		var nti := maxi(ti + 1, TIERS.find(need))
		ti = nti
	return c

# Climb tiers from `limbs` until two consecutive tiers agree within tol.
static func climb(limbs: int, first_events: Array, tol: float, compute: Callable) -> Climb:
	var c := Climb.new()
	var ti := TIERS.find(limbs)
	var prev: Array = first_events if not first_events.is_empty() else compute.call(limbs)
	c.tried.append(limbs)
	c.events = prev
	c.limbs = limbs
	while ti + 1 < TIERS.size():
		var nxt: int = TIERS[ti + 1]
		var ev: Array = compute.call(nxt)
		c.tried.append(nxt)
		var d := event_distance(prev, ev)
		c.events = ev
		c.limbs = nxt
		var ratio: float = NumCtx.with_limbs(nxt).eps() / NumCtx.with_limbs(TIERS[ti]).eps()
		c.err = d.map(func(x): return x * ratio)
		if _worst(d) <= tol and not ev.is_empty():
			c.converged = true
			return c
		prev = ev
		ti += 1
	return c

static func _physical_at(limbs: int, defs: Array, player: Array, cursor: Array, plan: Array, max_physical: int,
		flags: Dictionary = {}) -> Array:
	var ctx := NumCtx.with_limbs(limbs)
	return BeamSolver.physical(ctx, BeamScene.from_defs(ctx, defs),
		ctx.cn(player[0], player[1]), ctx.cn(cursor[0], cursor[1]), plan, max_physical, flags)

static func _worst(a: Array) -> float:
	var w := 0.0
	for x in a:
		w = maxf(w, x)
	return w

# Per-event distance between two traces; INF from the first structural
# difference on (different surface, side, kind or length).
static func event_distance(a: Array, b: Array) -> Array:
	var out: Array = []
	var broken := false
	for i in maxi(a.size(), b.size()):
		if broken or i >= a.size() or i >= b.size():
			out.append(INF)
			broken = true
			continue
		var ea: BeamSolver.Event = a[i]
		var eb: BeamSolver.Event = b[i]
		if ea.kind != eb.kind or ea.surface_id != eb.surface_id or ea.left != eb.left or ea.applied != eb.applied:
			out.append(INF)
			broken = true
			continue
		if ea.point == null or eb.point == null:
			out.append(0.0)
			continue
		var dx: float = ea.point.fx() - eb.point.fx()
		var dy: float = ea.point.fy() - eb.point.fy()
		out.append(sqrt(dx * dx + dy * dy))
	return out

# A-posteriori residual estimate for the anchored planned trace: every event
# point must lie on its surface's carrier and, for applied non-portal hits, on
# the next beam (constructed independently). Inconsistency ~ error, since nothing
# propagates. float64 only.
static func planned_residuals(events: Array, scene: BeamScene) -> Array:
	var out: Array = []
	var floor_err := NumCtx.f64().eps() * ErrorEstimate.SCALE * 4.0
	for i in events.size():
		var e: BeamSolver.Event = events[i]
		var res := 0.0
		if e.point != null and e.kind == "hit":
			var s: BeamScene.Surf = scene.by_id[e.surface_id]
			var x := e.point.fx()
			var y := e.point.fy()
			if s.line:
				var dx := s.ex - s.sx
				var dy := s.ey - s.sy
				res = absf(dx * (y - s.sy) - dy * (x - s.sx)) / sqrt(dx * dx + dy * dy)
			else:
				res = absf(sqrt((x - s.cx) * (x - s.cx) + (y - s.cy) * (y - s.cy)) - s.r)
			# (a portal's next beam starts at T(point), elsewhere: no shared point)
			if e.applied and not e.terminal and not e.teleport and i + 1 < events.size():
				var nx: BeamSolver.Event = events[i + 1]
				if nx.raw != null:
					var rb: PackedFloat64Array = nx.raw
					var q := FastSolver.at(rb, FastSolver.param_of(rb, x, y), 0.0)
					res = maxf(res, sqrt((q[0] - x) * (q[0] - x) + (q[1] - y) * (q[1] - y)))
				else:
					var nb: HPBeam = nx.beam
					var qq := nb.at(nb.param_of(e.point))
					res = maxf(res, qq.sub(e.point).abs_val().to_float())
		out.append(maxf(res * 4.0, floor_err))
	return out
