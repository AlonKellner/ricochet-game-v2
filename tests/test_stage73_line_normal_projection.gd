extends GutTest

const H := preload("res://tests/test_helpers.gd")
const TOL := Vector2(1.0, 1.0)

func before_each() -> void:
	H.reset_counters()

# --- Helpers ---

func _vertical_seg(x: float) -> Segment:
	return Segment.from_coords(Vector2(x, 100), Vector2(x, 500), Vector2(x, 300))

func _horizontal_seg(y: float) -> Segment:
	return Segment.from_coords(Vector2(100, y), Vector2(500, y), Vector2(300, y))

func _projective_surface(seg: Segment, project_back: bool = false) -> Surface:
	var effect := LineNormalProjection.new(project_back)
	var config := SideConfig.new(effect, true)
	return Surface.new(seg, config, config, false, false)

func _room_walls() -> Array:
	return [
		H.wall(0),
		H.wall(800),
		RoomBuilder.create_block_surface(Vector2(0, 0), Vector2(800, 0), Vector2(400, 0)),
		RoomBuilder.create_block_surface(Vector2(0, 600), Vector2(800, 600), Vector2(400, 600)),
	]

func _trace(origin: Vector2, cursor: Vector2, surfaces: Array) -> TracePath:
	var aim := Direction.from_coords(origin, cursor)
	return TestTrace.trace(origin, aim, surfaces, GameState.new(),
		null, -1.0, TraceMode.PHYSICAL, TraceMode.PHYSICAL, [], null, cursor)

# Direction of the step after the first hit on proj_surf (beam trace; the
# effects' closed-form apply_forward was removed with the legacy solver).
func _exit_dir(surfaces: Array, proj_surf: Surface, player: Vector2, cursor: Vector2) -> Vector2:
	var path := _trace(player, cursor, surfaces)
	for i in range(path.steps.size() - 1):
		var step: TraceStep = path.steps[i]
		if step.hit != null and step.surface_id == proj_surf.id:
			var post: TraceStep = path.steps[i + 1]
			return (post.end - post.start).normalized()
	fail_test("no step after the projective hit (player %s, cursor %s)" % [player, cursor])
	return Vector2.ZERO

# Exit direction off a single projective segment in the room.
func _exit_dir_off(seg: Segment, project_back: bool, player: Vector2, cursor: Vector2) -> Vector2:
	var surfaces := _room_walls()
	var proj_surf := _projective_surface(seg, project_back)
	surfaces.append(proj_surf)
	return _exit_dir(surfaces, proj_surf, player, cursor)

# --- Test 1: exit direction off a vertical line ---

func test_stage73_apply_forward_vertical_line() -> void:
	# hit at (250, 200), approached from the left
	var dir_vec := _exit_dir_off(_vertical_seg(250.0), false, Vector2(100, 100), Vector2(400, 300))
	assert_almost_eq(absf(dir_vec.x), 1.0, 0.01,
		"Direction should be horizontal (|x| = 1)")
	assert_almost_eq(dir_vec.y, 0.0, 0.01,
		"Direction should be horizontal (y = 0)")

# --- Test 2: direction is perpendicular to surface ---

func test_stage73_apply_forward_direction_is_perpendicular() -> void:
	# Vertical line -> horizontal output (hit at (250, 200))
	var v_dir := _exit_dir_off(_vertical_seg(250.0), false, Vector2(100, 100), Vector2(400, 300))
	assert_almost_eq(absf(v_dir.dot(Vector2(0, 1))), 0.0, 0.01,
		"Vertical line: output should be perpendicular to line direction")
	# Horizontal line -> vertical output (hit at (200, 300))
	var h_dir := _exit_dir_off(_horizontal_seg(300.0), false, Vector2(150, 200), Vector2(250, 400))
	assert_almost_eq(absf(h_dir.dot(Vector2(1, 0))), 0.0, 0.01,
		"Horizontal line: output should be perpendicular to line direction")

# --- Test 5: S10 - projective hit starts a fresh frame ---

# Beam semantics: a projective clears the transform stack and starts a new
# epoch, so the steps after it get a frame_id never seen before the hit
# (legacy reset to the identity frame instead).
func test_stage73_S10_frame_resets_to_identity() -> void:
	var surfaces := _room_walls()
	# Portal pair to change frame from identity before hitting projective
	var source_seg := Segment.from_coords(
		Vector2(200, 100), Vector2(200, 500), Vector2(200, 300))
	var portal_data := RigidMotionEffect.create_portal_pair(source_seg, 0.0, Vector2(100, 0))
	var src_cfg := SideConfig.new(portal_data.source_effect, true)
	var tgt_cfg := SideConfig.new(portal_data.target_effect, true)
	var source_surf := Surface.new(source_seg, src_cfg, src_cfg, false, false)
	var target_surf := Surface.new(portal_data.target_segment, tgt_cfg, tgt_cfg, false, false)
	surfaces.append(source_surf)
	surfaces.append(target_surf)
	# Projective surface after portal exit
	var proj_seg := _vertical_seg(500.0)
	var proj_surf := _projective_surface(proj_seg)
	surfaces.append(proj_surf)
	# Horizontal trace through portal then projective
	var path := _trace(Vector2(50, 300), Vector2(750, 300), surfaces)
	var found_post_projective := false
	var before := {}
	for i in range(path.steps.size()):
		var step: TraceStep = path.steps[i]
		before[step.frame_id] = true
		if step.hit != null and step.surface_id == proj_surf.id:
			if i + 1 < path.steps.size():
				var post_step: TraceStep = path.steps[i + 1]
				assert_false(before.has(post_step.frame_id),
					"Frame after the projective hit is a fresh frame (new epoch), got %d" % post_step.frame_id)
				assert_ne(post_step.frame_id, TraceStep.IDENTITY_FRAME,
					"No transform carried: a new epoch, not the identity of epoch 0")
				found_post_projective = true
			break
	assert_true(found_post_projective, "Should have a step after the projective surface")

# --- Test 6: new direction created ---

func test_stage73_new_direction_created() -> void:
	# incoming (100,100)->(250,200) hits the line at (250, 200); the outgoing
	# direction is a new one, perpendicular to the line
	var incoming_dir := (Vector2(250, 200) - Vector2(100, 100)).normalized()
	var out_dir := _exit_dir_off(_vertical_seg(250.0), false, Vector2(100, 100), Vector2(400, 300))
	assert_true(out_dir.distance_to(incoming_dir) > 0.1,
		"Outgoing direction should differ from the incoming one")
	assert_almost_eq(absf(out_dir.dot(Vector2(0, 1))), 0.0, 0.01,
		"Outgoing direction should be perpendicular to vertical line")

# --- Test 7: trace through projective surface ---

func test_stage73_trace_through_projective_surface() -> void:
	var surfaces := _room_walls()
	var proj_seg := _vertical_seg(400.0)
	var proj_surf := _projective_surface(proj_seg)
	surfaces.append(proj_surf)
	# Diagonal approach: player at (100, 200) aiming at (700, 400)
	var path := _trace(Vector2(100, 200), Vector2(700, 400), surfaces)
	assert_gt(path.steps.size(), 1, "Should have multiple steps")
	# Find the projective surface hit and check the post-projection step
	var post_proj_idx := -1
	for i in range(path.steps.size()):
		var step: TraceStep = path.steps[i]
		if step.surface_id == proj_surf.id:
			post_proj_idx = i + 1
			break
	assert_gt(post_proj_idx, 0, "Should find the projective surface hit")
	assert_lt(post_proj_idx, path.steps.size(), "Should have a step after projective")
	if post_proj_idx > 0 and post_proj_idx < path.steps.size():
		var post_step: TraceStep = path.steps[post_proj_idx]
		var step_dir := (post_step.end - post_step.start).normalized()
		assert_almost_eq(absf(step_dir.y), 0.0, 0.05,
			"Post-projection step should be horizontal (perpendicular to vertical line)")
		# beam: a projective starts a new epoch (fresh frame), not epoch-0 identity
		assert_ne(post_step.frame_id, path.steps[post_proj_idx - 1].frame_id,
			"Frame changes at the projective hit")

# --- Test 8: orange rendering ---

func test_stage73_orange_rendering() -> void:
	var effect := LineNormalProjection.new()
	assert_eq(effect.get_display_color(), Color(1.0, 0.8, 0.2),
		"Projective surface should render in orange")
	assert_eq(effect.get_display_name(), "line_normal",
		"Display name should be 'line_normal'")

# --- Test 9: S16 - no NaN/Inf ---

func test_stage73_S16_no_nan_inf() -> void:
	var surfaces := _room_walls()
	var proj_surf := _projective_surface(_vertical_seg(250.0))
	surfaces.append(proj_surf)
	var path := _trace(Vector2(100, 100), Vector2(400, 300), surfaces)
	assert_gt(path.steps.size(), 1, "Should have steps")
	for s: TraceStep in path.steps:
		for v: Vector2 in [s.start, s.end]:
			assert_false(is_nan(v.x) or is_nan(v.y), "no NaN in step coords")
			assert_false(is_inf(v.x) or is_inf(v.y), "no Inf in a closed room")
	var dir := _exit_dir(surfaces, proj_surf, Vector2(100, 100), Vector2(400, 300))
	assert_false(is_nan(dir.x), "direction.x should not be NaN")
	assert_false(is_nan(dir.y), "direction.y should not be NaN")

# --- Test 10: project_back exits on same side ---

func test_stage73_project_back_exits_same_side() -> void:
	var seg := _vertical_seg(250.0)
	var through_dir := _exit_dir_off(seg, false, Vector2(100, 100), Vector2(400, 300))
	var back_dir := _exit_dir_off(seg, true, Vector2(100, 100), Vector2(400, 300))
	assert_almost_eq(through_dir.x + back_dir.x, 0.0, 0.01,
		"Through and back should have opposite x directions")
	assert_almost_eq(through_dir.y, 0.0, 0.01, "Through should be horizontal")
	assert_almost_eq(back_dir.y, 0.0, 0.01, "Back should be horizontal")

# --- Test 11: project_back is still perpendicular ---

func test_stage73_project_back_perpendicular() -> void:
	# Vertical line -> horizontal output
	var v_dir := _exit_dir_off(_vertical_seg(250.0), true, Vector2(100, 100), Vector2(400, 300))
	assert_almost_eq(absf(v_dir.dot(Vector2(0, 1))), 0.0, 0.01,
		"Back on vertical line: output perpendicular to line direction")
	# Horizontal line -> vertical output
	var h_dir := _exit_dir_off(_horizontal_seg(300.0), true, Vector2(150, 200), Vector2(250, 400))
	assert_almost_eq(absf(h_dir.dot(Vector2(1, 0))), 0.0, 0.01,
		"Back on horizontal line: output perpendicular to line direction")

# --- Test 12: project_back color is DARK_ORANGE ---

func test_stage73_project_back_color_dark_orange() -> void:
	var back := LineNormalProjection.new(true)
	var through := LineNormalProjection.new(false)
	assert_eq(back.get_display_color(), Color(1.0, 0.4, 0.1),
		"Back-projection should render DARK_ORANGE")
	assert_eq(through.get_display_color(), Color(1.0, 0.8, 0.2),
		"Through-projection should still render ORANGE")

# --- Test 13: project_back display name ---

func test_stage73_project_back_display_name() -> void:
	var back := LineNormalProjection.new(true)
	assert_eq(back.get_display_name(), "line_normal_back",
		"Back-projection display name should be 'line_normal_back'")

# --- Test 14: project_back trace exits on same side ---

func test_stage73_project_back_trace() -> void:
	var surfaces := _room_walls()
	var proj_seg := _vertical_seg(400.0)
	var proj_surf := _projective_surface(proj_seg, true)
	surfaces.append(proj_surf)
	# Player at (100, 300) approaches from RIGHT side (x < 400)
	var path := _trace(Vector2(100, 300), Vector2(700, 450), surfaces)
	assert_gt(path.steps.size(), 1, "Should have multiple steps")
	var post_proj_idx := -1
	for i in range(path.steps.size()):
		if path.steps[i].surface_id == proj_surf.id:
			post_proj_idx = i + 1
			break
	assert_gt(post_proj_idx, 0, "Should find the projective surface hit")
	assert_lt(post_proj_idx, path.steps.size(), "Should have a step after projective")
	if post_proj_idx > 0 and post_proj_idx < path.steps.size():
		var post_step: TraceStep = path.steps[post_proj_idx]
		var step_dir := (post_step.end - post_step.start).normalized()
		assert_almost_eq(absf(step_dir.y), 0.0, 0.05,
			"Post-back-projection should be horizontal (perpendicular)")
		assert_lt(step_dir.x, 0.0,
			"Post-back-projection should go back toward entry side (negative x)")

# --- Test 15: default constructor is through-projection (backward compat) ---

func test_stage73_default_is_through() -> void:
	var effect := LineNormalProjection.new()
	assert_eq(effect.get_display_color(), Color(1.0, 0.8, 0.2),
		"Default should be through-projection (ORANGE)")
	assert_eq(effect.get_display_name(), "line_normal",
		"Default display name should be 'line_normal'")
	var seg := _vertical_seg(250.0)
	# default-constructed effect vs explicit project_back = false
	var surfaces := _room_walls()
	var default_surf := Surface.new(seg, SideConfig.new(effect, true), SideConfig.new(effect, true), false, false)
	surfaces.append(default_surf)
	var dir_default := _exit_dir(surfaces, default_surf, Vector2(100, 100), Vector2(400, 300))
	var dir_explicit := _exit_dir_off(seg, false, Vector2(100, 100), Vector2(400, 300))
	assert_almost_eq(dir_default, dir_explicit, Vector2(0.01, 0.01),
		"Default and explicit false should produce same direction")
