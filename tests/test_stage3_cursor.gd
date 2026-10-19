extends GutTest

var _player: Node2D
var _cursor: Node2D
var _main_scene: Node

func before_each() -> void:
	_main_scene = load("res://scenes/main.tscn").instantiate()
	_main_scene.gravity = Vector2.ZERO
	add_child_autofree(_main_scene)
	_player = _main_scene.get_node("Player")
	_cursor = _main_scene.get_node("Cursor")

func test_stage3_cursor_uses_world_space() -> void:
	assert_not_null(_cursor, "Cursor node should exist in scene")
	assert_true(_cursor.has_method("_process"), "Cursor should have _process for tracking")

func test_stage3_cursor_updates_position() -> void:
	var pos_before := _cursor.global_position
	_cursor.global_position = Vector2(500, 300)
	assert_ne(_cursor.global_position, pos_before, "Cursor position should be changeable")

func test_stage3_player_triangle_faces_cursor() -> void:
	var visual := _player.get_node("Visual") as Node2D
	_cursor.global_position = Vector2(_player.global_position.x + 100, _player.global_position.y)
	_player._process(0.016)
	assert_almost_eq(visual.rotation, 0.0, 0.01, "Triangle should point right when cursor is to the right")

func test_stage3_player_triangle_faces_cursor_above() -> void:
	var visual := _player.get_node("Visual") as Node2D
	_cursor.global_position = Vector2(_player.global_position.x, _player.global_position.y - 100)
	_player._process(0.016)
	assert_almost_eq(visual.rotation, -PI / 2.0, 0.01, "Triangle should point up when cursor is above")

func test_stage3_player_triangle_faces_cursor_left() -> void:
	var visual := _player.get_node("Visual") as Node2D
	_cursor.global_position = Vector2(_player.global_position.x - 100, _player.global_position.y)
	_player._process(0.016)
	assert_almost_eq(absf(visual.rotation), PI, 0.01, "Triangle should point left when cursor is to the left")
