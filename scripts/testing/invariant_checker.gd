class_name InvariantChecker
extends RefCounted

var _scene: Node
var _player: CharacterBody2D
var _cursor: Node2D
var _renderer: Node2D
var _game_mgr: Node

func setup(scene: Node) -> void:
	_scene = scene
	_player = scene.get_node_or_null("Player")
	_cursor = scene.get_node_or_null("Cursor")
	_renderer = scene.get_node_or_null("PathRenderer")
	_game_mgr = scene.get_node_or_null("GameManager")

func check_all(player_pos: Vector2, cursor_pos: Vector2, plan_entries: Array = []) -> Array[String]:
	var violations: Array[String] = []
	_position_nodes(player_pos, cursor_pos)
	var plan_applied := false
	if _game_mgr and "plan" in _game_mgr:
		_game_mgr.plan.clear()
		for entry in plan_entries:
			_game_mgr.plan.add_entry(entry.surface_id, entry.side)
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
	violations.append_array(check_BACK_TRANSFORM_ALIGNMENT(player_pos, cursor_pos))
	violations.append_array(check_PHYSICS_COMPLIANCE(player_pos, cursor_pos))
	violations.append_array(check_HITPOINT_ON_CARRIER(player_pos, cursor_pos))
	violations.append_array(check_S18_FRAME_DETERMINANT(player_pos, cursor_pos))
	violations.append_array(check_ON_SEGMENT_CONSISTENCY(player_pos, cursor_pos))
	violations.append_array(check_SHARED_RAY(player_pos, cursor_pos))
	violations.append_array(check_RAY_ALIGNMENT(player_pos, cursor_pos))
	violations.append_array(check_ARC_MIDPOINT_ALIGNMENT(player_pos, cursor_pos))
	violations.append_array(check_PARAMETER_MONOTONICITY(player_pos, cursor_pos))
	violations.append_array(check_POST_INVERSION_ARC(player_pos, cursor_pos))
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
			var has_plan: bool = _game_mgr != null and "plan" in _game_mgr and not _game_mgr.plan.is_empty()
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
	var path: Tracer.TracedPath = _renderer.get_traced_path()
	if path == null:
		return violations
	for i in range(1, path.steps.size()):
		var prev: Tracer.Step = path.steps[i - 1]
		var curr: Tracer.Step = path.steps[i]
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
	var path: Tracer.TracedPath = _renderer.get_traced_path()
	if path == null:
		return violations
	for i in path.steps.size():
		var step: Tracer.Step = path.steps[i]
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
	var first: Tracer.Step = typed[0]
	if first.type != StepTypes.Type.ALIGNED and first.type != StepTypes.Type.ALIGNED_POST_PLANNED:
		violations.append("GREEN-FROM-PLAYER: First step type=%d, expected green" % first.type)
	return violations

func check_ORIGIN_NOT_REHIT(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var path: Tracer.TracedPath = _renderer.get_traced_path()
	if path == null:
		return violations
	for i in path.steps.size():
		var step: Tracer.Step = path.steps[i]
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
		var ms: Tracer.Step = typed[i]
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
		var ms: Tracer.Step = typed[i]
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
	var physical: Tracer.TracedPath = _renderer.get_traced_path()
	if physical == null:
		return violations
	# Extract non-red preview steps
	var non_red: Array = []
	for i in typed.size():
		var ms: Tracer.Step = typed[i]
		if ms.type == StepTypes.Type.ALIGNED or ms.type == StepTypes.Type.ALIGNED_POST_PLANNED or ms.type == StepTypes.Type.DIVERGED_PHYSICAL:
			non_red.append(ms)
	if non_red.size() != physical.steps.size():
		violations.append("PHYSICAL-PREVIEW-MATCH: non-red count=%d != physical count=%d" % [non_red.size(), physical.steps.size()])
		return violations
	var portal_seen := false
	for i in non_red.size():
		var ms: Tracer.Step = non_red[i]
		var ps: Tracer.Step = physical.steps[i]
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
	var physical: Tracer.TracedPath = _renderer.get_traced_path()
	if physical == null or physical.steps.size() < 2:
		return violations
	for i in range(1, physical.steps.size()):
		var prev: Tracer.Step = physical.steps[i - 1]
		var curr: Tracer.Step = physical.steps[i]
		# Only skip gaps at escape/return boundaries (beyond-hit with t < 0)
		var prev_is_escape: bool = (prev.hit == null) or (prev.hit != null and prev.hit.t < 0.0)
		var curr_is_return: bool = (curr.hit != null and curr.hit.t < 0.0)
		if prev_is_escape or curr_is_return:
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
		var ms: Tracer.Step = typed[i]
		if StepTypes.is_solid(ms.type):
			solid_steps.append(ms)
	if solid_steps.size() == 0:
		return violations
	var first: Tracer.Step = solid_steps[0]
	if first.start != player_pos:
		violations.append("SOLID-PATH-TO-CURSOR: first solid step starts at %s, not player %s" % [first.start, player_pos])
	var bounds := VisualConverter.DEFAULT_BOUNDS
	for i in range(1, solid_steps.size()):
		var prev: Tracer.Step = solid_steps[i - 1]
		var curr: Tracer.Step = solid_steps[i]
		if prev.end.distance_to(curr.start) > 0.05:
			# Skip gaps at escape/return boundaries (ray wraps through infinity)
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
		var path: Tracer.TracedPath = _traces[trace_name]
		if path.steps.size() == 0:
			continue
		var last: Tracer.Step = path.steps[path.steps.size() - 1]
		var end_pos: Vector2 = last.end
		var valid := false
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

func check_PLAN_EFFECTS_APPLIED(player_pos: Vector2, cursor_pos: Vector2, plan_entries: Array) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos or plan_entries.size() == 0:
		return violations
	var planned_path = _renderer.get_planned_path()
	if planned_path == null or planned_path.steps.size() < 2:
		return violations
	# cursor_index == -1 means the plan wasn't consumed (geometrically infeasible)
	if planned_path.cursor_index < 0:
		return violations
	var ci: int = planned_path.cursor_index
	var frame_changes := 0
	for i in range(1, mini(ci + 1, planned_path.steps.size())):
		var prev: Tracer.Step = planned_path.steps[i - 1]
		var curr: Tracer.Step = planned_path.steps[i]
		if prev.frame_id != curr.frame_id:
			frame_changes += 1
	if frame_changes < plan_entries.size():
		violations.append("PLAN-EFFECTS: expected %d frame changes for %d plan entries, got %d" % [plan_entries.size(), plan_entries.size(), frame_changes])
	return violations

func check_BACK_TRANSFORM_ALIGNMENT(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var path: Tracer.TracedPath = _renderer.get_traced_path()
	if path == null or path.steps.size() == 0:
		return violations
	var bounds := VisualConverter.DEFAULT_BOUNDS
	var check_limit: int = path.cursor_index if path.cursor_index >= 0 else path.steps.size()
	check_limit = mini(check_limit, path.steps.size())
	var portal_seen := false
	for i in check_limit:
		var step: Tracer.Step = path.steps[i]
		if step.after_portal:
			portal_seen = true
		if portal_seen:
			continue
		if step.hit == null:
			continue
		if step.start == step.end:
			continue
		if step.frame == null or step.ray == null:
			continue
		if step.frame.id != MobiusTransform.IDENTITY_ID:
			continue
		var frame_inv := step.frame.invert()
		var aim_dir := step.ray.direction.to_vector().normalized()
		var origin := step.ray.origin.coords
		var bt_start := frame_inv.apply(step.start)
		var bt_end := frame_inv.apply(step.end)
		if not _is_at_bounds(step.start, bounds):
			var cross_s := (bt_start - origin).cross(aim_dir)
			if absf(cross_s) > 1.0:
				violations.append("BACK-TRANSFORM-ALIGNMENT: step %d start cross=%.2f (bt=%s)" % [i, cross_s, bt_start])
		if not _is_at_bounds(step.end, bounds):
			var cross_e := (bt_end - origin).cross(aim_dir)
			if absf(cross_e) > 1.0:
				violations.append("BACK-TRANSFORM-ALIGNMENT: step %d end cross=%.2f (bt=%s)" % [i, cross_e, bt_end])
	return violations

func check_PHYSICS_COMPLIANCE(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var path: Tracer.TracedPath = _renderer.get_traced_path()
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
		var step: Tracer.Step = path.steps[i]
		if step.after_portal:
			portal_seen = true
		if portal_seen:
			continue
		if step.is_arc_step:
			continue
		if step.start == step.end:
			continue
		if step.frame_id != MobiusTransform.IDENTITY_ID:
			continue
		for surf in surfaces:
			var s: Surface = surf
			var config_l := s.active_side_config(Side.Value.LEFT, GameState.new())
			var config_r := s.active_side_config(Side.Value.RIGHT, GameState.new())
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
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var _traces := _get_named_traces()
	for trace_name in _traces:
		var path: Tracer.TracedPath = _traces[trace_name]
		for i in path.steps.size():
			var step: Tracer.Step = path.steps[i]
			if step.hit == null or step.hit.segment == null:
				continue
			var carrier := step.hit.segment.get_carrier()
			var pt := step.hit.point.coords
			if absf(pt.x) > 1e8 or absf(pt.y) > 1e8:
				continue
			var eval_val := carrier.evaluate(pt)
			var grad_mag := sqrt(carrier.b * carrier.b + carrier.c * carrier.c + 4.0 * carrier.a * carrier.a * (pt.x * pt.x + pt.y * pt.y))
			var dist := absf(eval_val) / maxf(grad_mag, 1e-10)
			var tol := 1.0 + 55.0 * i
			if dist > tol:
				violations.append("HITPOINT-ON-CARRIER: %s step %d dist=%.4f at %s" % [trace_name, i, dist, pt])
	return violations

func check_S18_FRAME_DETERMINANT(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var _traces := _get_named_traces()
	for trace_name in _traces:
		var path: Tracer.TracedPath = _traces[trace_name]
		for i in path.steps.size():
			var step: Tracer.Step = path.steps[i]
			if step.frame == null:
				continue
			var det_re: float = (step.frame.a_re * step.frame.d_re - step.frame.a_im * step.frame.d_im) - (step.frame.b_re * step.frame.c_re - step.frame.b_im * step.frame.c_im)
			var det_im: float = (step.frame.a_re * step.frame.d_im + step.frame.a_im * step.frame.d_re) - (step.frame.b_re * step.frame.c_im + step.frame.b_im * step.frame.c_re)
			var det_mod2: float = det_re * det_re + det_im * det_im
			if det_mod2 == 0.0:
				violations.append("S18-FRAME-DET: %s step %d |det|^2 = 0" % [trace_name, i])
	return violations

func check_ON_SEGMENT_CONSISTENCY(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var _traces := _get_named_traces()
	for trace_name in _traces:
		var path: Tracer.TracedPath = _traces[trace_name]
		for i in path.steps.size():
			var step: Tracer.Step = path.steps[i]
			if step.hit == null or step.hit.segment == null:
				continue
			var recomputed := Intersection.is_on_segment(step.hit.point.coords, step.hit.segment)
			if step.hit.on_segment != recomputed:
				violations.append("ON-SEGMENT-CONSISTENCY: %s step %d stored=%s recomputed=%s at %s" % [trace_name, i, step.hit.on_segment, recomputed, step.hit.point.coords])
	return violations

func check_SHARED_RAY(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	# Within each transformative sub-chain, all steps share the same ray instance.
	# Currently no projective effects exist, so the entire trace is one sub-chain.
	# When projective effects are added, split at projective-effect hitpoints.
	var _traces := _get_named_traces()
	for trace_name in _traces:
		var path: Tracer.TracedPath = _traces[trace_name]
		if path.steps.size() == 0:
			continue
		var first_ray: Ray = path.steps[0].ray
		if first_ray == null:
			continue
		for i in range(1, path.steps.size()):
			var step: Tracer.Step = path.steps[i]
			if step.ray == null:
				violations.append("SHARED-RAY: %s step %d has null ray" % [trace_name, i])
				continue
			if step.ray != first_ray:
				violations.append("SHARED-RAY: %s step %d ray is different instance from step 0" % [trace_name, i])
	return violations

func check_RAY_ALIGNMENT(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var bounds := VisualConverter.DEFAULT_BOUNDS
	# Back-transforming visual endpoints through frame.invert() should place
	# them on the original ray line. Only checked for involutory frames
	# (identity and single reflections).
	var _traces := _get_named_traces()
	for trace_name in _traces:
		var path: Tracer.TracedPath = _traces[trace_name]
		if path.steps.size() == 0:
			continue
		var first_step: Tracer.Step = path.steps[0]
		if first_step.ray == null:
			continue
		var origin := first_step.ray.origin.coords
		var aim_dir := first_step.ray.direction.to_vector().normalized()
		var portal_seen := false
		for i in path.steps.size():
			var step: Tracer.Step = path.steps[i]
			if step.after_portal:
				portal_seen = true
			if portal_seen:
				continue
			if step.frame == null or step.ray == null:
				continue
			if step.start == step.end:
				continue
			if step.hit == null:
				continue
			if step.frame.id != MobiusTransform.IDENTITY_ID:
				continue
			var frame_inv := step.frame.invert()
			var threshold := 5.0 if step.is_arc_step else 1.0
			if not _is_at_bounds(step.start, bounds):
				var bt_start := frame_inv.apply(step.start)
				var cross_s := (bt_start - origin).cross(aim_dir)
				if absf(cross_s) > threshold:
					violations.append("RAY-ALIGNMENT: %s step %d start cross=%.2f (bt=%s)" % [trace_name, i, cross_s, bt_start])
			if not _is_at_bounds(step.end, bounds):
				var bt_end := frame_inv.apply(step.end)
				var cross_e := (bt_end - origin).cross(aim_dir)
				if absf(cross_e) > threshold:
					violations.append("RAY-ALIGNMENT: %s step %d end cross=%.2f (bt=%s)" % [trace_name, i, cross_e, bt_end])
	return violations

func check_ARC_MIDPOINT_ALIGNMENT(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var bounds := VisualConverter.DEFAULT_BOUNDS
	var _traces := _get_named_traces()
	for trace_name in _traces:
		var path: Tracer.TracedPath = _traces[trace_name]
		if path.steps.size() == 0:
			continue
		var first_step: Tracer.Step = path.steps[0]
		if first_step.ray == null:
			continue
		var origin := first_step.ray.origin.coords
		var aim_dir := first_step.ray.direction.to_vector().normalized()
		for i in path.steps.size():
			var step: Tracer.Step = path.steps[i]
			if not step.is_arc_step or step.frame == null or step.ray == null:
				continue
			if step.hit == null:
				continue
			if not VisualConverter.is_arc(step.start, step.via, step.end):
				continue
			if _is_at_bounds(step.start, bounds) or _is_at_bounds(step.end, bounds):
				continue
			var bt_via := step.frame.invert().apply(step.via)
			if is_inf(bt_via.x) or is_inf(bt_via.y) or bt_via.length() > 1e5:
				continue
			var cross := (bt_via - origin).cross(aim_dir)
			if absf(cross) > 10.0:
				violations.append("ARC-MIDPOINT-ALIGNMENT: %s step %d cross=%.2f (via=%s bt=%s)" % [trace_name, i, cross, step.via, bt_via])
	return violations

func check_PARAMETER_MONOTONICITY(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var _traces := _get_named_traces()
	for trace_name in _traces:
		var path: Tracer.TracedPath = _traces[trace_name]
		if path.steps.size() < 2:
			continue
		for i in range(1, path.steps.size()):
			var prev: Tracer.Step = path.steps[i - 1]
			var curr: Tracer.Step = path.steps[i]
			if prev.frame_id != curr.frame_id:
				continue
			if curr.after_portal:
				continue
			# hit.t is the normalized-frame ray parameter; steps without a
			# normalized frame (BeamTracer) do not define it
			if prev.frame == null or curr.frame == null:
				continue
			if prev.hit == null or curr.hit == null:
				continue
			if prev.hit.segment == null or curr.hit.segment == null:
				continue
			if prev.hit.t > 0.0 and curr.hit.t <= 0.0:
				continue
			if curr.hit.t <= 0.0:
				continue
			if curr.hit.t < prev.hit.t:
				violations.append("PARAM-MONOTONICITY: %s step %d t=%.4f < prev t=%.4f (frame=%d)" % [trace_name, i, curr.hit.t, prev.hit.t, curr.frame_id])
	return violations

static func _is_ray_through_pole(ray: Ray, frame: MobiusTransform) -> bool:
	var pole := frame.pole()
	if is_inf(pole.x) or is_inf(pole.y) or is_nan(pole.x) or is_nan(pole.y):
		return false
	var origin := ray.origin.coords
	var dir := ray.direction.to_normalized()
	var to_pole := pole - origin
	var cross := to_pole.cross(dir)
	return absf(cross) < 1.0

func check_POST_INVERSION_ARC(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var _traces := _get_named_traces()
	for trace_name in _traces:
		var path: Tracer.TracedPath = _traces[trace_name]
		for i in path.steps.size():
			var step: Tracer.Step = path.steps[i]
			if step.frame != null and step.frame.maps_lines_to_arcs() and not step.is_arc_step and step.hit != null:
				if step.ray != null and _is_ray_through_pole(step.ray, step.frame):
					continue
				violations.append("POST-INVERSION-ARC: %s step %d in inversive frame (fid=%d) has is_arc_step=false" % [trace_name, i, step.frame_id])
	return violations

func check_VIA_ON_ARC(player_pos: Vector2, cursor_pos: Vector2) -> Array[String]:
	var violations: Array[String] = []
	if not _renderer or player_pos == cursor_pos:
		return violations
	var _traces := _get_named_traces()
	for trace_name in _traces:
		var path: Tracer.TracedPath = _traces[trace_name]
		for i in path.steps.size():
			var step: Tracer.Step = path.steps[i]
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
		var path: Tracer.TracedPath = _traces[trace_name]
		for i in path.steps.size():
			var step: Tracer.Step = path.steps[i]
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
				if step.frame_id != MobiusTransform.IDENTITY_ID and step.hit.segment != null:
					var nc := step.hit.segment.get_carrier()
					var norm_err := _geometric_carrier_dist(step.hit.point.coords, nc)
					if norm_err < 1.0:
						continue
				violations.append("VISUAL-ON-CARRIER: %s step %d visual endpoint %s not on any physical carrier" % [trace_name, i, end_pos])
	return violations

func check_DIRECTION_ONLY(player_pos: Vector2, cursor_pos: Vector2, plan_entries: Array) -> Array[String]:
	var violations: Array[String] = []
	if plan_entries.size() == 0:
		return violations
	if not _renderer or player_pos == cursor_pos:
		return violations

	var surfaces: Array = []
	var parent := _renderer.get_parent()
	if parent and "surfaces" in parent:
		surfaces = parent.surfaces

	var cache := TransformCache.new()
	var aim_dir := Planner.compute_aim_direction(
		player_pos, cursor_pos, plan_entries, surfaces, GameState.new(), cache)
	if aim_dir.is_zero_length():
		return violations

	var aim_point = Planner._compute_image(cursor_pos, plan_entries, surfaces, GameState.new())
	if aim_point == null:
		return violations
	if is_nan(aim_point.x) or is_nan(aim_point.y) or is_inf(aim_point.x) or is_inf(aim_point.y):
		return violations
	if aim_point.distance_to(player_pos) < 0.001:
		return violations

	var trace_with_plan := Tracer.trace(player_pos, aim_dir, surfaces, GameState.new(),
		null, -1.0, Tracer.TraceMode.PHYSICAL, Tracer.TraceMode.PHYSICAL,
		plan_entries, null, cursor_pos)

	var trace_no_plan := Tracer.trace(player_pos, aim_dir, surfaces, GameState.new(),
		null, -1.0, Tracer.TraceMode.PHYSICAL, Tracer.TraceMode.PHYSICAL,
		[], null, aim_point)

	var orig_geo := _extract_trace_geometry(trace_with_plan)
	var ref_geo := _extract_trace_geometry(trace_no_plan)

	if orig_geo.size() == 0 and ref_geo.size() == 0:
		return violations

	var both_at_max := trace_with_plan.hit_count >= Tracer.MAX_HITS \
		and trace_no_plan.hit_count >= Tracer.MAX_HITS

	if orig_geo.size() != ref_geo.size() and not both_at_max:
		violations.append("DIRECTION-ONLY: segment count mismatch: with_plan=%d no_plan=%d" % [orig_geo.size(), ref_geo.size()])
		return violations

	var compare_count := mini(orig_geo.size(), ref_geo.size())
	var tol := 1.0
	for i in compare_count:
		var o: Dictionary = orig_geo[i]
		var r: Dictionary = ref_geo[i]
		var start_dist: float = o.start.distance_to(r.start)
		if start_dist > tol:
			violations.append("DIRECTION-ONLY: segment %d start mismatch: orig=%s ref=%s dist=%.2f" % [i, o.start, r.start, start_dist])
		var is_last := i == compare_count - 1
		if both_at_max and is_last:
			continue
		var end_dist: float = o.end.distance_to(r.end)
		if end_dist > tol:
			violations.append("DIRECTION-ONLY: segment %d end mismatch: orig=%s ref=%s dist=%.2f" % [i, o.end, r.end, end_dist])

	return violations

static func _extract_trace_geometry(path: Tracer.TracedPath) -> Array:
	var segments: Array = []
	if path == null or path.steps.size() == 0:
		return segments
	var current_start: Vector2 = path.steps[0].start
	var current_end: Vector2 = path.steps[0].end
	var current_fid: int = path.steps[0].frame_id
	for i in range(1, path.steps.size()):
		var step: Tracer.Step = path.steps[i]
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

# ---------------------------------------------------------------- beam events (STUB, Stage 83 red)

static func check_effect_law(_events: Array, _scene: BeamScene) -> Array[String]:
	return []

static func check_event_on_beam(_events: Array, _scene: BeamScene) -> Array[String]:
	return []

static func check_beam_order(_events: Array) -> Array[String]:
	return []

static func check_plan_effects_applied_events(_events: Array, _plan: Array, _cursor_reached: bool) -> Array[String]:
	return []
