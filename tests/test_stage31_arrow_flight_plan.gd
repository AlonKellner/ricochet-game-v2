extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

# --- Math tests (no scene needed) ---

func test_stage31_arrow_follows_physical_trace_with_plan() -> void:
	var m := H.mirror(400)
	var w := H.wall(700)
	var player := Vector2(200, 300)
	var cursor := Vector2(300, 300)
	var aim := Direction.from_coords(player, cursor)
	var plan: Array = [PlanManager.PlanEntry.new(m.id, Side.Value.LEFT)]
	var path := TestTrace.trace(player, aim, [m, w], GameState.new(),
		null, -1.0,
		TraceMode.PHYSICAL, TraceMode.PHYSICAL, plan, null, cursor)
	assert_gt(path.steps.size(), 0, "Path should have steps")
	var first_step: TraceStep = path.steps[0]
	assert_true(first_step.start.distance_to(player) < 1.0, "First step starts at player")

func test_stage31_aligned_plan_matches_physical() -> void:
	var room := RoomBuilder.create_room_surfaces(Rect2(0, 0, 800, 600))
	var m := H.mirror(400)
	var surfaces: Array = room + [m]
	var player := Vector2(200, 300)
	var cursor := Vector2(300, 300)
	var aim := Direction.from_coords(player, cursor)
	var plan: Array = [PlanManager.PlanEntry.new(m.id, Side.Value.RIGHT)]
	var physical := TestTrace.trace(player, aim, surfaces, GameState.new(),
		null, -1.0,
		TraceMode.PHYSICAL, TraceMode.PHYSICAL, plan, null, cursor)
	var planned := TestTrace.trace(player, aim, surfaces, GameState.new(),
		null, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, plan, null, cursor)
	var overlap := mini(physical.steps.size(), planned.steps.size())
	assert_gt(overlap, 0, "Both traces should have steps")
	for i in overlap:
		var p: TraceStep = physical.steps[i]
		var r: TraceStep = planned.steps[i]
		assert_eq(p.frame_id, r.frame_id,
			"Step %d frame_id should match (aligned)" % i)
		assert_almost_eq(p.start, r.start, Vector2(0.01, 0.01),
			"Step %d start matches" % i)
		assert_almost_eq(p.end, r.end, Vector2(0.01, 0.01),
			"Step %d end matches" % i)

func test_stage31_determinism_with_plan() -> void:
	var m := H.mirror(400)
	var w := H.wall(700)
	var player := Vector2(200, 300)
	var cursor := Vector2(300, 300)
	var aim := Direction.from_coords(player, cursor)
	var plan: Array = [PlanManager.PlanEntry.new(m.id, Side.Value.LEFT)]
	var path1 := TestTrace.trace(player, aim, [m, w], GameState.new(),
		null, -1.0,
		TraceMode.PHYSICAL, TraceMode.PHYSICAL, plan, null, cursor)
	var path2 := TestTrace.trace(player, aim, [m, w], GameState.new(),
		null, -1.0,
		TraceMode.PHYSICAL, TraceMode.PHYSICAL, plan, null, cursor)
	assert_eq(path1.steps.size(), path2.steps.size(), "Same step count")
	for i in path1.steps.size():
		var s1: TraceStep = path1.steps[i]
		var s2: TraceStep = path2.steps[i]
		assert_eq(s1.start, s2.start, "Step %d start identical" % i)
		assert_eq(s1.end, s2.end, "Step %d end identical" % i)

# --- Scene tests (need main.tscn) ---

func _create_scene() -> Node:
	var scene: Node = load("res://scenes/main.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	return scene

func test_stage31_camera_smoothing_enabled() -> void:
	var scene := _create_scene()
	await get_tree().process_frame
	var camera: Camera2D = scene.get_node("Camera")
	assert_not_null(camera, "Camera2D should exist")
	assert_true(camera.position_smoothing_enabled, "Smoothing should be enabled")
	assert_almost_eq(camera.position_smoothing_speed, 5.0, 0.01,
		"Smoothing speed should be 5.0")

func test_stage31_camera_clamped_to_bounds() -> void:
	var scene := _create_scene()
	await get_tree().process_frame
	var camera: Camera2D = scene.get_node("Camera")
	assert_not_null(camera, "Camera2D should exist")
	assert_eq(camera.limit_left, 0, "limit_left from level_bounds")
	assert_eq(camera.limit_top, 0, "limit_top from level_bounds")
	assert_eq(camera.limit_right, 1920, "limit_right from level_bounds")
	assert_eq(camera.limit_bottom, 1080, "limit_bottom from level_bounds")

func test_stage31_camera_returns_to_player() -> void:
	var scene := _create_scene()
	await get_tree().process_frame
	var game_mgr: Node = scene.get_node("GameManager")
	var player: Node2D = scene.get_node("Player")
	var camera: Camera2D = scene.get_node("Camera")
	var cursor: Node2D = scene.get_node("Cursor")
	cursor.global_position = Vector2(1200, 540)
	game_mgr._try_fire()
	var arrow: Node2D = scene.get_node("ArrowAnimator")
	arrow._finish_flight()
	await get_tree().process_frame
	await get_tree().process_frame
	assert_almost_eq(camera.global_position, player.global_position,
		Vector2(1, 1), "Camera should target player after flight")

func test_stage31_preview_hidden_during_flight() -> void:
	var scene := _create_scene()
	await get_tree().process_frame
	var game_mgr: Node = scene.get_node("GameManager")
	var path_renderer: Node2D = scene.get_node("PathRenderer")
	var cursor: Node2D = scene.get_node("Cursor")
	var arrow: Node2D = scene.get_node("ArrowAnimator")
	cursor.global_position = Vector2(1200, 540)
	assert_almost_eq(path_renderer.modulate.a, 1.0, 0.01, "Full opacity before fire")
	game_mgr._try_fire()
	assert_true(path_renderer.modulate.a < 0.5, "Dimmed during flight")
	arrow._finish_flight()
	await get_tree().process_frame
	assert_almost_eq(path_renderer.modulate.a, 1.0, 0.01, "Full opacity after flight")

func test_stage31_camera_bounds_arc_flight() -> void:
	var step := TraceStep.make(
		Vector2(2, 0), Vector2(-2, 0), 0, null, null, Vector2(0, 2), true)
	var arc_len := 2.0 * PI
	var r := ArrowAdvance.advance([step], 0, 0.0, step.start, arc_len * 0.5,
		VisualConverter.DEFAULT_BOUNDS())
	assert_almost_eq(r.position, Vector2(0, 2), Vector2(0.01, 0.01),
		"Arc flight should follow curve, not chord")

func test_stage31_camera_returns_after_escape_flight() -> void:
	var scene := _create_scene()
	await get_tree().process_frame
	var game_mgr: Node = scene.get_node("GameManager")
	var player: Node2D = scene.get_node("Player")
	var camera: Camera2D = scene.get_node("Camera")
	var cursor: Node2D = scene.get_node("Cursor")
	var arrow: Node2D = scene.get_node("ArrowAnimator")
	cursor.global_position = Vector2(1200, 540)
	game_mgr._try_fire()
	arrow._finish_flight()
	await get_tree().process_frame
	await get_tree().process_frame
	assert_almost_eq(camera.global_position, player.global_position,
		Vector2(1, 1), "Camera returns to player after escape")
