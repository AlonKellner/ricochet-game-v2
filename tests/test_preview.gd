extends GutTest

func test_line_drawn_when_cursor_differs() -> void:
	var scene: Node = load("res://scenes/test_levels/room_boundaries.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var player: Node2D = scene.get_node("Player")
	var cursor: Node2D = scene.get_node("Cursor")
	var renderer: Node2D = scene.get_node("PathRenderer")

	player.global_position = Vector2(960, 540)
	cursor.global_position = Vector2(1200, 540)
	renderer._compute_trace()

	assert_true(renderer.has_line(), "Should have a line when cursor differs from player")

func test_line_starts_at_player() -> void:
	var scene: Node = load("res://scenes/test_levels/room_boundaries.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var player: Node2D = scene.get_node("Player")
	var cursor: Node2D = scene.get_node("Cursor")
	var renderer: Node2D = scene.get_node("PathRenderer")

	player.global_position = Vector2(960, 540)
	cursor.global_position = Vector2(1200, 540)
	renderer._compute_trace()

	assert_eq(renderer.get_line_from(), Vector2(960, 540), "Line should start at player")

func test_line_direction_toward_cursor() -> void:
	var scene: Node = load("res://scenes/test_levels/room_boundaries.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var player: Node2D = scene.get_node("Player")
	var cursor: Node2D = scene.get_node("Cursor")
	var renderer: Node2D = scene.get_node("PathRenderer")

	player.global_position = Vector2(960, 540)
	cursor.global_position = Vector2(1200, 540)
	renderer._compute_trace()

	var dir: Vector2 = renderer.get_line_direction()
	assert_almost_eq(dir.x, 1.0, 0.01, "Should point right toward cursor")

func test_no_line_when_cursor_equals_player() -> void:
	var scene: Node = load("res://scenes/test_levels/room_boundaries.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var player: Node2D = scene.get_node("Player")
	var cursor: Node2D = scene.get_node("Cursor")
	var renderer: Node2D = scene.get_node("PathRenderer")

	player.global_position = Vector2(960, 540)
	cursor.global_position = Vector2(960, 540)
	renderer._compute_trace()

	assert_false(renderer.has_line(), "No line when cursor == player")

func test_solid_green_to_cursor_dashed_after() -> void:
	var scene: Node = load("res://scenes/test_levels/room_boundaries.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var player: Node2D = scene.get_node("Player")
	var cursor: Node2D = scene.get_node("Cursor")
	var renderer: Node2D = scene.get_node("PathRenderer")

	player.global_position = Vector2(960, 540)
	cursor.global_position = Vector2(1200, 540)
	renderer._compute_trace()

	var typed: Array = renderer.get_typed_steps()
	assert_gt(typed.size(), 0, "Should have typed steps")

	var first: TraceStep = typed[0]
	assert_eq(first.type, StepTypes.Type.ALIGNED, "First step solid green (ALIGNED)")

	var has_post := false
	for i in typed.size():
		var ms: TraceStep = typed[i]
		if ms.type == StepTypes.Type.ALIGNED_POST_PLANNED:
			has_post = true
	assert_true(has_post, "Should have dashed green (ALIGNED_POST_PLANNED) after cursor")
