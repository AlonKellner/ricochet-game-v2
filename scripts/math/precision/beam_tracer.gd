class_name BeamTracer
extends RefCounted

# Game-facing adapter: runs PrecisionLadder (BeamSolver + dynamic precision)
# and converts its events into Tracer.TracedPath / Tracer.Step, so the
# renderer, merge, invariant checker, debug dump and arrow flight work
# unchanged.
#
# Applicable to scenes made of full-circle inversion surfaces (same inversion
# on both sides) plus effect-free surfaces (which never change the beam and
# are ignored). Anything else returns null from `circle_defs_for` and callers
# fall back to Tracer.

const TOL := 0.01            # px, PHYSICAL-PREVIEW-MATCH tolerance
const POST_CURSOR_EVENTS := 8
const MAX_PHYSICAL_EVENTS := 24

class Pair extends RefCounted:
	var planned: Tracer.TracedPath
	var physical: Tracer.TracedPath
	var result: PrecisionLadder.Result

static var _cache_key: String = ""
static var _cache_pair: Pair = null

# [[id, cx, cy, r], ...] or null if the scene is not supported.
static func circle_defs_for(surfaces: Array, plan_entries: Array) -> Variant:
	var defs: Array = []
	var ids := {}
	var state := GameState.new()
	for surf in surfaces:
		var l: SideConfig = surf.active_side_config(Side.Value.LEFT, state)
		var r: SideConfig = surf.active_side_config(Side.Value.RIGHT, state)
		var le: Effect = l.effect if l != null else null
		var re: Effect = r.effect if r != null else null
		if le == null and re == null:
			continue
		if not (le is CircleInversionEffect and re is CircleInversionEffect and surf.segment.full):
			return null
		var carrier: GeneralizedCircle = surf.segment.get_carrier()
		if carrier.is_line():
			return null
		# center() returns a float32 Vector2; recompute in float64
		var cx: float = -carrier.b / (2.0 * carrier.a)
		var cy: float = -carrier.c / (2.0 * carrier.a)
		defs.append([surf.id, cx, cy, carrier.radius()])
		ids[surf.id] = true
	for e in plan_entries:
		if not ids.has(e.surface_id):
			return null
	return defs

# STUB (Stage 81 red): capability check per effect type
static func scene_supported(surfaces: Array) -> bool:
	return circle_defs_for(surfaces, []) != null

static func plan_items(plan_entries: Array) -> Array:
	var out: Array = []
	for e in plan_entries:
		out.append(BeamSolver.PlanItem.make(e.surface_id, e.side == Side.Value.LEFT))
	return out

# Planned + physical traces, or null if the scene is not supported.
static func trace_pair(surfaces: Array, player: Vector2, cursor: Vector2, plan_entries: Array) -> Pair:
	var defs = circle_defs_for(surfaces, plan_entries)
	if defs == null:
		return null
	if _degenerate(defs, player, cursor):
		return null
	var key := "%s|%s|%s|%s" % [var_to_str(player), var_to_str(cursor),
		str(plan_entries.map(func(e): return [e.surface_id, e.side])), str(defs)]
	if key == _cache_key and _cache_pair != null:
		return _cache_pair
	var res := PrecisionLadder.solve(defs, [player.x, player.y], [cursor.x, cursor.y],
		plan_items(plan_entries), MAX_PHYSICAL_EVENTS, MAX_PHYSICAL_EVENTS, TOL)
	if res.planned.is_empty() or res.physical.is_empty() or not _finite(res.planned) or not _finite(res.physical):
		return null
	# Identical up to and including the cursor -> one trajectory: share it, so
	# planned and physical post-cursor tails are the same steps (Tracer runs
	# both with the same post-cursor mode, so its tails coincide too).
	var ci := _cursor_event(res.planned)
	if ci >= 0 and _same_prefix(res.planned, res.physical, ci + 1):
		res.planned = res.physical
		res.planned_err = res.physical_err
		res.planned_tier = res.physical_tier
	var by_id := {}
	for s in surfaces:
		by_id[s.id] = s
	var frames := {}
	var pair := Pair.new()
	pair.result = res
	# one trajectory from one aim: every step references the same aim ray
	var aim := Ray.from_coords(player, Direction.from_coords(player, cursor))
	pair.planned = to_path(res.planned, res.planned_err, res.planned_tier, by_id, frames, aim)
	pair.physical = to_path(res.physical, res.physical_err, res.physical_tier, by_id, frames, aim)
	_cache_key = key
	_cache_pair = pair
	return pair

# Inputs the beam model does not define: player == cursor, or player/cursor
# exactly at an inversion center (its image is the point at infinity).
static func _degenerate(defs: Array, player: Vector2, cursor: Vector2) -> bool:
	if player == cursor:
		return true
	for d in defs:
		var c := Vector2(d[1], d[2])
		if player == c or cursor == c:
			return true
	return false

static func _finite(events: Array) -> bool:
	for e in events:
		if e.point != null:
			var x: float = e.point.fx()
			var y: float = e.point.fy()
			if is_nan(x) or is_nan(y) or is_inf(x) or is_inf(y):
				return false
	return true

static func _cursor_event(events: Array) -> int:
	for i in events.size():
		if events[i].kind == "cursor":
			return i
	return -1

static func _same_prefix(a: Array, b: Array, n: int) -> bool:
	if a.size() < n or b.size() < n:
		return false
	for i in n:
		var x: BeamSolver.Event = a[i]
		var y: BeamSolver.Event = b[i]
		if x.kind != y.kind or x.surface_id != y.surface_id or x.left != y.left or x.applied != y.applied:
			return false
	return true

static func clear_cache() -> void:
	_cache_key = ""
	_cache_pair = null

# ---------------------------------------------------------------- conversion

# Steps carry no normalized frame (frame = null): the beam model never maps
# back to Tracer's normalized coordinates, which collapse numerically for deep
# inversion chains. frame_id identifies the transform stack (merge alignment).
static func to_path(events: Array, errs: Array, limbs: int, by_id: Dictionary, frames: Dictionary, aim: Ray = null) -> Tracer.TracedPath:
	var path := Tracer.TracedPath.new()
	var stack: Array = []
	var bits: int = NumCtx.with_limbs(limbs).bits()
	var prev_pt = null
	for i in events.size():
		var e: BeamSolver.Event = events[i]
		var fid := _frame_id(stack, frames)
		var steps := _steps_for(e, fid, null, prev_pt)
		if e.point != null:
			prev_pt = Vector2(e.point.fx(), e.point.fy())
		var surf: Surface = by_id.get(e.surface_id)
		var appended := false
		for s in steps:
			# zero-length steps are not emitted (Tracer skips them too); the
			# event's effect still applies
			if s.start == s.end and not is_inf(s.start.x):
				continue
			# like Tracer: every finite step after an inversion is an arc step
			# (inversions map lines to circles); the renderer draws
			# near-straight ones as lines
			if not is_inf(s.start.x) and not is_inf(s.end.x):
				s.is_arc_step = not stack.is_empty()
			s.ray = aim
			s.est_error = errs[i] if i < errs.size() else -1.0
			s.precision_bits = bits
			path.steps.append(s)
			appended = true
		var last: Tracer.Step = path.steps.back() if appended else null
		if e.kind == "hit" and e.applied:
			if not stack.is_empty() and stack.back() == e.surface_id:
				stack.pop_back()
			else:
				stack.append(e.surface_id)
		if e.kind == "hit" and last != null:
			var side := Side.Value.LEFT if e.left else Side.Value.RIGHT
			last.hit = Intersection.HitRecord.new(e.t_to.to_float(), last.end, surf.segment if surf else null, side, true)
			last.surface_id = e.surface_id
			last.hit_side = side
			last.hit_on_segment = true
			if surf and surf.is_target:
				path.targets_hit[surf.id] = true
		if e.kind == "cursor":
			path.cursor_index = path.steps.size()
	path.hit_count = events.size()
	return path

static func _frame_id(stack: Array, frames: Dictionary) -> int:
	if stack.is_empty():
		return MobiusTransform.IDENTITY_ID
	var key := ",".join(stack.map(func(x): return str(x)))
	if not frames.has(key):
		frames[key] = {"id": frames.size() + 1}
	return frames[key]["id"]

# One event = the arc from its t_from to t_to on its beam. Arcs that wrap
# through t = infinity on a straight beam are split at infinity.
# prev_pt: the previous event's point -- the arc's start, exactly (never
# re-evaluated from t_from, which can sit at the parametrization's singular
# point, t -> infinity, after a provenance resume).
static func _steps_for(e: BeamSolver.Event, fid: int, frame: MobiusTransform, prev_pt = null) -> Array:
	var b: HPBeam = e.beam
	var bf := FastSolver.beam(b.p.fx(), b.p.fy(), b.tau.fx(), b.tau.fy(), b.kappa.to_float())
	var tf: float = e.t_from.to_float()
	var start: Vector2
	if prev_pt != null:
		start = prev_pt
	else:
		var h0 := FastSolver.at(bf, tf, 0.0)
		start = Vector2(h0[0], h0[1])
	var is_line: bool = bf[FastSolver.K] == 0.0
	# Steps to/from infinity follow Tracer's escape-step convention
	# (_add_escape_steps): `via` holds the travel direction, not a point.
	if e.t_to == null:
		# escape to infinity along a straight beam
		var tg := FastSolver.tangent_at(bf, tf, 0.0)
		var s := Tracer.Step.new(start, Vector2(INF, INF), fid, null, _ray(start, tg), frame, Vector2(tg[0], tg[1]), false)
		return [s]
	var tt: float = e.t_to.to_float()
	var end := Vector2(e.point.fx(), e.point.fy())
	var via: Vector2
	if tt > tf or (tt == tf):
		var m := FastSolver.at(bf, (tf + tt) * 0.5, 0.0)
		via = Vector2(m[0], m[1])
	elif not is_line:
		# wraps through t = infinity: the arc passes the antipode of p
		var inf_pt = b.at_infinity()
		via = Vector2(inf_pt.fx(), inf_pt.fy())
	if not is_line:
		# The renderer draws arcs it cannot draw as arcs (radius above
		# VisualConverter.MAX_ARC_RADIUS) as the chord start -> end. That is only
		# the path when it runs between the endpoints; when it goes the long way
		# round (via on the far arc: the inscribed angle at via is acute) the
		# chord is the OTHER arc. Draw it like a straight beam wrapping through
		# infinity instead: out along the exact start tangent, back in along the
		# exact end tangent.
		if VisualConverter.is_arc(start, via, end) or (start - via).dot(end - via) <= 0.0:
			return [Tracer.Step.new(start, end, fid, null, null, frame, via, true)]
		return _through_infinity(bf, tf, tt, start, end, fid, frame)
	if tt > tf or (tt == tf):
		return [Tracer.Step.new(start, end, fid, null, null, frame, via, false)]
	return _through_infinity(bf, tf, tt, start, end, fid, frame)

# start -> infinity along the start tangent, infinity -> end along the end
# tangent (Tracer's escape-step convention: `via` holds the direction).
static func _through_infinity(bf: PackedFloat64Array, tf: float, tt: float, start: Vector2, end: Vector2, fid: int, frame: MobiusTransform) -> Array:
	var tg0 := FastSolver.tangent_at(bf, tf, 0.0)
	var out_step := Tracer.Step.new(start, Vector2(INF, INF), fid, null, _ray(start, tg0), frame, Vector2(tg0[0], tg0[1]), false)
	var tg1 := FastSolver.tangent_at(bf, tt, 0.0)
	var in_step := Tracer.Step.new(Vector2(INF, INF), end, fid, null, _ray(end, tg1), frame, -Vector2(tg1[0], tg1[1]), false)
	return [out_step, in_step]

static func _ray(origin: Vector2, tg: PackedFloat64Array) -> Ray:
	var d := Direction.from_coords(origin, origin + Vector2(tg[0], tg[1]) * 100.0)
	return Ray.from_coords(origin, d)
