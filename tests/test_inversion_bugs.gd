extends GutTest

func before_each() -> void:
	Surface.reset_id_counter()

func _build_scene_surfaces() -> Array:
	var surfs: Array = []
	# Room walls: Rect2(160, 90, 1600, 900)
	surfs.append_array(RoomBuilder.create_room_surfaces(Rect2(160, 90, 1600, 900)))
	# mirror_lines (left-side reflect)
	for def in [Vector4(500, 200, 500, 700), Vector4(1400, 300, 1400, 800), Vector4(700, 800, 1200, 800)]:
		var seg := Segment.from_coords(Vector2(def.x, def.y), Vector2(def.z, def.w),
			Vector2((def.x + def.z) / 2.0, (def.y + def.w) / 2.0))
		var carrier := seg.get_carrier()
		var refl := ReflectionEffect.new(carrier)
		surfs.append(Surface.new(seg, SideConfig.new(refl, true), SideConfig.new(null, false), false, false))
	# mirror_right_lines
	for def in [Vector4(960, 200, 960, 500)]:
		var seg := Segment.from_coords(Vector2(def.x, def.y), Vector2(def.z, def.w),
			Vector2((def.x + def.z) / 2.0, (def.y + def.w) / 2.0))
		var carrier := seg.get_carrier()
		var refl := ReflectionEffect.new(carrier)
		surfs.append(Surface.new(seg, SideConfig.new(null, false), SideConfig.new(refl, true), false, false))
	# inversion_left_arcs: (1100, 400, 1100, 700, 1230, 550)
	var inv_seg := Segment.from_coords(Vector2(1100, 400), Vector2(1100, 700), Vector2(1230, 550))
	var inv_carrier := inv_seg.get_carrier()
	var inv_effect := CircleInversionEffect.new(inv_carrier)
	surfs.append(Surface.new(inv_seg, SideConfig.new(inv_effect, true), SideConfig.new(null, false), false, false))
	# Screen boundary pass-throughs
	for def in [Vector4(0, 0, 1920, 0), Vector4(1920, 0, 1920, 1080),
				Vector4(1920, 1080, 0, 1080), Vector4(0, 1080, 0, 0)]:
		var seg := Segment.from_coords(Vector2(def.x, def.y), Vector2(def.z, def.w),
			Vector2((def.x + def.z) / 2.0, (def.y + def.w) / 2.0))
		surfs.append(Surface.new(seg, SideConfig.new(null, false), SideConfig.new(null, false), false, false))
	return surfs

# === STEP 1: Reproduce the infinite loop ===

func test_step1_reproduce_infinite_loop() -> void:
	var surfs := _build_scene_surfaces()
	var player := Vector2(1309.974, 816.636)
	var cursor := Vector2(1244.296, 594.100)
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, surfs, GameState.new())
	gut.p("Step count: %d" % path.steps.size())
	assert_lt(path.steps.size(), 50, "Trace should not produce hundreds of steps (got %d)" % path.steps.size())

# === STEP 2: Instrument the trace — per-step diagnostics ===

func test_step2_trace_diagnostics() -> void:
	var surfs := _build_scene_surfaces()
	var player := Vector2(1309.974, 816.636)
	var cursor := Vector2(1244.296, 594.100)
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, surfs, GameState.new())
	gut.p("Total steps: %d" % path.steps.size())
	var limit: int = mini(path.steps.size(), 20)
	for i in limit:
		var s: TraceStep = path.steps[i]
		var hit_info := "no_hit"
		if s.hit != null:
			hit_info = "t=%.4f on_seg=%s seg_line=%s" % [s.hit.t, s.hit.on_segment, s.hit.segment.is_line()]
		var step_len := s.start.distance_to(s.end)
		gut.p("  [%d] fid=%d start=%s end=%s len=%.4f is_arc=%s %s" % [
			i, s.frame_id, s.start, s.end, step_len, s.is_arc_step, hit_info])
	# Check for zero-length steps
	var zero_count := 0
	for s in path.steps:
		var step: TraceStep = s
		if step.start.distance_to(step.end) < 0.01:
			zero_count += 1
	gut.p("Zero-length steps: %d" % zero_count)
	assert_eq(zero_count, 0, "Should have no zero-length steps")

# === STEP 4: Segment exclusion after re-normalization ===

func test_step4_segment_exclusion_after_renorm() -> void:
	# Minimal scene: just the inversion surface + walls
	var inv_seg := Segment.from_coords(Vector2(1100, 400), Vector2(1100, 700), Vector2(1230, 550))
	var inv_carrier := inv_seg.get_carrier()
	var inv_effect := CircleInversionEffect.new(inv_carrier)
	var inv_surf := Surface.new(inv_seg, SideConfig.new(inv_effect, true), SideConfig.new(null, false), false, false)
	var walls := RoomBuilder.create_room_surfaces(Rect2(160, 90, 1600, 900))
	var surfs: Array = walls + [inv_surf]

	# Use a ray that approaches the arc from OUTSIDE (to trigger the effect)
	var player := Vector2(1400, 550)
	var cursor := Vector2(900, 550)
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, surfs, GameState.new())

	gut.p("Step count: %d" % path.steps.size())
	var limit2: int = mini(path.steps.size(), 15)
	for i in limit2:
		var s: TraceStep = path.steps[i]
		var hit_info := "no_hit"
		if s.hit != null:
			hit_info = "t=%.4f on_seg=%s" % [s.hit.t, s.hit.on_segment]
		gut.p("  [%d] fid=%d %s->%s %s" % [i, s.frame_id, s.start, s.end, hit_info])

	# Check: does the trace loop?
	if path.steps.size() > 20:
		var s10: TraceStep = path.steps[10]
		var s13: TraceStep = path.steps[13]
		var repeating := s10.start.distance_to(s13.start) < 1.0
		gut.p("Steps 10 and 13 same start: %s (dist=%.4f)" % [repeating, s10.start.distance_to(s13.start)])

	assert_lt(path.steps.size(), 50, "Should not loop (got %d steps)" % path.steps.size())

# === BUG 2: Post-inversion aim point ignores surfaces ===

func test_bug2_reproduce_aim_ignores_surfaces() -> void:
	var surfs := _build_scene_surfaces()
	var player := Vector2(1301.632, 932.631)
	var cursor := Vector2(1112.158, 507.941)
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, surfs, GameState.new())
	gut.p("Steps: %d" % path.steps.size())
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		var hit_info := "no_hit"
		if s.hit != null:
			hit_info = "t=%.4f on_seg=%s seg_line=%s" % [s.hit.t, s.hit.on_segment, s.hit.segment.is_line()]
		gut.p("  [%d] fid=%d start=%s end=%s is_arc=%s %s" % [
			i, s.frame_id, s.start, s.end, s.is_arc_step, hit_info])
	# After inversion, the trace should still interact with walls (block surfaces)
	# The user reports steps 2-4 are all escape/no_hit, passing through walls
	var escape_count := 0
	for s in path.steps:
		var step: TraceStep = s
		if step.hit == null:
			escape_count += 1
	gut.p("Escape (no_hit) steps: %d out of %d" % [escape_count, path.steps.size()])
	# There should be at most 2 escape steps (forward + return from infinity)
	# The user sees 3 escape steps passing through blocking surfaces
	assert_lt(escape_count, 3, "Should not have multiple escape steps passing through walls")

# === BUG 3: Three user-reported cases ===

func _trace_and_diagnose(player: Vector2, cursor: Vector2, label: String) -> TracePath:
	var surfs := _build_scene_surfaces()
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, surfs, GameState.new())
	gut.p("--- %s ---" % label)
	gut.p("Player=%s Cursor=%s Steps=%d" % [player, cursor, path.steps.size()])
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		var hit_info := "no_hit"
		if s.hit != null:
			hit_info = "t=%.4f on_seg=%s line=%s side=%d" % [
				s.hit.t, s.hit.on_segment, s.hit.segment.is_line(), s.hit.side]
		gut.p("  [%d] fid=%d arc=%s %s->%s %s" % [
			i, s.frame_id, s.is_arc_step, s.start, s.end, hit_info])
	return path

func test_bug3_case1_line_ignores_surfaces() -> void:
	var path := _trace_and_diagnose(
		Vector2(1370.004, 540.0), Vector2(1301.356, 503.933),
		"Case 1: line ignores surfaces after inversion")
	# After inversion, post-aim steps should still hit walls
	var post_inversion_escapes := 0
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		if s.frame_id != 0 and s.hit == null:
			post_inversion_escapes += 1
	assert_lt(post_inversion_escapes, 3,
		"Post-inversion should not have 3+ escape steps (got %d)" % post_inversion_escapes)

func test_bug3_case2_cursor_inside_circle() -> void:
	var path := _trace_and_diagnose(
		Vector2(1346.67, 540.0), Vector2(1144.192, 495.918),
		"Case 2: cursor inside circle carrier")
	var post_inversion_escapes := 0
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		if s.frame_id != 0 and s.hit == null:
			post_inversion_escapes += 1
	assert_lt(post_inversion_escapes, 3,
		"Post-inversion should not have 3+ escape steps (got %d)" % post_inversion_escapes)
