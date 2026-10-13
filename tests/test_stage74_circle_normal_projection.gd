extends GutTest

const H := preload("res://tests/test_helpers.gd")
const TOL := Vector2(1.0, 1.0)

func before_each() -> void:
	H.reset_counters()

# --- Helpers ---

func _standard_arc() -> Segment:
	return Segment.from_coords(Vector2(200, 100), Vector2(200, 300), Vector2(300, 200))

func _small_arc() -> Segment:
	return Segment.from_coords(Vector2(270, 130), Vector2(270, 270), Vector2(300, 200))

func _projective_arc_surface(seg: Segment, project_back: bool = false) -> Surface:
	var effect := CircleNormalProjection.new(project_back)
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

# --- Test 1: radial outward from interior ---

func test_stage74_apply_forward_radial_outward() -> void:
	# from inside, hit at (300, 200)
	var dir := _exit_dir_off(_standard_arc(), CircleNormalProjection.new(), Vector2(250, 200), Vector2(350, 200))
	assert_almost_eq(dir.x, 1.0, 0.01, "Radial outward x should be 1")
	assert_almost_eq(dir.y, 0.0, 0.01, "Radial outward y should be 0")

# --- Test 2: angled position ---

func test_stage74_apply_forward_angled() -> void:
	# from inside (250, 200) toward (260, 280), which is on the circle
	# (center (200, 200), r 100): radial direction there is (0.6, 0.8).
	# (Legacy hit the arc endpoint (200, 300); a trace needs an interior hit.)
	var dir := _exit_dir_off(_standard_arc(), CircleNormalProjection.new(), Vector2(250, 200), Vector2(270, 360))
	assert_almost_eq(dir.x, 0.6, 0.01, "Angled hit: radially outward x")
	assert_almost_eq(dir.y, 0.8, 0.01, "Angled hit: radially outward y")

# --- Test 5: S10 - frame resets to identity ---

func test_stage74_S10_frame_resets() -> void:
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
	var proj_seg := Segment.from_coords(Vector2(400, 100), Vector2(400, 500), Vector2(500, 300))
	var proj_surf := _projective_arc_surface(proj_seg)
	surfaces.append(proj_surf)
	var path := _trace(Vector2(50, 300), Vector2(750, 300), surfaces)
	var found_post_projective := false
	var before := {}
	for i in range(path.steps.size()):
		var step: TraceStep = path.steps[i]
		before[step.frame_id] = true
		if step.hit != null and step.surface_id == proj_surf.id:
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

# --- Test 6: trace through circle projection ---

func test_stage74_trace_through_circle_projection() -> void:
	var surfaces := _room_walls()
	var proj_seg := Segment.from_coords(Vector2(400, 100), Vector2(400, 500), Vector2(500, 300))
	var proj_surf := _projective_arc_surface(proj_seg)
	surfaces.append(proj_surf)
	var path := _trace(Vector2(200, 300), Vector2(600, 300), surfaces)
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
		# beam: a projective starts a new epoch (fresh frame), not epoch-0 identity
		assert_ne(post_step.frame_id, path.steps[post_proj_idx - 1].frame_id,
			"Frame changes at the projective hit")

# --- Test 7: S16 - no NaN/Inf ---

func test_stage74_S16_no_nan_inf() -> void:
	var surfaces := _room_walls()
	var cfg := SideConfig.new(CircleNormalProjection.new(), true)
	var proj_surf := Surface.new(_standard_arc(), cfg, cfg, false, false)
	surfaces.append(proj_surf)
	var path := _trace(Vector2(250, 200), Vector2(350, 200), surfaces)
	assert_gt(path.steps.size(), 1, "Should have steps")
	for s: TraceStep in path.steps:
		for v: Vector2 in [s.start, s.end]:
			assert_false(is_nan(v.x) or is_nan(v.y), "no NaN in step coords")
			assert_false(is_inf(v.x) or is_inf(v.y), "no Inf in a closed room")
	var dir := _exit_dir(surfaces, proj_surf, Vector2(250, 200), Vector2(350, 200))
	assert_false(is_nan(dir.x), "direction.x not NaN")
	assert_false(is_nan(dir.y), "direction.y not NaN")

# --- Test 9: project_back exits same side ---

func test_stage74_project_back_exits_same_side() -> void:
	var seg := _standard_arc()
	var through_dir := _exit_dir_off(seg, CircleNormalProjection.new(false), Vector2(250, 200), Vector2(350, 200))
	var back_dir := _exit_dir_off(seg, CircleNormalProjection.new(true), Vector2(250, 200), Vector2(350, 200))
	assert_almost_eq(through_dir.x + back_dir.x, 0.0, 0.01,
		"Through and back should have opposite x directions")

# --- Test 10: project_back color ---

func test_stage74_project_back_color() -> void:
	var back := CircleNormalProjection.new(true)
	var through := CircleNormalProjection.new(false)
	assert_eq(back.get_display_color(), Color(1.0, 0.4, 0.1),
		"Back should be red-orange")
	assert_eq(through.get_display_color(), Color(1.0, 0.8, 0.2),
		"Through should be yellow-orange")

# --- Test 11: project_back display name ---

func test_stage74_project_back_display_name() -> void:
	var back := CircleNormalProjection.new(true)
	assert_eq(back.get_display_name(), "circle_normal_back",
		"Back display name should be 'circle_normal_back'")

# --- Test 12: default is through ---

func test_stage74_default_is_through() -> void:
	var effect := CircleNormalProjection.new()
	assert_eq(effect.get_display_color(), Color(1.0, 0.8, 0.2),
		"Default should be through (yellow-orange)")
	assert_eq(effect.get_display_name(), "circle_normal",
		"Default display name should be 'circle_normal'")
	var seg := _standard_arc()
	var explicit := CircleNormalProjection.new(false)
	var dir_default := _exit_dir_off(seg, effect, Vector2(250, 200), Vector2(350, 200))
	var dir_explicit := _exit_dir_off(seg, explicit, Vector2(250, 200), Vector2(350, 200))
	assert_almost_eq(dir_default, dir_explicit, Vector2(0.01, 0.01),
		"Default and explicit false should produce same direction")

# --- Test 13: kind is projective ---

func test_stage74_kind_is_projective() -> void:
	var effect := CircleNormalProjection.new()
	assert_eq(effect.kind(), Effect.Kind.PROJECTIVE, "Kind should be PROJECTIVE")
