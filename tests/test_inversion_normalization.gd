extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

func _build_debug_scene() -> Dictionary:
	var room_walls := RoomBuilder.create_room_surfaces(Rect2(160, 90, 1600, 900))
	var mirror_left := _mirror_line(Vector2(500, 200), Vector2(500, 700))
	var mirror_right := _mirror_line(Vector2(1400, 300), Vector2(1400, 800))
	var mirror_bottom := _mirror_line(Vector2(700, 800), Vector2(1200, 800))
	var mirror_mid := _mirror_right_line(Vector2(960, 200), Vector2(960, 500))
	var inv_seg := Segment.from_coords(Vector2(1100, 400), Vector2(1100, 700), Vector2(1230, 550))
	var inv_effect := CircleInversionEffect.new(inv_seg.get_carrier())
	var inv_surf := Surface.new(inv_seg, SideConfig.new(inv_effect, true), SideConfig.new(null, false), false, true)
	var screen_bounds := _screen_bounds()
	var surfaces: Array = []
	surfaces.append_array(room_walls)
	surfaces.append(mirror_left)
	surfaces.append(mirror_right)
	surfaces.append(mirror_bottom)
	surfaces.append(mirror_mid)
	surfaces.append(inv_surf)
	surfaces.append_array(screen_bounds)
	return {"surfaces": surfaces, "inversion": inv_surf}

func _mirror_line(start: Vector2, end_v: Vector2) -> Surface:
	var mid := (start + end_v) / 2.0
	var seg := Segment.from_coords(start, end_v, mid)
	var refl := ReflectionEffect.new(seg.get_carrier())
	return Surface.new(seg, SideConfig.new(refl, true), SideConfig.new(null, false), false, false)

func _mirror_right_line(start: Vector2, end_v: Vector2) -> Surface:
	var mid := (start + end_v) / 2.0
	var seg := Segment.from_coords(start, end_v, mid)
	var refl := ReflectionEffect.new(seg.get_carrier())
	return Surface.new(seg, SideConfig.new(null, false), SideConfig.new(refl, true), false, false)

func _screen_bounds() -> Array:
	var result: Array = []
	var bounds_defs := [
		[Vector2(0, 0), Vector2(1920, 0)],
		[Vector2(1920, 0), Vector2(1920, 1080)],
		[Vector2(1920, 1080), Vector2(0, 1080)],
		[Vector2(0, 1080), Vector2(0, 0)],
	]
	for bd in bounds_defs:
		var s: Vector2 = bd[0]
		var e: Vector2 = bd[1]
		var config := SideConfig.new(null, false)
		result.append(Surface.new(Segment.from_coords(s, e, (s + e) / 2.0), config, config, false, false))
	return result

# ==========================================================================
# Phase 1: Reproduce the bug — exact debug state
# ==========================================================================

func test_phase1_trace_hits_inversion_surface() -> void:
	var scene := _build_debug_scene()
	var surfaces: Array = scene.surfaces
	var inv_surf: Surface = scene.inversion
	var player := Vector2(1194.262, 755.5463)
	var cursor := Vector2(1183.232, 715.3248)
	var aim := Direction.from_coords(player, cursor)
	var aim_ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new(),
		aim_ray, -1.0,
		TraceMode.PHYSICAL, TraceMode.PHYSICAL, [], null, cursor)
	var hit_inversion := false
	for step in path.steps:
		var s: TraceStep = step
		if s.hit != null and s.hit.segment == inv_surf.segment:
			hit_inversion = true
	assert_true(hit_inversion, "Trace should hit the inversion arc surface")

func test_phase1_post_inversion_hits_surfaces() -> void:
	var scene := _build_debug_scene()
	var surfaces: Array = scene.surfaces
	var player := Vector2(1194.262, 755.5463)
	var cursor := Vector2(1183.232, 715.3248)
	var aim := Direction.from_coords(player, cursor)
	var aim_ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new(),
		aim_ray, -1.0,
		TraceMode.PHYSICAL, TraceMode.PHYSICAL, [], null, cursor)
	var found_inversion_frame := false
	var post_inversion_hits := 0
	for step in path.steps:
		var s: TraceStep = step
		if s.frame_id != TraceStep.IDENTITY_FRAME:
			found_inversion_frame = true
			if s.hit != null:
				post_inversion_hits += 1
	assert_true(found_inversion_frame, "Should have steps in inverted frame")
	gut.p("Post-inversion surface hits: %d" % post_inversion_hits)
	assert_gt(post_inversion_hits, 0,
		"BUG: After inversion, ray should hit surfaces (walls) but escapes to bounds instead")

func test_phase1_post_inversion_has_surface_hits() -> void:
	var scene := _build_debug_scene()
	var surfaces: Array = scene.surfaces
	var player := Vector2(1194.262, 755.5463)
	var cursor := Vector2(1183.232, 715.3248)
	var aim := Direction.from_coords(player, cursor)
	var aim_ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new(),
		aim_ray, -1.0,
		TraceMode.PHYSICAL, TraceMode.PHYSICAL, [], null, cursor)
	var hit_count := 0
	for step in path.steps:
		var s: TraceStep = step
		if s.frame_id != TraceStep.IDENTITY_FRAME and s.hit != null:
			hit_count += 1
	gut.p("Post-inversion surface hits: %d" % hit_count)
	assert_gt(hit_count, 0,
		"After inversion, ray should hit surfaces (walls bounce correctly)")

# ==========================================================================
# Phase 3: Prove escape-step arc flag issue
# ==========================================================================

func test_phase3_all_inversive_steps_are_arcs() -> void:
	var scene := _build_debug_scene()
	var surfaces: Array = scene.surfaces
	var player := Vector2(1194.262, 755.5463)
	var cursor := Vector2(1183.232, 715.3248)
	var aim := Direction.from_coords(player, cursor)
	var aim_ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new(),
		aim_ray, -1.0,
		TraceMode.PHYSICAL, TraceMode.PHYSICAL, [], null, cursor)
	var inv_surf: Surface = scene.inversion
	var inversive_non_arc_steps := 0
	var inversive_total := 0
	# beam steps carry no frame: the inversive frame is "after the inversion
	# hit, until the frame returns to identity" (a re-hit pops the inversion)
	var inverted := false
	for step in path.steps:
		var s: TraceStep = step
		if inverted and s.frame_id == TraceStep.IDENTITY_FRAME:
			inverted = false
		if inverted and s.hit != null:
			inversive_total += 1
			if not s.is_arc_step:
				inversive_non_arc_steps += 1
		if s.hit != null and s.surface_id == inv_surf.id and s.hit_on_segment:
			inverted = true
	gut.p("Inversive steps: %d total, %d non-arc" % [inversive_total, inversive_non_arc_steps])
	assert_gt(inversive_total, 0, "Should have steps in inversive frame")
	assert_eq(inversive_non_arc_steps, 0,
		"All steps in inversive frame should have is_arc_step=true")
