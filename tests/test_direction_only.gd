extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

# --- Geometry extraction tests ---

func test_extract_geometry_empty_path() -> void:
	var path := TracePath.new()
	var geo := InvariantChecker._extract_trace_geometry(path)
	assert_eq(geo.size(), 0, "Empty path should produce empty geometry")

func test_extract_geometry_null_path() -> void:
	var geo := InvariantChecker._extract_trace_geometry(null)
	assert_eq(geo.size(), 0, "Null path should produce empty geometry")

func test_extract_geometry_single_step() -> void:
	var path := TracePath.new()
	path.steps.append(TraceStep.make(Vector2(0, 0), Vector2(100, 0), 0))
	var geo := InvariantChecker._extract_trace_geometry(path)
	assert_eq(geo.size(), 1, "Single step should produce one segment")
	assert_eq(geo[0].start, Vector2(0, 0))
	assert_eq(geo[0].end, Vector2(100, 0))

func test_extract_geometry_merges_same_frame_id() -> void:
	var path := TracePath.new()
	path.steps.append(TraceStep.make(Vector2(0, 0), Vector2(50, 0), 0))
	path.steps.append(TraceStep.make(Vector2(50, 0), Vector2(100, 0), 0))
	path.steps.append(TraceStep.make(Vector2(100, 0), Vector2(200, 100), 1))
	var geo := InvariantChecker._extract_trace_geometry(path)
	assert_eq(geo.size(), 2, "Two frame_ids should produce two segments")
	assert_eq(geo[0].start, Vector2(0, 0))
	assert_eq(geo[0].end, Vector2(100, 0))
	assert_eq(geo[1].start, Vector2(100, 0))
	assert_eq(geo[1].end, Vector2(200, 100))

func test_extract_geometry_no_merge_different_frame_ids() -> void:
	var path := TracePath.new()
	path.steps.append(TraceStep.make(Vector2(0, 0), Vector2(100, 0), 0))
	path.steps.append(TraceStep.make(Vector2(100, 0), Vector2(200, 100), 1))
	path.steps.append(TraceStep.make(Vector2(200, 100), Vector2(300, 0), 2))
	var geo := InvariantChecker._extract_trace_geometry(path)
	assert_eq(geo.size(), 3, "All different frame_ids should produce three segments")

# --- Direct tracer DIRECTION-ONLY tests ---

func _make_mirror_left(x: float) -> Surface:
	var seg := Segment.from_coords(Vector2(x, 0), Vector2(x, 600), Vector2(x, 300))
	var refl := ReflectionEffect.new(seg.get_carrier())
	var left := SideConfig.new(refl, true)
	var right := SideConfig.new(refl, true)
	return Surface.new(seg, left, right, false, false)

func test_direction_only_single_mirror_direct() -> void:
	var mirror := _make_mirror_left(400)
	var w_left := H.wall(0)
	var w_right := H.wall(800)
	var w_top := RoomBuilder.create_block_surface(Vector2(0, 0), Vector2(800, 0), Vector2(400, 0))
	var w_bot := RoomBuilder.create_block_surface(Vector2(0, 600), Vector2(800, 600), Vector2(400, 600))
	var surfaces := [mirror, w_left, w_right, w_top, w_bot]
	var player := Vector2(200, 300)
	var cursor := Vector2(600, 200)
	var plan := [PlanManager.PlanEntry.new(mirror.id, Side.Value.LEFT)]

	# The planned aim point is the cursor's image in the mirror x=400
	# (the legacy planner image helper is gone; the beam computes its own aim).
	var aim_point := Vector2(2.0 * 400.0 - cursor.x, cursor.y)

	var trace_with_plan := TestTrace.trace(player, Direction.from_coords(player, cursor), surfaces,
		GameState.new(), null, -1.0, TraceMode.PHYSICAL, TraceMode.PHYSICAL, plan, null, cursor)

	var ref_dir := Direction.from_coords(player, aim_point)
	var trace_no_plan := TestTrace.trace(player, ref_dir, surfaces, GameState.new(),
		null, -1.0, TraceMode.PHYSICAL, TraceMode.PHYSICAL, [], null, aim_point)

	var geo_a := InvariantChecker._extract_trace_geometry(trace_with_plan)
	var geo_b := InvariantChecker._extract_trace_geometry(trace_no_plan)

	assert_eq(geo_a.size(), geo_b.size(),
		"Segment count should match: with_plan=%d no_plan=%d" % [geo_a.size(), geo_b.size()])
	for i in geo_a.size():
		assert_almost_eq(geo_a[i].start, geo_b[i].start, Vector2(1, 1),
			"Segment %d start should match" % i)
		assert_almost_eq(geo_a[i].end, geo_b[i].end, Vector2(1, 1),
			"Segment %d end should match" % i)

# --- Scene-based DIRECTION-ONLY tests ---

func test_direction_only_scene_mirror() -> void:
	Surface.reset_id_counter()
	var scene_path := "res://scenes/test_levels/room_with_mirror.tscn"
	if not FileAccess.file_exists(scene_path):
		pass_test("Scene not found, skipping")
		return
	var scene: Node = load(scene_path).instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var checker := InvariantChecker.new()
	checker.setup(scene)

	var mirrors: Array = []
	for surf in scene.surfaces:
		var s: Surface = surf
		var left: SideConfig = s.active_side_config(Side.Value.LEFT, GameState.new())
		var right: SideConfig = s.active_side_config(Side.Value.RIGHT, GameState.new())
		if (left != null and left.effect is ReflectionEffect) or (right != null and right.effect is ReflectionEffect):
			mirrors.append(s)
	if mirrors.size() == 0:
		pass_test("No mirrors in scene, skipping")
		return

	var side: Side.Value = Side.Value.LEFT
	var config: SideConfig = mirrors[0].active_side_config(Side.Value.LEFT, GameState.new())
	if config == null or not config.effect is ReflectionEffect:
		side = Side.Value.RIGHT
	var plan: Array = [PlanManager.PlanEntry.new(mirrors[0].id, side)]

	var positions := [Vector2(700, 400), Vector2(800, 300), Vector2(650, 500)]
	var cursors := [Vector2(900, 500), Vector2(1000, 400), Vector2(850, 350)]

	for pi in positions.size():
		var violations := checker.check_DIRECTION_ONLY(positions[pi], cursors[pi], plan)
		assert_eq(violations.size(), 0,
			"DIRECTION-ONLY at player=%s cursor=%s: %s" % [positions[pi], cursors[pi], str(violations)])

func test_direction_only_scene_inversion() -> void:
	Surface.reset_id_counter()
	var scene_path := "res://scenes/test_levels/room_with_inversion.tscn"
	if not FileAccess.file_exists(scene_path):
		pass_test("Scene not found, skipping")
		return
	var scene: Node = load(scene_path).instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var checker := InvariantChecker.new()
	checker.setup(scene)

	var inversions: Array = []
	for surf in scene.surfaces:
		var s: Surface = surf
		var left: SideConfig = s.active_side_config(Side.Value.LEFT, GameState.new())
		var right: SideConfig = s.active_side_config(Side.Value.RIGHT, GameState.new())
		if (left != null and left.effect is CircleInversionEffect) or (right != null and right.effect is CircleInversionEffect):
			inversions.append(s)
	if inversions.size() == 0:
		pass_test("No inversions in scene, skipping")
		return

	var side: Side.Value = Side.Value.LEFT
	var config: SideConfig = inversions[0].active_side_config(Side.Value.LEFT, GameState.new())
	if config == null or not config.effect is CircleInversionEffect:
		side = Side.Value.RIGHT
	var plan: Array = [PlanManager.PlanEntry.new(inversions[0].id, side)]

	var player := Vector2(700, 400)
	var cursor := Vector2(900, 500)
	var violations := checker.check_DIRECTION_ONLY(player, cursor, plan)
	assert_eq(violations.size(), 0,
		"DIRECTION-ONLY with inversion: %s" % str(violations))
