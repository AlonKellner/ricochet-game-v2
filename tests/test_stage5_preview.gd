extends GutTest

var _player: Node2D
var _cursor: Node2D
var _renderer: Node2D
var _main_scene: Node

func before_each() -> void:
	_main_scene = load("res://scenes/main.tscn").instantiate()
	_main_scene.gravity = Vector2.ZERO
	add_child_autofree(_main_scene)
	_player = _main_scene.get_node("Player")
	_cursor = _main_scene.get_node("Cursor")
	_renderer = _main_scene.get_node("PathRenderer")

func test_stage5_preview_exists_when_cursor_differs() -> void:
	_cursor.global_position = Vector2(800, 400)
	_renderer._compute_trace()
	assert_true(_renderer.has_line(), "Renderer should have a line when cursor differs from player")

func test_stage5_preview_absent_when_cursor_equals_player() -> void:
	_cursor.global_position = _player.global_position
	_renderer._compute_trace()
	assert_false(_renderer.has_line(), "Renderer should have no line when cursor equals player")

func test_stage5_preview_color_green() -> void:
	assert_eq(StepTypes.color(StepTypes.Type.ALIGNED), Color.GREEN, "ALIGNED color should be green")

func test_stage5_preview_starts_at_player() -> void:
	_cursor.global_position = Vector2(800, 400)
	_renderer._compute_trace()
	assert_eq(_renderer.get_line_from(), _player.global_position, "Line should start at player position")

func test_stage5_preview_direction_toward_cursor() -> void:
	_cursor.global_position = Vector2(800, 400)
	_renderer._compute_trace()
	var expected_dir := (_cursor.global_position - _player.global_position).normalized()
	var actual_dir: Vector2 = _renderer.get_line_direction()
	assert_almost_eq(expected_dir.dot(actual_dir), 1.0, 0.01, "Line should point toward cursor")
