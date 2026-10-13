extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

func _build_main_scene_surfaces() -> Array:
	var surfaces: Array = []
	surfaces.append_array(RoomBuilder.create_room_surfaces(Rect2(160, 90, 1600, 900)))
	# Surface 5: mirror at x=500
	var seg5 := Segment.from_coords(Vector2(500, 200), Vector2(500, 700), Vector2(500, 450))
	var refl5 := ReflectionEffect.new(seg5.get_carrier())
	surfaces.append(Surface.new(seg5, SideConfig.new(refl5, true), SideConfig.new(null, false), false, false))
	# Surface 6: mirror at x=1400
	var seg6 := Segment.from_coords(Vector2(1400, 300), Vector2(1400, 800), Vector2(1400, 550))
	var refl6 := ReflectionEffect.new(seg6.get_carrier())
	surfaces.append(Surface.new(seg6, SideConfig.new(refl6, true), SideConfig.new(null, false), false, false))
	# Surface 7: mirror at y=800
	var seg7 := Segment.from_coords(Vector2(700, 800), Vector2(1200, 800), Vector2(950, 800))
	var refl7 := ReflectionEffect.new(seg7.get_carrier())
	surfaces.append(Surface.new(seg7, SideConfig.new(refl7, true), SideConfig.new(null, false), false, false))
	# Surface 8: right-side mirror at x=960
	var seg8 := Segment.from_coords(Vector2(960, 200), Vector2(960, 500), Vector2(960, 350))
	var refl8 := ReflectionEffect.new(seg8.get_carrier())
	surfaces.append(Surface.new(seg8, SideConfig.new(null, false), SideConfig.new(refl8, true), false, false))
	# Surface 9: inversion arc at x=1100
	var seg9 := Segment.from_coords(Vector2(1100, 400), Vector2(1100, 700), Vector2(1230, 550))
	var inv9 := CircleInversionEffect.new(seg9.get_carrier())
	surfaces.append(Surface.new(seg9, SideConfig.new(inv9, true), SideConfig.new(null, false), false, true))
	return surfaces

static func _geometric_carrier_dist(point: Vector2, carrier: GeneralizedCircle) -> float:
	if carrier.is_line():
		var denom := sqrt(carrier.b * carrier.b + carrier.c * carrier.c)
		if denom < 1e-10:
			return INF
		return absf(carrier.b * point.x + carrier.c * point.y + carrier.d) / denom
	else:
		var ctr := carrier.center()
		var r := carrier.radius()
		return absf(point.distance_to(ctr) - r)

## Check ALL visual hitpoints in a trace against physical surface carriers.
func _check_all_visual_hitpoints(path: TracePath, surfaces: Array, tolerance: float = 5.0) -> Array:
	var violations := []
	var bounds := VisualConverter.DEFAULT_BOUNDS
	for i in path.steps.size():
		var step: TraceStep = path.steps[i]
		if step.hit == null or step.hit.segment == null:
			continue
		var end_pos := step.end
		if is_inf(end_pos.x) or is_inf(end_pos.y):
			continue
		if InvariantChecker._is_at_bounds(end_pos, bounds):
			continue
		var min_dist := INF
		var closest_idx := -1
		for j in surfaces.size():
			var s: Surface = surfaces[j]
			var dist := _geometric_carrier_dist(end_pos, s.segment.get_carrier())
			if dist < min_dist:
				min_dist = dist
				closest_idx = j
		if min_dist > tolerance:
			violations.append("step %d: visual endpoint %s is %.2f px from nearest carrier (surf %d), frame_id=%d on_seg=%s" % [
				i, end_pos, min_dist, closest_idx, step.frame_id, step.hit.on_segment])
	return violations

## Check that the trace's terminal endpoint is on a terminal-effect carrier.
func _check_terminal_on_terminal_carrier(path: TracePath, surfaces: Array, tolerance: float = 5.0) -> Array:
	var violations := []
	if path.steps.is_empty():
		return violations
	var last: TraceStep = path.steps[path.steps.size() - 1]
	if last.hit == null or last.hit.segment == null:
		return violations
	var end_pos := last.end
	if is_inf(end_pos.x) or is_inf(end_pos.y):
		return violations
	var bounds := VisualConverter.DEFAULT_BOUNDS
	if InvariantChecker._is_at_bounds(end_pos, bounds):
		return violations
	var on_terminal := false
	var state := GameState.new()
	for surf in surfaces:
		var s: Surface = surf
		var has_terminal := false
		var left := s.active_side_config(Side.Value.LEFT, state)
		var right := s.active_side_config(Side.Value.RIGHT, state)
		if left != null and left.effect != null and left.effect.kind() == Effect.Kind.TERMINAL:
			has_terminal = true
		if right != null and right.effect != null and right.effect.kind() == Effect.Kind.TERMINAL:
			has_terminal = true
		if not has_terminal:
			continue
		var dist := _geometric_carrier_dist(end_pos, s.segment.get_carrier())
		if dist < tolerance:
			on_terminal = true
			break
	if not on_terminal:
		violations.append("terminal endpoint %s not on any terminal carrier" % end_pos)
	return violations

# --- Invariant: ALL visual hitpoints on physical carriers (repro scenario) ---

func test_all_visual_hitpoints_on_physical_carrier() -> void:
	var surfaces := _build_main_scene_surfaces()
	var player := Vector2(791.3528, 725.1172)
	var aim := Direction.from_coords(player, Vector2(847.8832, 768.4231))
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())

	var violations := _check_all_visual_hitpoints(path, surfaces)
	for v in violations:
		print("DIAG %s" % v)
	assert_eq(violations.size(), 0,
		"All visual hitpoints should be on a physical carrier. Found %d violations" % violations.size())

# --- Invariant: terminal endpoint on terminal carrier ---

func test_terminal_endpoint_on_terminal_carrier() -> void:
	var surfaces := _build_main_scene_surfaces()
	var player := Vector2(791.3528, 725.1172)
	var aim := Direction.from_coords(player, Vector2(847.8832, 768.4231))
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())

	var violations := _check_terminal_on_terminal_carrier(path, surfaces)
	for v in violations:
		print("DIAG %s" % v)
	assert_eq(violations.size(), 0,
		"Terminal endpoint should be on a terminal carrier. %s" % (violations[0] if violations.size() > 0 else ""))

# --- Diagnostic: per-step carrier distances ---

func test_diagnose_per_step_carrier_distances() -> void:
	var surfaces := _build_main_scene_surfaces()
	var player := Vector2(791.3528, 725.1172)
	var aim := Direction.from_coords(player, Vector2(847.8832, 768.4231))
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())

	var surface_names := ["top wall", "right wall", "bottom wall", "left wall",
		"mirror x=500", "mirror x=1400", "mirror y=800", "mirror x=960(R)", "inversion arc"]

	for i in path.steps.size():
		var step: TraceStep = path.steps[i]
		var end_pos := step.end
		var hit_info := "no-hit"
		if step.hit != null:
			hit_info = "on_seg=%s" % step.hit.on_segment
		if is_inf(end_pos.x) or is_inf(end_pos.y):
			print("DIAG step %d: end=INF frame_id=%d %s" % [i, step.frame_id, hit_info])
			continue
		var min_dist := INF
		var closest_name := ""
		for j in surfaces.size():
			var s: Surface = surfaces[j]
			var dist := _geometric_carrier_dist(end_pos, s.segment.get_carrier())
			if dist < min_dist:
				min_dist = dist
				closest_name = surface_names[j] if j < surface_names.size() else "surf %d" % j
		print("DIAG step %d: end=%s frame_id=%d %s nearest_carrier=%s dist=%.4f" % [
			i, end_pos, step.frame_id, hit_info, closest_name, min_dist])

	assert_true(path.steps.size() > 0, "Should have trace steps")

# --- Sweep: ALL hitpoints on carriers across many traces ---

func test_sweep_all_visual_hitpoints() -> void:
	var surfaces := _build_main_scene_surfaces()
	var total_steps := 0
	var violation_count := 0

	for px in range(200, 1700, 100):
		for py in range(100, 1000, 100):
			var player := Vector2(px, py)
			for angle_deg in range(0, 360, 15):
				var angle := deg_to_rad(angle_deg)
				var target := player + Vector2(cos(angle), sin(angle)) * 100.0
				var aim := Direction.from_coords(player, target)
				H.reset_counters()
				var path := TestTrace.trace(player, aim, surfaces, GameState.new())

				var violations := _check_all_visual_hitpoints(path, surfaces)
				for step_i in path.steps.size():
					var step: TraceStep = path.steps[step_i]
					if step.hit != null and step.hit.segment != null:
						total_steps += 1
				violation_count += violations.size()

	print("DIAG [sweep] %d/%d hitpoints violate carrier invariant" % [
		violation_count, total_steps])
	assert_eq(violation_count, 0,
		"Zero carrier invariant violations allowed. Found %d/%d" % [
			violation_count, total_steps])

