extends GutTest

const H := preload("res://tests/test_helpers.gd")
const TOL := Vector2(1.0, 1.0)

func before_each() -> void:
	H.reset_counters()

# --- Helpers ---

# Right semicircle: center (300,300), r=100
# LEFT = outside circle, RIGHT = inside circle
func _standard_arc() -> Segment:
	return Segment.from_coords(Vector2(300, 200), Vector2(300, 400), Vector2(400, 300))

func _projective_arc_surface(seg: Segment, normal: Vector2, project_back: bool = false) -> Surface:
	var effect := CircleDirectionalProjection.new(normal, project_back)
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

func _exit_dir_off(seg: Segment, effect: Effect, player: Vector2, cursor: Vector2) -> Vector2:
	var surfaces := _room_walls()
	var cfg := SideConfig.new(effect, true)
	var proj_surf := Surface.new(seg, cfg, cfg, false, false)
	surfaces.append(proj_surf)
	return _exit_dir(surfaces, proj_surf, player, cursor)

# --- Test 1: exit direction from inside (RIGHT) ---

func test_stage75_exit_direction_from_right() -> void:
	# from inside the circle, hit at (400, 300)
	var dir := _exit_dir_off(_standard_arc(), CircleDirectionalProjection.new(Vector2(1, 0)),
		Vector2(330, 300), Vector2(450, 300))
	assert_almost_eq(dir.x, 1.0, 0.01, "From inside: exit should be +normal (rightward)")
	assert_almost_eq(dir.y, 0.0, 0.01, "From inside: exit y should be 0")

# --- Test 2: exit direction from outside (LEFT) ---

func test_stage75_exit_direction_from_left() -> void:
	# from outside the circle, hit at (400, 300)
	var dir := _exit_dir_off(_standard_arc(), CircleDirectionalProjection.new(Vector2(1, 0)),
		Vector2(600, 300), Vector2(350, 300))
	assert_almost_eq(dir.x, -1.0, 0.01, "From outside: exit should be -normal (leftward)")
	assert_almost_eq(dir.y, 0.0, 0.01, "From outside: exit y should be 0")

# --- Test 3: hit position independent ---

func test_stage75_hit_position_independent() -> void:
	# from the center (300, 300) toward the arc points (400, 300),
	# (350, 213.4) and (350, 386.6)
	var player := Vector2(300, 300)
	var hits := [Vector2(400, 300), Vector2(350, 213.4), Vector2(350, 386.6)]
	var dirs := []
	for h: Vector2 in hits:
		var cursor := player + (h - player) * 1.5
		dirs.append(_exit_dir_off(_standard_arc(), CircleDirectionalProjection.new(Vector2(1, 0)), player, cursor))
	for i in range(1, hits.size()):
		assert_almost_eq(dirs[i], dirs[0], Vector2(0.01, 0.01),
			"Hit at position %d should produce same direction as position 0" % i)

# --- Test 6: S10 - frame resets ---

func test_stage75_S10_frame_resets() -> void:
	var surfaces := _room_walls()
	var source_seg := Segment.from_coords(
		Vector2(100, 100), Vector2(100, 500), Vector2(100, 300))
	var portal_data := RigidMotionEffect.create_portal_pair(source_seg, 0.0, Vector2(100, 0))
	var src_cfg := SideConfig.new(portal_data.source_effect, true)
	var tgt_cfg := SideConfig.new(portal_data.target_effect, true)
	var source_surf := Surface.new(source_seg, src_cfg, src_cfg, false, false)
	var target_surf := Surface.new(portal_data.target_segment, tgt_cfg, tgt_cfg, false, false)
	surfaces.append(source_surf)
	surfaces.append(target_surf)
	var proj_seg := _standard_arc()
	var proj_surf := _projective_arc_surface(proj_seg, Vector2(1, 0))
	surfaces.append(proj_surf)
	var path := _trace(Vector2(50, 300), Vector2(750, 300), surfaces)
	var found_post_projective := false
	var before := {}
	for i in range(path.steps.size()):
		var step: TraceStep = path.steps[i]
		before[step.frame_id] = true
		if step.hit != null and step.surface_id == proj_surf.id and step.hit_on_segment:
			if i + 1 < path.steps.size():
				var post_step: TraceStep = path.steps[i + 1]
				# beam: a projective starts a new epoch, a frame never seen before
				assert_false(before.has(post_step.frame_id),
					"Frame after the projective hit is fresh (new epoch), got %d" % post_step.frame_id)
				assert_ne(post_step.frame_id, TraceStep.IDENTITY_FRAME,
					"No transform carried: a new epoch, not the identity of epoch 0")
				found_post_projective = true
			break
	assert_true(found_post_projective, "Should have a step after the projective surface")

# --- Test 7: trace through semicircle ---

func test_stage75_trace_through_semicircle() -> void:
	var surfaces := _room_walls()
	var proj_seg := _standard_arc()
	var proj_surf := _projective_arc_surface(proj_seg, Vector2(1, 0))
	surfaces.append(proj_surf)
	var path := _trace(Vector2(200, 300), Vector2(500, 350), surfaces)
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
		assert_almost_eq(step_dir.x, 1.0, 0.05,
			"Post-projection should go in +normal direction (rightward)")
		assert_almost_eq(absf(step_dir.y), 0.0, 0.05,
			"Post-projection should have no y component")
		# beam: a projective starts a new epoch (fresh frame), not epoch-0 identity
		assert_ne(post_step.frame_id, path.steps[post_proj_idx - 1].frame_id,
			"Frame changes at the projective hit")

# --- Test 8: S16 - no NaN/Inf ---

func test_stage75_S16_no_nan_inf() -> void:
	var surfaces := _room_walls()
	var proj_surf := _projective_arc_surface(_standard_arc(), Vector2(1, 0))
	surfaces.append(proj_surf)
	var path := _trace(Vector2(330, 300), Vector2(450, 300), surfaces)
	assert_gt(path.steps.size(), 1, "Should have steps")
	for s: TraceStep in path.steps:
		for v: Vector2 in [s.start, s.end]:
			assert_false(is_nan(v.x) or is_nan(v.y), "no NaN in step coords")
			assert_false(is_inf(v.x) or is_inf(v.y), "no Inf in a closed room")
	var dir := _exit_dir(surfaces, proj_surf, Vector2(330, 300), Vector2(450, 300))
	assert_false(is_nan(dir.x), "direction.x not NaN")
	assert_false(is_nan(dir.y), "direction.y not NaN")

# --- Test 9: kind is projective ---

func test_stage75_kind_is_projective() -> void:
	var effect := CircleDirectionalProjection.new(Vector2(1, 0))
	assert_eq(effect.kind(), Effect.Kind.PROJECTIVE, "Kind should be PROJECTIVE")

# --- Test 10: project_back exits same side ---

func test_stage75_project_back_exits_same_side() -> void:
	var seg := _standard_arc()
	var through_dir := _exit_dir_off(seg, CircleDirectionalProjection.new(Vector2(1, 0), false),
		Vector2(330, 300), Vector2(450, 300))
	var back_dir := _exit_dir_off(seg, CircleDirectionalProjection.new(Vector2(1, 0), true),
		Vector2(330, 300), Vector2(450, 300))
	assert_almost_eq(through_dir.x + back_dir.x, 0.0, 0.01,
		"Through and back should have opposite directions")

# --- Test 11: project_back color ---

func test_stage75_project_back_color() -> void:
	var back := CircleDirectionalProjection.new(Vector2(1, 0), true)
	var through := CircleDirectionalProjection.new(Vector2(1, 0), false)
	assert_eq(back.get_display_color(), Color(1.0, 0.4, 0.1),
		"Back should be red-orange")
	assert_eq(through.get_display_color(), Color(1.0, 0.8, 0.2),
		"Through should be yellow-orange")

# --- Test 12: project_back display name ---

func test_stage75_project_back_display_name() -> void:
	var back := CircleDirectionalProjection.new(Vector2(1, 0), true)
	assert_eq(back.get_display_name(), "circle_directional_back",
		"Back display name")

# --- Test 13: default is through ---

func test_stage75_default_is_through() -> void:
	var normal := Vector2(1, 0)
	var effect := CircleDirectionalProjection.new(normal)
	assert_eq(effect.get_display_color(), Color(1.0, 0.8, 0.2),
		"Default should be through (yellow-orange)")
	assert_eq(effect.get_display_name(), "circle_directional",
		"Default display name")
	var seg := _standard_arc()
	var explicit := CircleDirectionalProjection.new(normal, false)
	var dir_default := _exit_dir_off(seg, effect, Vector2(330, 300), Vector2(450, 300))
	var dir_explicit := _exit_dir_off(seg, explicit, Vector2(330, 300), Vector2(450, 300))
	assert_almost_eq(dir_default, dir_explicit, Vector2(0.01, 0.01),
		"Default and explicit false should produce same direction")
