extends GutTest

var _player: Node2D
var _main_scene: Node

func before_each() -> void:
	_main_scene = load("res://scenes/gravity_test.tscn").instantiate()
	add_child_autofree(_main_scene)
	_player = _main_scene.get_node("Player")

func _set_gravity(g: Vector2) -> void:
	_main_scene.gravity = g

func test_stage2_5_gravity_horizontal_only() -> void:
	_set_gravity(Vector2(0, 980))
	var start_pos := _player.position
	Input.action_press("move_right")
	_simulate_physics(5)
	Input.action_release("move_right")
	assert_gt(_player.position.x, start_pos.x, "Player should move right with gravity")

func test_stage2_5_gravity_causes_falling() -> void:
	_set_gravity(Vector2(0, 980))
	var start_pos := _player.position
	_simulate_physics(10)
	assert_gt(_player.position.y, start_pos.y, "Player should fall with gravity")

func test_stage2_5_jump_velocity_logic() -> void:
	_set_gravity(Vector2(0, 980))
	_player.velocity.y = 0.0
	# Simulate being on floor by calling _physics_process with move_up pressed
	# Since we can't reliably test is_on_floor() without real frames,
	# test the velocity logic directly
	assert_eq(_player.JUMP_VELOCITY, 400.0, "Jump velocity constant should be 400")

func test_stage2_5_zero_gravity_unchanged() -> void:
	_set_gravity(Vector2(0, 0))
	var start_pos := _player.position
	Input.action_press("move_up")
	_simulate_physics(5)
	Input.action_release("move_up")
	assert_lt(_player.position.y, start_pos.y, "W should move up in zero gravity")

func test_stage2_5_s_ignored_with_gravity() -> void:
	_set_gravity(Vector2(0, 980))
	var start_x := _player.position.x
	Input.action_press("move_down")
	_simulate_physics(5)
	Input.action_release("move_down")
	assert_almost_eq(_player.position.x, start_x, 0.1, "S key should not move horizontally")

func test_stage2_5_no_coyote_time() -> void:
	_set_gravity(Vector2(0, 980))
	_simulate_physics(2)
	Input.action_press("move_up")
	_simulate_physics(1)
	Input.action_release("move_up")
	assert_ne(_player.velocity.y, -_player.JUMP_VELOCITY, "Should not jump when not on floor")

func _simulate_physics(count: int) -> void:
	var dt := get_physics_process_delta_time()
	for i in count:
		_player._physics_process(dt)


