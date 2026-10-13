extends GutTest

const H := preload("res://tests/test_helpers.gd")
const AA := preload("res://scripts/game/arrow_animator.gd")

func before_each() -> void:
	H.reset_counters()

func _hwall(y: float) -> Surface:
	return RoomBuilder.create_block_surface(Vector2(0, y), Vector2(1200, y), Vector2(600, y))

func _step(path: TracePath, idx: int) -> TraceStep:
	return path.steps[idx]

func _setup_scene() -> Array:
	var w_top := RoomBuilder.create_block_surface(Vector2(560, 240), Vector2(1360, 240), Vector2(960, 240))
	var w_bot := RoomBuilder.create_block_surface(Vector2(1360, 840), Vector2(560, 840), Vector2(960, 840))
	var w_left := RoomBuilder.create_block_surface(Vector2(560, 840), Vector2(560, 240), Vector2(560, 540))
	var m1_seg := Segment.from_coords(Vector2(800, 300), Vector2(800, 780), Vector2(800, 540))
	var m1_refl := ReflectionEffect.new(m1_seg.get_carrier())
	var m1 := Surface.new(m1_seg, SideConfig.new(m1_refl, true), SideConfig.new(null, false), false, false)
	var m2_seg := Segment.from_coords(Vector2(1200, 300), Vector2(1200, 780), Vector2(1200, 540))
	var m2_refl := ReflectionEffect.new(m2_seg.get_carrier())
	var m2 := Surface.new(m2_seg, SideConfig.new(m2_refl, true), SideConfig.new(null, false), false, false)
	return [w_top, w_bot, w_left, m1, m2]

# --- Terminal pass-through in PLANNED mode ---

func test_wall_doesnt_stop_planned() -> void:
	var w := _hwall(400)
	var player := Vector2(600, 500)
	var cursor := Vector2(600, 200)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, [w], GameState.new(),
		ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, [])
	assert_gt(path.cursor_index, 0, "Planned trace should reach cursor through wall")

func test_terminal_stops_physical() -> void:
	var w := _hwall(400)
	var player := Vector2(600, 500)
	var cursor := Vector2(600, 200)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, [w], GameState.new(),
		ray)
	assert_eq(path.cursor_index, -1, "Physical trace should stop at wall before cursor")

# --- Cursor image = direction.end ---

func test_cursor_image_is_direction_end() -> void:
	var w := H.wall(600)
	var player := Vector2(200, 300)
	var cursor := Vector2(400, 300)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, [w], GameState.new(), ray)
	assert_gt(path.cursor_index, 0, "Cursor should be injected")
	var cs := _step(path, path.cursor_index - 1)
	assert_almost_eq(cs.end.x, cursor.x, 1.0, "Cursor step ends at cursor x")
	assert_almost_eq(cs.end.y, cursor.y, 1.0, "Cursor step ends at cursor y")

func test_cursor_with_plan_reachable() -> void:
	# Use full room so the ray can loop back
	var surfaces := _setup_scene()
	var plan: Array = [PlanManager.PlanEntry.new(surfaces[3].id, Side.Value.LEFT)]
	var player := Vector2(960, 500)
	var cursor := Vector2(700, 400)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new(),
		ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, plan, null, cursor)
	assert_gt(path.cursor_index, 1, "Planned trace should reach cursor after plan + loop")

func test_cursor_not_reached_after_nonplan_reflection() -> void:
	# No plan, mirror between player and cursor → physical reflects → diverged → cursor NOT reached
	var m := H.mirror(400)
	var w := H.wall(700)
	var player := Vector2(200, 300)
	var cursor := Vector2(600, 300)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, [m, w], GameState.new(), ray)
	# Mirror reflects before aim point → non-plan effect → plan_matched=false → cursor NOT reached
	assert_eq(path.cursor_index, -1, "Cursor not reached after non-plan reflection")

# --- Physical preview matches physical trace ---

func test_physical_preview_matches() -> void:
	var surfaces := _setup_scene()
	var player := Vector2(960.0, 827.9623)
	var cursor := Vector2(689.7184, 640.1856)
	var aim := Direction.from_coords(player, cursor)
	var aim_ray := Ray.from_coords(player, aim)

	var physical := TestTrace.trace(player, aim, surfaces, GameState.new(),
		aim_ray)
	var planned := TestTrace.trace(player, aim, surfaces, GameState.new(),
		aim_ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, [])
	var ci: int = planned.cursor_index
	if ci < 0:
		ci = planned.steps.size()
	var merged := StepTreeMerge.merge(planned.steps, physical.steps, ci)

	var non_red: Array = []
	for i in merged.size():
		var ms: TraceStep = merged[i]
		if ms.type == StepTypes.Type.ALIGNED or ms.type == StepTypes.Type.ALIGNED_POST_PLANNED or ms.type == StepTypes.Type.DIVERGED_PHYSICAL:
			non_red.append(ms)

	assert_eq(non_red.size(), physical.steps.size(),
		"Non-red count (%d) must match physical count (%d)" % [non_red.size(), physical.steps.size()])

# --- Player block ---

func test_player_block_stops_loop() -> void:
	# No obstacles — ray escapes, loops through infinity, returns to player
	var player := Vector2(500, 300)
	var cursor := Vector2(700, 300)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, [], GameState.new(), ray)
	# Should terminate (player block prevents infinite loop)
	assert_gt(path.steps.size(), 0, "Should have steps")
	# Trace should not hit MAX_HITS (256 steps) — player block should stop it
	assert_lt(path.steps.size(), 256, "Player block should stop before MAX_HITS")

func test_player_block_wall_stops_first() -> void:
	var w := H.wall(600)
	var player := Vector2(200, 300)
	var cursor := Vector2(400, 300)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, [w], GameState.new(), ray)
	# Wall should stop the trace, not the player block
	var last := _step(path, path.steps.size() - 1)
	assert_not_null(last.hit, "Last step should hit the wall")

# --- Repro tests ---

func test_repro_bug1_wall_between() -> void:
	var surfaces := _setup_scene()
	var player := Vector2(960.0, 827.9623)
	var cursor := Vector2(968.0083, 153.2839)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var planned := TestTrace.trace(player, aim, surfaces, GameState.new(),
		ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, [])
	assert_gt(planned.cursor_index, 0, "Planned trace must reach cursor past wall")

func test_repro_bug2_mirror_plan() -> void:
	var surfaces := _setup_scene()
	var player := Vector2(1353.337, 827.9246)
	var cursor := Vector2(1092.138, 794.4713)
	var plan: Array = [PlanManager.PlanEntry.new(surfaces[4].id, Side.Value.LEFT)]
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var planned := TestTrace.trace(player, aim, surfaces, GameState.new(),
		ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, plan, null, cursor)
	# Cursor reached after plan completes and ray loops back
	assert_gt(planned.cursor_index, 1, "Planned must reach cursor after plan + loop")

func test_repro_bug3_off_segment() -> void:
	var surfaces := _setup_scene()
	var player := Vector2(930.0002, 827.9246)
	var cursor := Vector2(717.7476, 825.5289)
	var plan: Array = [PlanManager.PlanEntry.new(surfaces[3].id, Side.Value.LEFT)]
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var planned := TestTrace.trace(player, aim, surfaces, GameState.new(),
		ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, plan, null, cursor)
	assert_gt(planned.cursor_index, 1, "Planned must reach cursor after plan + loop")

# --- Side flip after odd reflections ---

func _setup_three_mirrors() -> Array:
	var w_top := RoomBuilder.create_block_surface(Vector2(560, 240), Vector2(1360, 240), Vector2(960, 240))
	var w_bot := RoomBuilder.create_block_surface(Vector2(1360, 840), Vector2(560, 840), Vector2(960, 840))
	var w_left := RoomBuilder.create_block_surface(Vector2(560, 840), Vector2(560, 240), Vector2(560, 540))
	var m1_seg := Segment.from_coords(Vector2(800, 300), Vector2(800, 780), Vector2(800, 540))
	var m1 := Surface.new(m1_seg, SideConfig.new(ReflectionEffect.new(m1_seg.get_carrier()), true), SideConfig.new(null, false), false, false)
	var m2_seg := Segment.from_coords(Vector2(1200, 300), Vector2(1200, 780), Vector2(1200, 540))
	var m2 := Surface.new(m2_seg, SideConfig.new(ReflectionEffect.new(m2_seg.get_carrier()), true), SideConfig.new(null, false), false, false)
	var m3_seg := Segment.from_coords(Vector2(1000, 400), Vector2(1000, 700), Vector2(1000, 550))
	var m3 := Surface.new(m3_seg, SideConfig.new(null, false), SideConfig.new(ReflectionEffect.new(m3_seg.get_carrier()), true), false, false)
	return [w_top, w_bot, w_left, m1, m2, m3]

func test_side_correct_after_odd_reflections() -> void:
	var surfaces := _setup_three_mirrors()
	var player := Vector2(960.0, 827.9623)
	var cursor := Vector2(857.8936, 783.4509)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new(), ray)
	# Step 2 hits mirror at x=1000 (R=reflect). After 1 reflection, frame conjugating.
	# Side must be flipped → R=reflect applied → frame changes.
	var step1: TraceStep = path.steps[1] if path.steps.size() > 1 else null
	var step2: TraceStep = path.steps[2] if path.steps.size() > 2 else null
	assert_not_null(step2, "Should have step 2")
	if step1 and step2:
		assert_ne(step1.frame_id, step2.frame_id,
			"Step 2 should reflect — side must be flipped after odd reflection")

# --- Wrong carrier reflection bug ---

func test_repro_wrong_carrier_reflection() -> void:
	# WRONG case: cursor at y=736.364. After reflecting off x=800, the hit at
	# x=1000 uses the ORIGINAL carrier Möbius (x=1000) instead of the NORMALIZED
	# carrier (x=600). This produces a backtracking ray instead of proper reflection.
	var surfaces := _setup_three_mirrors()
	var player := Vector2(960.0, 827.9623)
	var cursor := Vector2(820.855, 736.3637)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new(), ray)
	# Should bounce between mirrors ≥5 steps (not backtrack to x=560 at step 3)
	assert_gte(path.steps.size(), 5,
		"Should bounce between mirrors, not backtrack (got %d steps)" % path.steps.size())

func test_repro_correct_carrier_reflection() -> void:
	# CORRECT case: cursor at y=737.366. Very close angle. Should also bounce ≥5 steps.
	var surfaces := _setup_three_mirrors()
	var player := Vector2(960.0, 827.9623)
	var cursor := Vector2(820.855, 737.3656)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new(), ray)
	assert_gte(path.steps.size(), 5,
		"Regression: correct case should still bounce ≥5 steps (got %d steps)" % path.steps.size())

func test_reflection_direction_after_frame_change() -> void:
	# After reflecting off x=800 (going left→right), hitting x=1000 should
	# reflect back to the left (x reverses). The y-slope should be preserved.
	var surfaces := _setup_three_mirrors()
	var player := Vector2(960.0, 827.9623)
	var cursor := Vector2(820.855, 736.3637)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new(), ray)
	if path.steps.size() >= 4:
		# Step 2: (800, ~723) → (1000, ~591) — going RIGHT
		# Step 3: should go LEFT (reflected at x=1000)
		var s2 := _step(path, 2)
		var s3 := _step(path, 3)
		var dir2_x := s2.end.x - s2.start.x  # positive (going right)
		var dir3_x := s3.end.x - s3.start.x  # should be negative (going left)
		assert_gt(dir2_x, 0.0, "Step 2 should go right (toward x=1000)")
		assert_lt(dir3_x, 0.0, "Step 3 should go left (reflected back from x=1000)")

# --- Multi-surface plan: second entry ignored ---

func test_multi_surface_plan_both_effects_applied() -> void:
	# Plan: [m1/LEFT, m3/RIGHT]. Both effects should be applied in PLANNED mode.
	var surfaces := _setup_three_mirrors()
	var player := Vector2(968.1862, 784.5909)
	var cursor := Vector2(881.9186, 614.1374)
	var m1: Surface = surfaces[3]  # id=4, x=800, L=reflect
	var m3: Surface = surfaces[5]  # id=6, x=1000, R=reflect
	var plan: Array = [PlanManager.PlanEntry.new(m1.id, Side.Value.LEFT), PlanManager.PlanEntry.new(m3.id, Side.Value.RIGHT)]
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var planned := TestTrace.trace(player, aim, surfaces, GameState.new(),
		ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, plan, null, cursor)
	# Step 0: hits m1 → frame changes (effect applied)
	# Step 1: hits m3 → frame should change AGAIN (second effect applied)
	var s0 := _step(planned, 0)
	var s1 := _step(planned, 1)
	assert_ne(s0.frame_id, s1.frame_id,
		"Step 1 should have different frame from step 0 (m3 effect applied)")
	# Step 1's frame should differ from step 0 AND from identity
	assert_ne(s1.frame_id, 0,
		"Step 1 frame should not be identity (two effects applied)")

# --- Player block mid-air in planned trace with non-identity frame ---

func test_planned_trace_no_midair_player_block() -> void:
	# Plan with 2 entries. After cursor, planned trace has non-identity frame.
	# Player block should NOT fire (non-identity frame) — only waypoint.
	# Last step should end at a surface or bounds, not mid-air.
	var surfaces := _setup_three_mirrors()
	var m1: Surface = surfaces[3]
	var m3: Surface = surfaces[5]
	var player := Vector2(1143.334, 827.9978)
	var cursor := Vector2(292.3045, 236.4379)
	var plan: Array = [PlanManager.PlanEntry.new(m1.id, Side.Value.LEFT), PlanManager.PlanEntry.new(m3.id, Side.Value.RIGHT)]
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var planned := TestTrace.trace(player, aim, surfaces, GameState.new(),
		ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, plan, null, cursor)
	var last := _step(planned, planned.steps.size() - 1)
	# Should end at a surface hit, not mid-air at the player image
	var near_player_image := last.end.distance_to(Vector2(1543, 828)) < 10.0
	assert_false(near_player_image,
		"Should NOT end at player image mid-air (got %s)" % last.end)

# --- Player block fires mid-air after reflections ---

func test_no_midair_end_after_reflections() -> void:
	# Trace bounces between mirrors, then loops through infinity.
	# Should NOT end mid-air at the player's image — should reach a surface or bounds.
	var surfaces := _setup_three_mirrors()
	var player := Vector2(942.1896, 449.0703)
	var cursor := Vector2(877.9145, 441.8182)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new(), ray)
	# Last step should end at a surface hit or bounds edge, not mid-air
	var last := _step(path, path.steps.size() - 1)
	# Check: last step end should be near a surface or bounds edge
	var bounds := VisualConverter.DEFAULT_BOUNDS
	var near_surface := false
	for surf in surfaces:
		var s: Surface = surf
		var dist_start := _point_to_segment_dist(last.end, s.segment.start.coords, s.segment.end.coords)
		if dist_start < 2.0:
			near_surface = true
			break
	var near_bounds := (last.end.x <= bounds.position.x + 2.0 or
		last.end.x >= bounds.end.x - 2.0 or
		last.end.y <= bounds.position.y + 2.0 or
		last.end.y >= bounds.end.y - 2.0)
	var near_player := last.end.distance_to(player) < 2.0
	assert_true(near_surface or near_bounds or near_player,
		"Trace should end at surface, bounds, or player — not mid-air at %s" % last.end)

func _point_to_segment_dist(p: Vector2, a: Vector2, b: Vector2) -> float:
	var ab := b - a
	var ap := p - a
	var t := clampf(ap.dot(ab) / ab.length_squared(), 0.0, 1.0)
	var closest := a + t * ab
	return p.distance_to(closest)

func test_player_block_in_identity_frame() -> void:
	# No reflections, trace loops. Player block should prevent infinite loop.
	var player := Vector2(500, 300)
	var cursor := Vector2(700, 300)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, [], GameState.new(), ray)
	assert_lt(path.steps.size(), 256, "Should terminate before MAX_HITS")

func test_player_waypoint_after_reflection() -> void:
	# After reflection, player image is a waypoint, trace continues past it.
	var surfaces := _setup_three_mirrors()
	var player := Vector2(942.1896, 449.0703)
	var cursor := Vector2(877.9145, 441.8182)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new(), ray)
	# The trace should continue past the player image (after reflections)
	# and reach an actual surface hit
	var has_hit_after_escape := false
	for i in path.steps.size():
		var s := _step(path, i)
		if s.hit != null and s.hit.on_segment and i > 5:
			has_hit_after_escape = true
	assert_true(has_hit_after_escape,
		"After reflections + escape, trace should hit a real surface")

# --- Arrow animation through infinity: floating-point at screen boundary ---

func _setup_three_mirrors_with_screen_bounds() -> Array:
	var surfaces := _setup_three_mirrors()
	var screen_bounds: Array[Vector4] = [
		Vector4(0, 0, 1920, 0),
		Vector4(1920, 0, 1920, 1080),
		Vector4(1920, 1080, 0, 1080),
		Vector4(0, 1080, 0, 0),
	]
	for line_def in screen_bounds:
		var seg := Segment.from_coords(
			Vector2(line_def.x, line_def.y),
			Vector2(line_def.z, line_def.w),
			Vector2((line_def.x + line_def.z) / 2.0, (line_def.y + line_def.w) / 2.0))
		var config := SideConfig.new(null, false)
		surfaces.append(Surface.new(seg, config, config, false, false))
	return surfaces

func test_repro_infinity_animation_buggy_position():
	var surfaces := _setup_three_mirrors_with_screen_bounds()
	var player := Vector2(1156.668, 827.9246)
	var aim_end := Vector2(-226.4858, 488.9054)
	var aim := Direction.from_coords(player, aim_end)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())
	assert_gt(path.steps.size(), 0, "Should have steps")

func test_rect2_has_point_boundary_sensitivity():
	# Demonstrates the root cause: Rect2.has_point uses strict >= for lower bounds.
	# A Möbius-mapped coordinate at x=-0.000061 (sub-pixel error) is rejected.
	var screen := Rect2(0, 0, 1920, 1080)

	# Point exactly on boundary — passes
	assert_true(screen.has_point(Vector2(0.0, 500.0)),
		"Exact boundary x=0.0 should be on-screen")
	assert_true(screen.has_point(Vector2(500.0, 0.0)),
		"Exact boundary y=0.0 should be on-screen")

	# Point with sub-pixel floating-point error — FAILS (this is the bug)
	assert_false(screen.has_point(Vector2(-0.000061, 642.4609)),
		"x=-0.000061 is rejected by strict >= check — this is the animation bug")
	assert_false(screen.has_point(Vector2(500.0, -0.00003)),
		"y=-0.00003 is also rejected")

	# The arrow animator uses screen.has_point() to decide whether to skip steps.
	# When a Möbius transform maps a screen-edge hit to x=-0.000061 instead of
	# x=0.0, the step is incorrectly skipped, killing the return animation.
	# Fix: expand the screen rect by ~1px tolerance.
	var tolerant_screen := Rect2(-1, -1, 1922, 1082)
	assert_true(tolerant_screen.has_point(Vector2(-0.000061, 642.4609)),
		"With 1px tolerance, sub-pixel error is accepted")

func test_repro_infinity_animation_working_position():
	var surfaces := _setup_three_mirrors_with_screen_bounds()
	var player := Vector2(1253.336, 827.9246)
	var cursor := Vector2(1494.557, 433.8034)
	var m1: Surface = surfaces[3]
	var m3: Surface = surfaces[5]
	var plan: Array = [
		PlanManager.PlanEntry.new(m1.id, Side.Value.LEFT),
		PlanManager.PlanEntry.new(m3.id, Side.Value.RIGHT),
		PlanManager.PlanEntry.new(m1.id, Side.Value.LEFT),
	]
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new(),
		null, -1.0, TraceMode.PHYSICAL, TraceMode.PHYSICAL, plan, null, cursor)
	assert_gt(path.steps.size(), 0, "Should have steps")

# --- AA.advance() unit tests ---

func _make_step(s: Vector2, e: Vector2) -> TraceStep:
	return TraceStep.new(s, e)

func test_advance_basic_interpolation():
	var steps: Array = [
		_make_step(Vector2(100, 100), Vector2(400, 100)),
		_make_step(Vector2(400, 100), Vector2(700, 100)),
		_make_step(Vector2(700, 100), Vector2(1000, 100)),
	]
	var bounds := Rect2(0, 0, 1920, 1080)
	var r := AA.advance(steps, 0, 0.0, Vector2(100, 100), 150.0, bounds)
	assert_almost_eq(r.position.x, 250.0, 1.0, "Arrow at midpoint of step 0")
	assert_almost_eq(r.position.y, 100.0, 1.0)
	assert_eq(r.step_index, 0, "Still on step 0")
	assert_false(r.finished, "Not finished")

func test_advance_completes_all_steps():
	var steps: Array = [
		_make_step(Vector2(100, 100), Vector2(400, 100)),
		_make_step(Vector2(400, 100), Vector2(700, 100)),
		_make_step(Vector2(700, 100), Vector2(1000, 100)),
	]
	var bounds := Rect2(0, 0, 1920, 1080)
	var r := AA.advance(steps, 0, 0.0, Vector2(100, 100), 99999.0, bounds)
	assert_almost_eq(r.position.x, 1000.0, 1.0, "Arrow at last step end")
	assert_true(r.finished, "Animation complete")

func test_advance_offbounds_fast_forward():
	var steps: Array = [
		_make_step(Vector2(100, 100), Vector2(500, 100)),
		_make_step(Vector2(500, 100), Vector2(2500, 100)),
		_make_step(Vector2(2500, 100), Vector2(3000, 100)),
		_make_step(Vector2(100, 500), Vector2(400, 500)),
	]
	var bounds := Rect2(0, 0, 1920, 1080)
	var r := AA.advance(steps, 0, 0.0, Vector2(100, 100), 99999.0, bounds)
	assert_almost_eq(r.position.x, 400.0, 1.0, "Arrow at last step end after fast-forward")
	assert_almost_eq(r.position.y, 500.0, 1.0)
	assert_true(r.finished, "Animation complete")

func test_advance_partial_step_goes_offbounds():
	var steps: Array = [
		_make_step(Vector2(1800, 500), Vector2(2100, 500)),
	]
	var bounds := Rect2(0, 0, 1920, 1080)
	var r := AA.advance(steps, 0, 0.0, Vector2(1800, 500), 200.0, bounds)
	assert_true(r.finished, "Single step that goes off-bounds should finish")

func test_advance_boundary_floating_point():
	var steps: Array = [
		_make_step(Vector2(-806.0, 840.0), Vector2(-0.000061, 642.46)),
		_make_step(Vector2(-0.000061, 642.46), Vector2(560.0, 505.0)),
	]
	var bounds := Rect2(0, 0, 1920, 1080)
	var step0_len := Vector2(-806.0, 840.0).distance_to(Vector2(-0.000061, 642.46))
	var r := AA.advance(steps, 0, 0.0, Vector2(-806.0, 840.0), step0_len + 100.0, bounds)
	assert_eq(r.step_index, 1, "Arrow in step 1 (not skipped past it)")
	assert_false(r.finished, "Not finished — still mid-step 1")
	assert_gt(r.position.x, 0.0, "Arrow past the left edge")
	assert_lt(r.position.x, 560.0, "Arrow before step 1 end")

func test_advance_wrap_through_infinity():
	var steps: Array = [
		_make_step(Vector2(500, 200), Vector2(1910, 100)),
		_make_step(Vector2(1910, 100), Vector2(2500, -50)),
		_make_step(Vector2(-500, 1100), Vector2(-100, 900)),
		_make_step(Vector2(-100, 900), Vector2(50, 700)),
		_make_step(Vector2(50, 700), Vector2(400, 600)),
		_make_step(Vector2(400, 600), Vector2(700, 500)),
	]
	var bounds := Rect2(0, 0, 1920, 1080)
	var r := AA.advance(steps, 0, 0.0, Vector2(500, 200), 99999.0, bounds)
	assert_almost_eq(r.position.x, 700.0, 1.0, "Arrow at final step end")
	assert_almost_eq(r.position.y, 500.0, 1.0)
	assert_true(r.finished, "Animation complete after wrap")

func test_advance_preserves_distance_after_fast_forward():
	var steps: Array = [
		_make_step(Vector2(100, 100), Vector2(500, 100)),
		_make_step(Vector2(500, 100), Vector2(2500, 100)),
		_make_step(Vector2(2500, 100), Vector2(3000, 100)),
		_make_step(Vector2(100, 500), Vector2(400, 500)),
	]
	var bounds := Rect2(0, 0, 1920, 1080)
	# 400 (step 0) + 2000 (step 1) = 2400 consumed; 200 remaining for step 3
	var r := AA.advance(steps, 0, 0.0, Vector2(100, 100), 2600.0, bounds)
	assert_almost_eq(r.position.x, 300.0, 1.0, "Distance preserved: 200px into 300px step")
	assert_almost_eq(r.position.y, 500.0, 1.0)
	assert_false(r.finished, "Not finished — still mid-step 3")
