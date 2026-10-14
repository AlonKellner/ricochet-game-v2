class_name InvariantChecker
extends RefCounted

var _scene: Node
var _player: CharacterBody2D
var _cursor: Node2D
var _renderer: Node2D
var _game_mgr: Node
var _plan: PlanManager = null

func setup(scene: Node) -> void:
	_scene = scene
	_player = scene.get_node_or_null("Player")
	_cursor = scene.get_node_or_null("Cursor")
	_renderer = scene.get_node_or_null("PathRenderer")
	_game_mgr = scene.get_node_or_null("GameManager")
	# the plan the renderer reads: the GameManager's, or the checker's own
	# (scenes without a GameManager)
	if _game_mgr and "plan" in _game_mgr:
		_plan = _game_mgr.plan
	elif _renderer and "plan_override" in _renderer:
		_plan = PlanManager.new()
		_renderer.plan_override = _plan
	_defs_cache = []
	_scene_cache = null

func check_all(player_pos: Vector2, cursor_pos: Vector2, plan_entries: Array = []) -> Array[String]:
	var violations: Array[String] = []
	_position_nodes(player_pos, cursor_pos)
	var plan_applied := false
	if _plan != null:
		_plan.clear()
		for entry in plan_entries:
			_plan.add_entry(entry.surface_id, entry.side)
		plan_applied = plan_entries.size() > 0
	if _renderer:
		_renderer._compute_trace()
	violations.append_array(check_UX7(player_pos, cursor_pos))
	violations.append_array(check_PREVIEW_NOGAPS(player_pos, cursor_pos))
	violations.append_array(check_S16(player_pos, cursor_pos))
	violations.append_array(check_GREEN_FROM_PLAYER(player_pos, cursor_pos))
	violations.append_array(check_ORIGIN_NOT_REHIT(player_pos, cursor_pos))
	violations.append_array(check_SINGLE_DIVERGENCE(player_pos, cursor_pos))
	violations.append_array(check_NO_POST_CURSOR_DIVERGENCE(player_pos, cursor_pos))
	violations.append_array(check_PHYSICAL_PREVIEW_MATCH(player_pos, cursor_pos))
	violations.append_array(check_PHYSICAL_CONTINUITY(player_pos, cursor_pos))
	violations.append_array(check_SOLID_PATH_TO_CURSOR(player_pos, cursor_pos))
	violations.append_array(check_TRACE_ENDS_AT_SURFACE_OR_BOUNDS(player_pos, cursor_pos))
	if plan_applied:
		violations.append_array(check_PLAN_EFFECTS_APPLIED(player_pos, cursor_pos, plan_entries))
	violations.append_array(check_PHYSICS_COMPLIANCE(player_pos, cursor_pos))
	violations.append_array(check_HITPOINT_ON_CARRIER(player_pos, cursor_pos))
	violations.append_array(check_ON_SEGMENT_CONSISTENCY(player_pos, cursor_pos))
	# beam-solver events (Stage 83): the legacy frame checks (S18,
	# BACK_TRANSFORM, RAY_ALIGNMENT, ARC_MIDPOINT, PARAM_MONOTONICITY,
	# POST_INVERSION_ARC, SHARED_RAY) have no beam counterpart and are retired
	violations.append_array(check_EFFECT_LAW(player_pos, cursor_pos))
	violations.append_array(check_EVENT_ON_BEAM(player_pos, cursor_pos))
	violations.append_array(check_BEAM_ORDER(player_pos, cursor_pos))
	violations.append_array(check_VIA_ON_ARC(player_pos, cursor_pos))
	violations.append_array(check_VISUAL_ON_PHYSICAL_CARRIER(player_pos, cursor_pos))
	if plan_applied:
		violations.append_array(check_DIRECTION_ONLY(player_pos, cursor_pos, plan_entries))
	return violations

func check_UX7(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer:
		return violations
	if player_pos != cursor_pos:
		if not _renderer.has_line():
			violations.append("UX7: No line when cursor != player")
		else:
			if _renderer.get_line_from() != player_pos:
				violations.append("UX7: Line start %s != player %s" % [_renderer.get_line_from(), player_pos])
			# Direction check only valid for empty plan — with a plan, aim is back-propagated
			var has_plan: bool = _plan != null and not _plan.is_empty()
			if not has_plan:
				var expected_dir := (cursor_pos - player_pos).normalized()
				var actual_dir: Vector2 = _renderer.get_line_direction()
				if expected_dir.dot(actual_dir) < 0.99:
					violations.append("UX7: Direction %s not toward cursor %s" % [actual_dir, expected_dir])
	else:
		if _renderer.has_line():
			violations.append("UX7: Line present when cursor == player")
	return violations

func check_PREVIEW_NOGAPS(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var path: TracePath = _renderer.get_traced_path()
	if path == null:
		return violations
	for i in range(1, path.steps.size()):
		var prev: TraceStep = path.steps[i - 1]
		var curr: TraceStep = path.steps[i]
		if prev.hit == null or curr.hit == null:
			continue
		var gap := prev.end.distance_to(curr.start)
		var tol := 1.5 + 0.001 * i
		if gap > tol:
			if _is_infinity_gap(prev.end, curr.start):
				continue
			if curr.after_portal:
				continue
			violations.append("NOGAPS: Gap between step %d end=%s and step %d start=%s" % [i - 1, prev.end, i, curr.start])
	return violations

static func _is_infinity_gap(prev_end: Vector2, curr_start: Vector2) -> bool:
	const HUGE := 10000.0
	var prev_huge := absf(prev_end.x) > HUGE or absf(prev_end.y) > HUGE
	var curr_huge := absf(curr_start.x) > HUGE or absf(curr_start.y) > HUGE
	if prev_huge or curr_huge:
		return true
	const EDGE_TOL := 2.0
	const MIN_GAP := 500.0
	var gap := prev_end.distance_to(curr_start)
	if gap < MIN_GAP:
		return false
	var prev_at_edge := (absf(prev_end.x) < EDGE_TOL or absf(prev_end.x - 1920.0) < EDGE_TOL
		or absf(prev_end.y) < EDGE_TOL or absf(prev_end.y - 1080.0) < EDGE_TOL)
	var curr_at_edge := (absf(curr_start.x) < EDGE_TOL or absf(curr_start.x - 1920.0) < EDGE_TOL
		or absf(curr_start.y) < EDGE_TOL or absf(curr_start.y - 1080.0) < EDGE_TOL)
	return prev_at_edge or curr_at_edge

func check_S16(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var path: TracePath = _renderer.get_traced_path()
	if path == null:
		return violations
	for i in path.steps.size():
		var step: TraceStep = path.steps[i]
		if is_nan(step.start.x) or is_nan(step.start.y):
			violations.append("S16: NaN in step %d start" % i)
		if is_nan(step.end.x) or is_nan(step.end.y):
			violations.append("S16: NaN in step %d end" % i)
	return violations

func check_GREEN_FROM_PLAYER(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var typed: Array = _renderer.get_typed_steps()
	if typed.size() == 0:
		return violations
	var first: TraceStep = typed[0]
	if first.type != StepTypes.Type.ALIGNED and first.type != StepTypes.Type.ALIGNED_POST_PLANNED:
		violations.append("GREEN-FROM-PLAYER: First step type=%d, expected green" % first.type)
	return violations

func check_ORIGIN_NOT_REHIT(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var path: TracePath = _renderer.get_traced_path()
	if path == null:
		return violations
	for i in path.steps.size():
		var step: TraceStep = path.steps[i]
		if step.start == step.end:
			if step.hit != null and step.hit.t > 0.0:
				violations.append("ORIGIN-NOT-REHIT: Zero-length step %d at %s" % [i, step.start])
	return violations

func check_SINGLE_DIVERGENCE(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var typed: Array = _renderer.get_typed_steps()
	var diverged := false
	for i in typed.size():
		var ms: TraceStep = typed[i]
		var is_aligned := (ms.type == StepTypes.Type.ALIGNED or ms.type == StepTypes.Type.ALIGNED_POST_PLANNED)
		var is_diverged := not is_aligned
		if is_diverged:
			diverged = true
		elif diverged and is_aligned:
			violations.append("SINGLE-DIVERGENCE: Re-convergence at step %d after divergence" % i)
	return violations

func check_NO_POST_CURSOR_DIVERGENCE(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var typed: Array = _renderer.get_typed_steps()
	if typed.size() == 0:
		return violations
	var all_pre_cursor_aligned := true
	var past_cursor := false
	for i in typed.size():
		var ms: TraceStep = typed[i]
		if ms.type == StepTypes.Type.ALIGNED:
			continue
		if ms.type == StepTypes.Type.ALIGNED_POST_PLANNED:
			past_cursor = true
			continue
		if not past_cursor:
			all_pre_cursor_aligned = false
			break
		if all_pre_cursor_aligned:
			violations.append("NO-POST-CURSOR-DIVERGENCE: step %d type=%d diverges after fully-aligned path to cursor" % [i, ms.type])
			break
	return violations

func check_PHYSICAL_PREVIEW_MATCH(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var typed: Array = _renderer.get_typed_steps()
	if typed.size() == 0:
		return violations
	# The physical trace from the renderer IS the arrow path (both use the same trace)
	var physical: TracePath = _renderer.get_traced_path()
	if physical == null:
		return violations
	# Extract non-red preview steps
	var non_red: Array = []
	for i in typed.size():
		var ms: TraceStep = typed[i]
		if ms.type == StepTypes.Type.ALIGNED or ms.type == StepTypes.Type.ALIGNED_POST_PLANNED or ms.type == StepTypes.Type.DIVERGED_PHYSICAL:
			non_red.append(ms)
	if non_red.size() != physical.steps.size():
		violations.append("PHYSICAL-PREVIEW-MATCH: non-red count=%d != physical count=%d" % [non_red.size(), physical.steps.size()])
		return violations
	var portal_seen := false
	for i in non_red.size():
		var ms: TraceStep = non_red[i]
		var ps: TraceStep = physical.steps[i]
		if ps.after_portal or ms.after_portal:
			portal_seen = true
		if portal_seen:
			continue
		if ms.start.distance_to(ps.start) > 0.01:
			violations.append("PHYSICAL-PREVIEW-MATCH: step %d start mismatch: preview=%s physical=%s" % [i, ms.start, ps.start])
		if ms.end.distance_to(ps.end) > 0.01:
			violations.append("PHYSICAL-PREVIEW-MATCH: step %d end mismatch: preview=%s physical=%s" % [i, ms.end, ps.end])
	return violations

func check_PHYSICAL_CONTINUITY(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var physical: TracePath = _renderer.get_traced_path()
	if physical == null or physical.steps.size() < 2:
		return violations
	for i in range(1, physical.steps.size()):
		var prev: TraceStep = physical.steps[i - 1]
		var curr: TraceStep = physical.steps[i]
		# steps without a hit (cursor / escape pieces) bound no contact
		if prev.hit == null:
			continue
		var gap := prev.end.distance_to(curr.start)
		var tol := 1.5 + 0.001 * i
		if gap > tol:
			if _is_infinity_gap(prev.end, curr.start):
				continue
			if curr.after_portal:
				continue
			violations.append("PHYSICAL-CONTINUITY: gap between step %d end=%s and step %d start=%s" % [i - 1, prev.end, i, curr.start])
	return violations

func check_SOLID_PATH_TO_CURSOR(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var typed: Array = _renderer.get_typed_steps()
	if typed.size() == 0:
		return violations
	var solid_steps: Array = []
	for i in typed.size():
		var ms: TraceStep = typed[i]
		if StepTypes.is_solid(ms.type):
			solid_steps.append(ms)
	if solid_steps.size() == 0:
		return violations
	var first: TraceStep = solid_steps[0]
	if first.start != player_pos:
		violations.append("SOLID-PATH-TO-CURSOR: first solid step starts at %s, not player %s" % [first.start, player_pos])
	var bounds := VisualConverter.DEFAULT_BOUNDS
	for i in range(1, solid_steps.size()):
		var prev: TraceStep = solid_steps[i - 1]
		var curr: TraceStep = solid_steps[i]
		if prev.end.distance_to(curr.start) > 0.05 and not curr.after_portal:
			# Skip gaps at escape/return boundaries (ray wraps through infinity)
			# and portal teleports (intended gaps)
			var at_bounds := (prev.end.x <= bounds.position.x + 1.0 or prev.end.x >= bounds.end.x - 1.0 or
				prev.end.y <= bounds.position.y + 1.0 or prev.end.y >= bounds.end.y - 1.0)
			if not at_bounds:
				violations.append("SOLID-PATH-TO-CURSOR: gap between solid step %d end=%s and step %d start=%s" % [i - 1, prev.end, i, curr.start])
	return violations

func check_TRACE_ENDS_AT_SURFACE_OR_BOUNDS(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var bounds := VisualConverter.DEFAULT_BOUNDS
	var surfaces: Array = []
	var parent := _renderer.get_parent()
	if parent and "surfaces" in parent:
		surfaces = parent.surfaces

	var _traces := _get_named_traces()
	for trace_name in _traces:
		var path: TracePath = _traces[trace_name]
		if path.steps.size() == 0:
			continue
		var last: TraceStep = path.steps[path.steps.size() - 1]
		var end_pos: Vector2 = last.end
		var valid := _cut_by_max_hits(trace_name)
		if end_pos.distance_to(player_pos) < 2.0:
			valid = true
		if end_pos.x <= bounds.position.x + 2.0 or end_pos.x >= bounds.end.x - 2.0:
			valid = true
		if end_pos.y <= bounds.position.y + 2.0 or end_pos.y >= bounds.end.y - 2.0:
			valid = true
		for surf in surfaces:
			var s: Surface = surf
			var dist := _point_to_segment_dist(end_pos, s.segment.start.coords, s.segment.end.coords)
			if dist < 2.0:
				valid = true
				break
		# Also check if endpoint is on any surface's carrier (off-segment but on carrier line)
		if not valid:
			for surf in surfaces:
				var s: Surface = surf
				var carrier := s.segment.get_carrier()
				var eval_val := carrier.evaluate(end_pos)
				var grad_mag := sqrt(carrier.b * carrier.b + carrier.c * carrier.c + 4.0 * carrier.a * carrier.a * (end_pos.x * end_pos.x + end_pos.y * end_pos.y))
				var dist_to_carrier := absf(eval_val) / maxf(grad_mag, 1e-10)
				if dist_to_carrier < 2.0:
					valid = true
					break
		if not valid:
			violations.append("TRACE-ENDS: %s trace ends mid-air at %s" % [trace_name, end_pos])
	return violations

# The solver cut this trace at MAX_HITS: it ends where the cut is drawn (the
# next crossing, or the cursor), which need not be on a surface.
func _cut_by_max_hits(trace_name: String) -> bool:
	var r = _beam_result()
	if r == null:
		return false
	var ev: Array = r.planned if trace_name.to_lower().contains("plan") else r.physical
	return not ev.is_empty() and ev.back().kind == "max"

func check_PLAN_EFFECTS_APPLIED(player_pos: Vector2, cursor_pos: Vector2, plan_entries: Array) -> Array[String]:
	var r = _beam_result()
	if r == null or player_pos == cursor_pos or plan_entries.size() == 0:
		return [] as Array[String]
	var reached := false
	for e in r.planned:
		if e.kind == "cursor":
			reached = true
	if not reached:
		return [] as Array[String]
	var sc := _beam_scene()
	var all_items: Array = BeamTracer.plan_items(plan_entries)
	var effs: Array = BeamSolver.plan_effects(sc, all_items, _flags())
	var items: Array = []
	for j in all_items.size():
		if effs[j] in BeamScene.TRANSFORMATIVE():
			items.append(all_items[j])
	return check_plan_effects_applied_events(r.planned, items, true)

func check_PHYSICS_COMPLIANCE(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var path: TracePath = _renderer.get_traced_path()
	if path == null or path.steps.size() == 0:
		return violations
	var surfaces: Array = []
	var parent := _renderer.get_parent()
	if parent and "surfaces" in parent:
		surfaces = parent.surfaces
	if surfaces.size() == 0:
		return violations
	var check_limit: int = path.cursor_index if path.cursor_index >= 0 else path.steps.size()
	check_limit = mini(check_limit, path.steps.size())
	var portal_seen := false
	for i in check_limit:
		var step: TraceStep = path.steps[i]
		if step.after_portal:
			portal_seen = true
		if portal_seen:
			continue
		if step.is_arc_step:
			continue
		if step.start == step.end:
			continue
		if step.frame_id != TraceStep.IDENTITY_FRAME:
			continue
		for surf in surfaces:
			var s: Surface = surf
			var config_l := s.active_side_config(Side.Value.LEFT, GameState.new(_flags()))
			var config_r := s.active_side_config(Side.Value.RIGHT, GameState.new(_flags()))
			var has_effect := false
			if config_l != null and config_l.effect != null and config_l.effect.is_transformative():
				has_effect = true
			if config_r != null and config_r.effect != null and config_r.effect.is_transformative():
				has_effect = true
			if s.player_solid:
				has_effect = true
			if not has_effect:
				continue
			if not s.segment.get_carrier().is_line():
				continue
			var sa := s.segment.start.coords
			var sb := s.segment.end.coords
			var ix := _segment_intersection(step.start, step.end, sa, sb)
			if ix.is_empty():
				continue
			var pt: Vector2 = ix["point"]
			if pt.distance_to(step.start) < 1.0 or pt.distance_to(step.end) < 1.0:
				continue
			if pt.distance_to(sa) < 1.0 or pt.distance_to(sb) < 1.0:
				continue
			violations.append("PHYSICS-COMPLIANCE: step %d crosses surface %d at %s" % [i, s.id, pt])
	return violations

func check_HITPOINT_ON_CARRIER(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var r = _beam_result()
	if r == null or player_pos == cursor_pos:
		return [] as Array[String]
	var sc := _beam_scene()
	var out: Array[String] = []
	for events in [r.planned, r.physical]:
		out.append_array(_carrier_violations(events, sc))
	return out

func check_ON_SEGMENT_CONSISTENCY(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var r = _beam_result()
	if r == null or player_pos == cursor_pos:
		return [] as Array[String]
	var sc := _beam_scene()
	var out: Array[String] = []
	for events in [r.planned, r.physical]:
		for i in events.size():
			var e: BeamSolverEvent = events[i]
			if e.kind != "hit" or e.point == null:
				continue
			var s: BeamSceneSurf = sc.by_id[e.surface_id]
			var p := CN.make(F64Num.of(e.point.fx()), F64Num.of(e.point.fy()))
			if BeamGeom.on_segment(s.seg, p) == e.on_segment:
				continue
			# a higher-tier point rounded onto the other side of an endpoint
			var near := minf(Vector2(p.fx() - s.sx, p.fy() - s.sy).length(), Vector2(p.fx() - s.ex, p.fy() - s.ey).length())
			if near > 1e-9 * maxf(1.0, Vector2(p.fx(), p.fy()).length()):
				out.append("ON-SEGMENT-CONSISTENCY: event %d s=%d stored=%s recomputed=%s at (%s, %s)" % [i, e.surface_id, e.on_segment, not e.on_segment, p.fx(), p.fy()])
	return out

func check_VIA_ON_ARC(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var _traces := _get_named_traces()
	for trace_name in _traces:
		var path: TracePath = _traces[trace_name]
		for i in path.steps.size():
			var step: TraceStep = path.steps[i]
			if not step.is_arc_step:
				continue
			if not VisualConverter.is_arc(step.start, step.via, step.end):
				continue
			var p := VisualConverter.arc_params(step.start, step.via, step.end)
			var ctr: Vector2 = p["center"]
			var sa: float = p["start_angle"]
			var span: float = p["span"]
			var cw: bool = p["clockwise"]
			var via_angle := (step.via - ctr).angle()
			var diff: float
			if cw:
				diff = fposmod(sa - via_angle, TAU)
			else:
				diff = fposmod(via_angle - sa, TAU)
			if diff > span + 0.01:
				violations.append("VIA_ON_ARC: %s step %d via not on drawn arc (diff=%.3f, span=%.3f)" %
					[trace_name, i, diff, span])
	return violations

func check_VISUAL_ON_PHYSICAL_CARRIER(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var surfaces: Array = []
	var parent := _renderer.get_parent()
	if parent and "surfaces" in parent:
		surfaces = parent.surfaces
	if surfaces.size() == 0:
		return violations
	var bounds := VisualConverter.DEFAULT_BOUNDS
	var _traces := _get_named_traces()
	for trace_name in _traces:
		var path: TracePath = _traces[trace_name]
		for i in path.steps.size():
			var step: TraceStep = path.steps[i]
			if step.hit == null or step.hit.segment == null:
				continue
			var end_pos := step.end
			if is_inf(end_pos.x) or is_inf(end_pos.y):
				continue
			if _is_at_bounds(end_pos, bounds):
				continue
			var on_carrier := false
			for surf in surfaces:
				var s: Surface = surf
				if _geometric_carrier_dist(end_pos, s.segment.get_carrier()) < 5.0:
					on_carrier = true
					break
			if not on_carrier:
				if not step.hit_on_segment:
					continue
				if step.frame_id != TraceStep.IDENTITY_FRAME and step.hit.segment != null:
					var nc: GeneralizedCircle = step.hit.segment.get_carrier()
					var norm_err := _geometric_carrier_dist(step.hit.point.coords, nc)
					if norm_err < 1.0:
						continue
				violations.append("VISUAL-ON-CARRIER: %s step %d visual endpoint %s not on any physical carrier" % [trace_name, i, end_pos])
	return violations

# The physical trace depends only on the aim direction: the planned shot's
# physical hits equal those of a shot without plan aimed at the cursor's
# image (beam solver, converged; projective plans until Stage 87 are skipped).
func check_DIRECTION_ONLY(player_pos: Vector2, cursor_pos: Vector2, plan_entries: Array) -> Array[String]:
	var out: Array[String] = []
	var r = _beam_result()
	if plan_entries.size() == 0 or r == null or player_pos == cursor_pos:
		return out
	var sc := _beam_scene()
	var ctx := NumCtx.f64()
	var X = ctx.cn(cursor_pos.x, cursor_pos.y)
	# largest magnitude along the image chain: the scale of its rounding
	var scale := maxf(1.0, cursor_pos.length())
	var flags := _flags()
	var effs: Array = BeamSolver.plan_effects(sc, BeamTracer.plan_items(plan_entries), flags)
	for j in range(plan_entries.size() - 1, -1, -1):
		var surf: BeamSceneSurf = sc.by_id.get(plan_entries[j].surface_id)
		var etype: String = effs[j]
		if etype in BeamScene.PROJECTIVE():
			return out
		if not (etype in BeamScene.TRANSFORMATIVE()):
			continue
		X = BeamEffects.map_point(X, surf, true)
		if X == null:
			return out
		scale = maxf(scale, Vector2(X.fx(), X.fy()).length())
	# every frame change fixes the cursor (it lies on each planned carrier):
	# the image is the cursor exactly, and the reference aims at it
	if BeamSolver.fixes_cursor(sc, ctx.cn(cursor_pos.x, cursor_pos.y), BeamTracer.plan_items(plan_entries), flags):
		X = ctx.cn(cursor_pos.x, cursor_pos.y)
	var img := Vector2(X.fx(), X.fy())
	if not (is_finite(img.x) and is_finite(img.y)) or img == player_pos:
		return out
	var defs := _defs()
	var ref := PrecisionLadder.solve_physical(defs, [player_pos.x, player_pos.y], [X.fx(), X.fy()], [],
		BeamSolver.MAX_HITS, BeamTracer.TOL(), _fsc_cache, flags)
	# the reference aims at the float64 rounding of the image: compare only
	# while the rounding of the aim angle (bound delta) cannot move an event
	# by more than 0.005 px (tangent-linear gains of the reference trace)
	var sens: ErrorEstimateSensitivity = ref.physical_sens
	var delta: float = 64.0 * Exact.U() * scale / maxf(img.distance_to(player_pos), 1e-300)
	var a: Array = r.physical.filter(func(e): return e.kind == "hit")
	var b: Array = ref.physical.filter(func(e): return e.kind == "hit")
	var hit_gain: Array = []
	for k in sens.events.size():
		if sens.events[k].kind == "hit":
			hit_gain.append(sens.gain[k])
	var n := mini(a.size(), b.size()) - 1     # the event budget differs by the cursor
	for i in maxi(n, 0):
		var move: float = hit_gain[i] * delta if i < hit_gain.size() else INF
		if move > 0.005:
			break
		# a decision the aim rounding can reach (a candidate or endpoint within
		# reach; an exact tie in the planned trace has a zero gap in the
		# reference) is decided by that rounding: stop comparing
		if b[i].margin <= move:
			break
		if a[i].surface_id != b[i].surface_id or a[i].left != b[i].left:
			out.append("DIRECTION-ONLY: hit %d s=%d vs s=%d without plan" % [i, a[i].surface_id, b[i].surface_id])
			return out
		var d := Vector2(a[i].point.fx() - b[i].point.fx(), a[i].point.fy() - b[i].point.fy()).length()
		if d > 0.05:
			out.append("DIRECTION-ONLY: hit %d off by %s px" % [i, d])
			return out
	return out

static func _extract_trace_geometry(path: TracePath) -> Array:
	var segments: Array = []
	if path == null or path.steps.size() == 0:
		return segments
	var current_start: Vector2 = path.steps[0].start
	var current_end: Vector2 = path.steps[0].end
	var current_fid: int = path.steps[0].frame_id
	for i in range(1, path.steps.size()):
		var step: TraceStep = path.steps[i]
		if step.frame_id == current_fid:
			current_end = step.end
		else:
			segments.append({"start": current_start, "end": current_end})
			current_start = step.start
			current_end = step.end
			current_fid = step.frame_id
	segments.append({"start": current_start, "end": current_end})
	return _merge_collinear_stages(segments)

static func _segments_collinear(a: Dictionary, b: Dictionary, tol: float = 1.0) -> bool:
	if a.end.distance_to(b.start) > tol:
		return false
	var dir_a: Vector2 = a.end - a.start
	var dir_b: Vector2 = b.end - b.start
	if dir_a.length_squared() < 0.001 or dir_b.length_squared() < 0.001:
		return true
	var cross := dir_a.normalized().cross(dir_b.normalized())
	var dot := dir_a.normalized().dot(dir_b.normalized())
	return absf(cross) < 0.01 and dot > 0.0

static func _merge_collinear_stages(segments: Array) -> Array:
	if segments.size() <= 1:
		return segments
	var merged: Array = [segments[0]]
	for i in range(1, segments.size()):
		if _segments_collinear(merged[merged.size() - 1], segments[i]):
			merged[merged.size() - 1].end = segments[i].end
		else:
			merged.append(segments[i])
	return merged

static func _geometric_carrier_dist(point: Vector2, carrier: GeneralizedCircle) -> float:
	var f := carrier.evaluate(point)
	var gx := 2.0 * carrier.a * point.x + carrier.b
	var gy := 2.0 * carrier.a * point.y + carrier.c
	var grad := sqrt(gx * gx + gy * gy)
	if grad < 1e-10:
		return INF
	return absf(f) / grad

func _segment_intersection(a1: Vector2, a2: Vector2, b1: Vector2, b2: Vector2) -> Dictionary:
	var d1 := a2 - a1
	var d2 := b2 - b1
	var cross := d1.cross(d2)
	if absf(cross) < 1e-10:
		return {}
	var d := b1 - a1
	var t := d.cross(d2) / cross
	var u := d.cross(d1) / cross
	if t > 0.01 and t < 0.99 and u > 0.01 and u < 0.99:
		return {"point": a1 + d1 * t}
	return {}

func _point_to_segment_dist(p: Vector2, a: Vector2, b: Vector2) -> float:
	var ab := b - a
	var len_sq := ab.length_squared()
	if len_sq == 0.0:
		return p.distance_to(a)
	var t := clampf((p - a).dot(ab) / len_sq, 0.0, 1.0)
	return p.distance_to(a + t * ab)

func _get_named_traces() -> Dictionary:
	var result := {}
	if _renderer:
		var physical = _renderer.get_traced_path()
		if physical != null:
			result["physical"] = physical
		var planned = _renderer.get_planned_path()
		if planned != null:
			result["planned"] = planned
	return result

static func _is_at_bounds(p: Vector2, bounds: Rect2) -> bool:
	return (p.x <= bounds.position.x + 2.0 or p.x >= bounds.end.x - 2.0 or
		p.y <= bounds.position.y + 2.0 or p.y >= bounds.end.y - 2.0)

func _position_nodes(player_pos: Vector2, cursor_pos: Vector2) -> void:
	if _player:
		_player.global_position = player_pos
	if _cursor:
		_cursor.global_position = cursor_pos

# --- Static checks (kept from original for stage compatibility) ---

static func check_S11(segment: Segment) -> Array[String]:
	var violations: Array[String] = []
	var carrier := segment.get_carrier()
	var eps := 0.01
	if absf(carrier.evaluate(segment.start.coords)) > eps:
		violations.append("S11: start not on carrier")
	if absf(carrier.evaluate(segment.end.coords)) > eps:
		violations.append("S11: end not on carrier")
	if segment.via.coords != Vector2(INF, INF) and absf(carrier.evaluate(segment.via.coords)) > eps:
		violations.append("S11: via not on carrier")
	return violations

static func check_S12(segment: Segment, test_points: Array[Vector2]) -> Array[String]:
	var violations: Array[String] = []
	for point in test_points:
		var carrier := segment.get_carrier()
		var f_val := carrier.evaluate(point)
		if f_val == 0.0:
			continue
		var side := segment.determine_side(point)
		var traversal := segment.end.coords - segment.start.coords
		var to_point := point - segment.start.coords
		var cross_val := traversal.cross(to_point)
		var expected_side: Side.Value
		if cross_val < 0.0:
			expected_side = Side.Value.LEFT
		else:
			expected_side = Side.Value.RIGHT
		if carrier.is_line() and side != expected_side:
			violations.append("S12: point %s side=%d expected=%d" % [point, side, expected_side])
	return violations

# ---------------------------------------------------------------- beam events

# The flags the renderer's traces started from (the game state's).
func _flags() -> Dictionary:
	return _renderer.get_flags() if _renderer and _renderer.has_method("get_flags") else {}

func _beam_result():
	return _renderer.last_beam_result if _renderer and "last_beam_result" in _renderer else null

var _defs_cache: Array = []
var _scene_cache: BeamScene = null
var _defs_src: Array = []
var _defs_n := -1
var _fsc_cache: FastSolverScene = null

# Scene defs of the checked scene, built once per surface set (the same
# surfaces array with the same surfaces: sweeps never edit surfaces).
func _defs() -> Array:
	var parent := _renderer.get_parent()
	var surfaces: Array = parent.surfaces if parent and "surfaces" in parent else []
	if _scene_cache != null and is_same(surfaces, _defs_src) and surfaces.size() == _defs_n:
		return _defs_cache
	_defs_src = surfaces
	_defs_n = surfaces.size()
	_defs_cache = SceneDefs.defs_from_surfaces(surfaces)
	_scene_cache = BeamScene.from_defs(NumCtx.f64(), _defs_cache)
	_fsc_cache = FastSolverScene.from_defs(_defs_cache)
	return _defs_cache

func _beam_scene() -> BeamScene:
	_defs()
	return _scene_cache

func check_EFFECT_LAW(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var r = _beam_result()
	if r == null or player_pos == cursor_pos:
		return [] as Array[String]
	var out: Array[String] = []
	out.append_array(check_effect_law(r.planned, _beam_scene()))
	out.append_array(check_effect_law(r.physical, _beam_scene()))
	return out

func check_EVENT_ON_BEAM(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var r = _beam_result()
	if r == null or player_pos == cursor_pos:
		return [] as Array[String]
	var out: Array[String] = []
	out.append_array(check_event_on_beam(r.planned, _beam_scene()))
	out.append_array(check_event_on_beam(r.physical, _beam_scene()))
	return out

func check_BEAM_ORDER(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var r = _beam_result()
	if r == null or player_pos == cursor_pos:
		return [] as Array[String]
	var out: Array[String] = []
	out.append_array(check_beam_order(r.planned))
	out.append_array(check_beam_order(r.physical))
	return out

static func _f64_beam(b: HPBeam) -> HPBeam:
	return HPBeam.make(CN.make(F64Num.of(b.p.fx()), F64Num.of(b.p.fy())), CN.make(F64Num.of(b.tau.fx()), F64Num.of(b.tau.fy())),
		F64Num.of(b.kappa.to_float()))

static func _f64_t(t) -> Num:
	return F64Num.of(t.to_float())

static func _tol(x: float, y: float) -> float:
	return 1e-6 * maxf(1.0, Vector2(x, y).length())

# Every applied effect maps its beam by its exact law: the next beam passes
# through the law's departure point with the law's tangent and curvature
# (pops restore an earlier beam and are checked by BEAM_ORDER / identity).
static func check_effect_law(events: Array, scene: BeamScene) -> Array[String]:
	var out: Array[String] = []
	for i in events.size() - 1:
		var e: BeamSolverEvent = events[i]
		if e.kind != "hit" or not e.applied or e.terminal or e.popped or e.point == null:
			continue
		var s: BeamSceneSurf = scene.by_id[e.surface_id]
		var b := _f64_beam(e.beam)
		var t := _f64_t(e.t_to)
		var h := CN.make(F64Num.of(e.point.fx()), F64Num.of(e.point.fy()))
		var law: HPBeam
		if e.projective:
			law = BeamEffects.project(b, t, s, e.left, h)
		else:
			law = BeamEffects.apply(b, t, s, s.effect(e.left), h)
		var nb := _f64_beam(events[i + 1].beam)
		var u = nb.param_of(law.p)
		var q := nb.at(u)
		var tol := _tol(law.p.fx(), law.p.fy())
		var dp := Vector2(q.fx() - law.p.fx(), q.fy() - law.p.fy()).length()
		var tg := nb.tangent_at(u)
		var dt := Vector2(tg.fx() - law.tau.fx(), tg.fy() - law.tau.fy()).length()
		var dk := absf(nb.kappa.to_float() - law.kappa.to_float())
		if dp > tol or dt > 1e-6 or dk > 1e-6 * maxf(1e-3, absf(law.kappa.to_float())):
			out.append("EFFECT-LAW: event %d s=%d: next beam off the law (point %s, tangent %s, curvature %s)" % [i, e.surface_id, dp, dt, dk])
	return out

static func _carrier_violations(events: Array, scene: BeamScene) -> Array[String]:
	var out: Array[String] = []
	for i in events.size():
		var e: BeamSolverEvent = events[i]
		if e.kind != "hit" or e.point == null:
			continue
		var s: BeamSceneSurf = scene.by_id[e.surface_id]
		var x: float = e.point.fx()
		var y: float = e.point.fy()
		var d: float
		if s.line:
			var dx := s.ex - s.sx
			var dy := s.ey - s.sy
			d = absf(dx * (y - s.sy) - dy * (x - s.sx)) / sqrt(dx * dx + dy * dy)
		else:
			d = absf(Vector2(x - s.cx, y - s.cy).length() - s.r)
		if d > _tol(x, y):
			out.append("HITPOINT-ON-CARRIER: event %d s=%d off its carrier by %s" % [i, e.surface_id, d])
	return out

# Every event point lies on its beam (at its parameter) and every hit on its
# surface's carrier.
static func check_event_on_beam(events: Array, scene: BeamScene) -> Array[String]:
	var out: Array[String] = []
	for i in events.size():
		var e: BeamSolverEvent = events[i]
		if e.point == null or e.t_to == null:
			continue
		var b := e.beam
		var qa := FastSolver.at_view(FastSolver.beam(b.p.fx(), b.p.fy(), b.tau.fx(), b.tau.fy(), b.kappa.to_float()), e.t_to.to_float())
		var d := Vector2(qa[0] - e.point.fx(), qa[1] - e.point.fy()).length()
		if d > _tol(e.point.fx(), e.point.fy()):
			out.append("EVENT-ON-BEAM: event %d (%s) off its beam by %s" % [i, e.kind, d])
	out.append_array(_carrier_violations(events, scene))
	return out

# Consecutive events on the same beam chain their parameters: each arc starts
# where the previous one ended (walk order).
static func check_beam_order(events: Array) -> Array[String]:
	var out: Array[String] = []
	for i in range(1, events.size()):
		var a: BeamSolverEvent = events[i - 1]
		var b: BeamSolverEvent = events[i]
		if a.beam != b.beam or a.t_to == null or b.t_from == null:
			continue
		if a.t_to.to_float() != b.t_from.to_float():
			out.append("BEAM-ORDER: event %d starts at t=%s, event %d ended at t=%s" % [i, b.t_from.to_float(), i - 1, a.t_to.to_float()])
	return out

# The planned trace applies exactly the plan's transformative entries, in
# order, before the cursor.
static func check_plan_effects_applied_events(events: Array, plan: Array, _cursor_reached: bool) -> Array[String]:
	var out: Array[String] = []
	var applied: Array = []
	for e in events:
		if e.kind == "cursor":
			break
		if e.kind == "hit" and e.applied and not e.terminal and not e.projective:
			applied.append([e.surface_id, e.left])
	var want: Array = []
	for item in plan:
		want.append([item.surface_id, item.left])
	if applied != want:
		out.append("PLAN-EFFECTS: applied %s, plan %s" % [str(applied), str(want)])
	return out
