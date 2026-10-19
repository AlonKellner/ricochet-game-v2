extends GutTest

var _player: Node2D
var _main_scene: Node

func before_each() -> void:
	_main_scene = load("res://scenes/main.tscn").instantiate()
	add_child_autofree(_main_scene)
	_main_scene.gravity = Vector2.ZERO
	_player = _main_scene.get_node("Player")

func test_stage2_player_speed_constant() -> void:
	assert_eq(_player.SPEED, 200.0, "Player speed should be 200")

func test_stage2_player_collision_radius() -> void:
	# Stage 117: the native controller's circle (rcg/kinematics.hpp)
	assert_eq(_player.COLLISION_RADIUS, 12.0, "Player collision radius should be 12")
	assert_eq(RcgKinematics.radius(), 12.0)
	assert_eq(RcgKinematics.speed(), _player.SPEED)
	assert_eq(RcgKinematics.jump_velocity(), _player.JUMP_VELOCITY)

func test_stage2_player_initial_position() -> void:
	assert_eq(_player.position, Vector2(960, 540), "Player should spawn at configured position")

func test_stage2_player_moves_on_input() -> void:
	var start_pos := _player.position
	Input.action_press("move_right")
	_simulate_frames(5)
	Input.action_release("move_right")
	assert_gt(_player.position.x, start_pos.x, "Player should move right")
	assert_almost_eq(_player.position.y, start_pos.y, 0.1, "Player should not move vertically")

func test_stage2_player_stops_on_release() -> void:
	Input.action_press("move_right")
	_simulate_frames(5)
	Input.action_release("move_right")
	_simulate_frames(1)
	var pos_after_release := _player.position
	_simulate_frames(5)
	assert_eq(_player.position, pos_after_release, "Player should stop immediately on key release")

func _simulate_frames(count: int) -> void:
	for i in count:
		get_tree().physics_frame.emit()
		_player._physics_process(get_physics_process_delta_time())
