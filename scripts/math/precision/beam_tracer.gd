class_name BeamTracer
extends RefCounted

# Game-facing adapter: runs PrecisionLadder (BeamSolver + dynamic precision)
# and converts its events into TracePath / TraceStep, so the
# renderer, merge, invariant checker, debug dump and arrow flight work
# unchanged.
#
# Applicable to every scene (BeamScene.SUPPORTED_TYPES covers every effect
# type, Stage 83). null only for player = cursor (no line, UX7).

const TOL := 0.01            # px, PHYSICAL-PREVIEW-MATCH tolerance

class Pair extends RefCounted:
	var planned: TracePath
	var physical: TracePath
	var result: PrecisionLadder.Result
	var final_flags: Dictionary = {}   # the physical trace's flags at its end (promoted after a shot)

static var _cache_key: String = ""
static var _cache_pair: Pair = null
static var _scene_key: String = ""
static var _scene: FastSolver.Scene = null
static var _plan_scene_key: String = ""
static var _plan_scene: BeamScene = null

# Capability check per effect type.
static func scene_supported(surfaces: Array) -> bool:
	return BeamScene.supported(BeamScene.defs_from_surfaces(surfaces))

static func plan_items(plan_entries: Array) -> Array:
	var out: Array = []
	for e in plan_entries:
		var c = e.get("click")
		out.append(BeamSolver.PlanItem.make(e.surface_id, e.side == Side.Value.LEFT,
			c if c is Vector2 and is_finite(c.x) and is_finite(c.y) else null))
	return out

# Whether planning `surface_id`'s side after `plan_entries` is refused: inside
# a ghost stretch of the plan (its simulated state is ghosted) only ghost
# togglers can be planned (GAME_SPEC 10.6). The plan starts solid: ghost is
# per shot.
static func plan_refuses(surfaces: Array, plan_entries: Array, surface_id: int, left: bool, flags: Dictionary = {}) -> bool:
	var defs := BeamScene.defs_from_surfaces(surfaces)
	var skey := str(defs)
	if skey != _plan_scene_key or _plan_scene == null:
		_plan_scene = BeamScene.from_defs(NumCtx.f64(), defs)
		_plan_scene_key = skey
	var surf: BeamScene.Surf = _plan_scene.by_id.get(surface_id)
	if surf == null:
		return false
	var start := flags
	if start.has(BeamScene.GHOST):
		start = start.duplicate()
		start.erase(BeamScene.GHOST)
	return surf.ghosted(left, BeamSolver.plan_flags(_plan_scene, plan_items(plan_entries), start))

# Planned + physical traces, or null if the scene is not supported or the
# input is degenerate (player = cursor, a pole / image at infinity).
# flags: the game state's flags (the traces start from them).
static func trace_pair(surfaces: Array, player: Vector2, cursor: Vector2, plan_entries: Array, flags: Dictionary = {}) -> Pair:
	var defs := BeamScene.defs_from_surfaces(surfaces)
	if not BeamScene.supported(defs):
		return null
	if player == cursor:
		return null
	# ghost is per shot: every trace starts solid
	if flags.has(BeamScene.GHOST):
		flags = flags.duplicate()
		flags.erase(BeamScene.GHOST)
	var skey := str(defs)
	var key := "%s|%s|%s|%s|%s" % [var_to_str(player), var_to_str(cursor),
		str(plan_entries.map(func(e): return [e.surface_id, e.side, e.get("click")])), var_to_str(flags), skey]
	if key == _cache_key and _cache_pair != null:
		return _cache_pair
	if skey != _scene_key or _scene == null:
		_scene = FastSolver.Scene.from_defs(defs)
		_scene_key = skey
	var res := PrecisionLadder.solve(defs, [player.x, player.y], [cursor.x, cursor.y],
		plan_items(plan_entries), BeamSolver.MAX_HITS, BeamSolver.MAX_HITS, TOL, false, _scene, flags)
	if res.planned.is_empty() or res.physical.is_empty() or not _finite(res.planned) or not _finite(res.physical):
		return null
	# Identical up to and including the cursor -> one trajectory: share it, so
	# planned and physical post-cursor tails are the same steps (both continue
	# PHYSICAL after the cursor).
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
	pair.final_flags = flags
	for e in res.physical:
		if e.kind == "hit":
			pair.final_flags = e.flags
	# one trajectory from one aim: every step references the same aim ray
	var aim := Ray.from_coords(player, Direction.from_coords(player, cursor))
	pair.physical = to_path(res.physical, res.physical_err, res.physical_tier, by_id, frames, aim, flags)
	if is_same(res.planned, res.physical):
		# one trajectory: the same steps
		pair.planned = TracePath.new()
		pair.planned.steps = pair.physical.steps.duplicate()
		pair.planned.targets_hit = pair.physical.targets_hit.duplicate()
		pair.planned.cursor_index = pair.physical.cursor_index
		pair.planned.hit_count = pair.physical.hit_count
	else:
		pair.planned = to_path(res.planned, res.planned_err, res.planned_tier, by_id, frames, aim, flags)
	_cache_key = key
	_cache_pair = pair
	return pair

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
		if x.kind != y.kind or x.surface_id != y.surface_id or x.left != y.left or x.applied != y.applied or x.state != y.state:
			return false
	return true

static func clear_cache() -> void:
	_cache_key = ""
	_cache_pair = null
	_scene_key = ""
	_scene = null
	_plan_scene_key = ""
	_plan_scene = null

# ---------------------------------------------------------------- conversion

# Steps are in visual space; frame_id identifies the (epoch, transform stack)
# (merge alignment).
static func to_path(events: Array, errs: Array, limbs: int, by_id: Dictionary, frames: Dictionary, aim: Ray = null, flags: Dictionary = {}) -> TracePath:
	var path := TracePath.new()
	var stack: Array = []
	var bits: int = NumCtx.with_limbs(limbs).bits()
	var prev_pt = null
	var gap := false           # the previous event teleported (portal)
	# the world state is part of a frame: a state change starts a new frame
	# (so planned / physical traces that fire different state changes are
	# not aligned, even on identical geometry)
	var init_flags := _flags_key(flags)
	var cur_flags := init_flags
	var ghost: bool = flags.get(BeamScene.GHOST) == true
	for i in events.size():
		var e: BeamSolver.Event = events[i]
		var fid := _frame_id(stack, frames, e.epoch, cur_flags, init_flags)
		# after a portal the arc starts at the exit point (its own t_from)
		var steps := _steps_for(e, fid, null if gap else prev_pt)
		var gap_pending := gap
		gap = false
		if e.point != null:
			prev_pt = Vector2(e.point.fx(), e.point.fy())
		var surf: Surface = by_id.get(e.surface_id)
		var appended := false
		for s in steps:
			# zero-length steps are not emitted; the
			# event's effect still applies
			if s.start == s.end and not is_inf(s.start.x):
				continue
			# the first emitted step after a portal marks the intended gap
			if gap_pending:
				s.after_portal = true
				gap_pending = false
			# a finite step on a curved beam is an arc step (curvature is exact:
			# reflections keep a straight beam exactly straight); the renderer
			# draws near-straight arcs as lines
			if not is_inf(s.start.x) and not is_inf(s.end.x):
				s.is_arc_step = not e.beam.kappa.is_zero()
			s.ray = aim
			s.ghost = ghost
			s.est_error = errs[i] if i < errs.size() else -1.0
			s.precision_bits = bits
			path.steps.append(s)
			appended = true
		var last: TraceStep = path.steps.back() if appended else null
		if e.kind == "hit" and e.applied and not e.terminal:
			if e.projective:
				stack.clear()
			elif e.popped:
				stack.pop_back()
			else:
				stack.append(e.surface_id)
			gap = e.teleport
		if e.kind == "hit" and last != null:
			var side := Side.Value.LEFT if e.left else Side.Value.RIGHT
			last.hit = TraceHit.new(e.t_to.to_float(), last.end, surf.segment if surf else null, side, e.on_segment)
			last.surface_id = e.surface_id
			last.hit_side = side
			last.hit_on_segment = e.on_segment
			if surf and surf.is_target and e.on_segment:
				path.targets_hit[surf.id] = true
		if e.kind == "hit":
			cur_flags = _flags_key(e.flags)
			ghost = e.flags.get(BeamScene.GHOST) == true
		if e.kind == "cursor":
			path.cursor_index = path.steps.size()
		if e.kind == "max":
			break
	path.hit_count = events.size()
	return path

# frame id per (epoch, transform stack, flags): a projective starts a new
# epoch, and a state change a new frame
static func _frame_id(stack: Array, frames: Dictionary, epoch: int = 0, flags_key: String = "", init_key: String = "") -> int:
	if stack.is_empty() and epoch == 0 and flags_key == init_key:
		return TraceStep.IDENTITY_FRAME
	var key := "%d|%s|%s" % [epoch, ",".join(stack.map(func(x): return str(x))), flags_key]
	if not frames.has(key):
		frames[key] = {"id": frames.size() + 1}
	return frames[key]["id"]

# One event = the arc from its t_from to t_to on its beam. Arcs that wrap
# through t = infinity on a straight beam are split at infinity.
# prev_pt: the previous event's point -- the arc's start, exactly (never
# re-evaluated from t_from, which can sit at the parametrization's singular
# point, t -> infinity, after a provenance resume).
# Canonical key of a flags dictionary (sorted keys, JSON spelling of values).
static func _flags_key(flags: Dictionary) -> String:
	var keys := flags.keys()
	keys.sort()
	return ",".join(keys.map(func(k): return "%s=%s" % [str(k), StateChange.flag_key(flags[k])]))

static func _steps_for(e: BeamSolver.Event, fid: int, prev_pt = null) -> Array:
	var b: HPBeam = e.beam
	var bf := FastSolver.beam(b.p.fx(), b.p.fy(), b.tau.fx(), b.tau.fy(), b.kappa.to_float())
	var tf: float = e.t_from.to_float()
	var start: Vector2
	if prev_pt != null:
		start = prev_pt
	else:
		var h0 := FastSolver.at_view(bf, tf)
		start = Vector2(h0[0], h0[1])
	var is_line: bool = bf[FastSolver.K] == 0.0
	# Steps to/from infinity (escape-step convention): `via` holds the travel
	# direction, not a point.
	if e.t_to == null:
		# escape to infinity along a straight beam
		var tg := FastSolver.tangent_at(bf, tf, 0.0)
		var s := TraceStep.new(start, Vector2(INF, INF), fid, null, _ray(start, tg), Vector2(tg[0], tg[1]), false)
		return [s]
	var tt: float = e.t_to.to_float()
	var end := Vector2(e.point.fx(), e.point.fy())
	var via: Vector2
	if not is_line and (is_inf(tt) or is_inf(tf)):
		# an end at the antipode (t = inf): any parameter strictly between the
		# ends is on the arc
		var span := 4.0 / absf(bf[FastSolver.K])
		var tm: float = (maxf(tf, 0.0) + span) if is_inf(tt) else (minf(tt, 0.0) - span)
		var m := FastSolver.at_view(bf, tm)
		via = Vector2(m[0], m[1])
	elif tt > tf or (tt == tf):
		var m := FastSolver.at_view(bf, (tf + tt) * 0.5)
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
			return [TraceStep.new(start, end, fid, null, null, via, true)]
		return _through_infinity(bf, tf, tt, start, end, fid)
	if tt > tf or (tt == tf):
		return [TraceStep.new(start, end, fid, null, null, via, false)]
	return _through_infinity(bf, tf, tt, start, end, fid)

# start -> infinity along the start tangent, infinity -> end along the end
# tangent (escape-step convention: `via` holds the direction).
static func _through_infinity(bf: PackedFloat64Array, tf: float, tt: float, start: Vector2, end: Vector2, fid: int) -> Array:
	var tg0 := FastSolver.tangent_at(bf, tf, 0.0)
	var out_step := TraceStep.new(start, Vector2(INF, INF), fid, null, _ray(start, tg0), Vector2(tg0[0], tg0[1]), false)
	var tg1 := FastSolver.tangent_at(bf, tt, 0.0)
	var in_step := TraceStep.new(Vector2(INF, INF), end, fid, null, _ray(end, tg1), -Vector2(tg1[0], tg1[1]), false)
	return [out_step, in_step]

static func _ray(origin: Vector2, tg: PackedFloat64Array) -> Ray:
	var d := Direction.from_coords(origin, origin + Vector2(tg[0], tg[1]) * 100.0)
	return Ray.from_coords(origin, d)
