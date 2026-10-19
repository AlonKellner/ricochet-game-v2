extends GutTest

# --- Pure data tests (no scene) ---

func test_stage32_checkpoint_saves_player_position() -> void:
	var pos := Vector2(200, 300)
	var data := CheckpointData.new(pos, Vector2.ZERO, GameState.new(), [], {})
	assert_eq(data.player_position, pos, "Position saved")

func test_stage32_checkpoint_saves_player_velocity() -> void:
	var vel := Vector2(100, -50)
	var data := CheckpointData.new(Vector2.ZERO, vel, GameState.new(), [], {})
	assert_eq(data.player_velocity, vel, "Velocity saved")

func test_stage32_checkpoint_saves_game_state_deep_copy() -> void:
	var gs := GameState.new({"key": "original"})
	var data := CheckpointData.new(Vector2.ZERO, Vector2.ZERO, gs, [], {})
	gs.flags["key"] = "modified"
	assert_eq(data.game_state.flags["key"], "original",
		"Checkpoint should hold a deep copy, not affected by later mutation")

func test_stage32_checkpoint_saves_plan() -> void:
	var entries: Array = [PlanManager.PlanEntry.new(5, Side.Value.LEFT)]
	var data := CheckpointData.new(Vector2.ZERO, Vector2.ZERO, GameState.new(), entries, {})
	entries.clear()
	assert_eq(data.plan_entries.size(), 1, "Plan entries deep-copied")
	assert_eq(data.plan_entries[0].surface_id, 5, "Surface ID preserved")
	assert_eq(data.plan_entries[0].side, Side.Value.LEFT, "Side preserved")

func test_stage32_checkpoint_saves_targets_hit() -> void:
	var targets: Dictionary = {1: true, 3: true}
	var data := CheckpointData.new(Vector2.ZERO, Vector2.ZERO, GameState.new(), [], targets)
	targets[5] = true
	assert_eq(data.targets_hit.size(), 2, "Targets deep-copied")
	assert_true(data.targets_hit.has(1), "Target 1 present")
	assert_true(data.targets_hit.has(3), "Target 3 present")
	assert_false(data.targets_hit.has(5), "Target 5 not present (added after checkpoint)")

func test_stage32_checkpoint_deep_copy_nested() -> void:
	var gs := GameState.new({"config": {"sub_key": [1, 2, 3]}})
	var data := CheckpointData.new(Vector2.ZERO, Vector2.ZERO, gs, [], {})
	gs.flags["config"]["sub_key"].append(4)
	var restored: Array = data.game_state.flags["config"]["sub_key"]
	assert_eq(restored, [1, 2, 3], "Nested array should be deep-copied")

func test_stage32_checkpoint_stack_depth_50() -> void:
	var stack := CheckpointStack.new()
	var initial := CheckpointData.new(Vector2(0, 0), Vector2.ZERO, GameState.new(), [], {})
	stack.set_initial(initial)
	for i in 50:
		var data := CheckpointData.new(
			Vector2(float(i), float(i)), Vector2.ZERO, GameState.new(), [], {})
		stack.push(data)
	assert_eq(stack.depth(), 50, "Stack holds 50 checkpoints")
	for i in range(49, -1, -1):
		var data := stack.pop()
		assert_eq(data.player_position, Vector2(float(i), float(i)),
			"Pop %d returns correct position" % i)
	assert_true(stack.is_empty(), "Stack empty after 50 pops")

# --- Scene tests ---

func _create_scene() -> Node:
	var scene: Node = load("res://scenes/main.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	return scene

func test_stage32_undo_pops_from_stack() -> void:
	var scene := _create_scene()
	await get_tree().process_frame
	var game_mgr: Node = scene.get_node("GameManager")
	var player: Node2D = scene.get_node("Player")
	var cursor: Node2D = scene.get_node("Cursor")
	var arrow: Node2D = scene.get_node("ArrowAnimator")

	var positions: Array[Vector2] = []
	for i in 3:
		positions.append(player.global_position)
		cursor.global_position = player.global_position + Vector2(100, 0)
		game_mgr._try_fire()
		arrow._finish_flight()
		await get_tree().process_frame
		player.global_position = Vector2(200 + i * 100, 400)
		await get_tree().process_frame

	for i in range(2, -1, -1):
		game_mgr._try_undo()
		await get_tree().process_frame
		assert_almost_eq(player.global_position, positions[i], Vector2(1, 1),
			"Undo %d restores position" % i)

	game_mgr._try_undo()
	await get_tree().process_frame
	assert_almost_eq(player.global_position, positions[0], Vector2(1, 1),
		"Extra undo is no-op (stays at last restored position)")

func test_stage32_full_reset_clears_stack() -> void:
	var scene := _create_scene()
	await get_tree().process_frame
	var game_mgr: Node = scene.get_node("GameManager")
	var player: Node2D = scene.get_node("Player")
	var cursor: Node2D = scene.get_node("Cursor")
	var arrow: Node2D = scene.get_node("ArrowAnimator")
	var initial_pos := player.global_position

	cursor.global_position = player.global_position + Vector2(100, 0)
	game_mgr._try_fire()
	arrow._finish_flight()
	await get_tree().process_frame
	player.global_position = Vector2(300, 400)

	game_mgr._try_reset()
	await get_tree().process_frame
	assert_almost_eq(player.global_position, initial_pos, Vector2(1, 1),
		"Reset restores initial position")
	assert_true(game_mgr._checkpoints.is_empty(), "Stack cleared after reset")

	game_mgr._try_undo()
	await get_tree().process_frame
	assert_almost_eq(player.global_position, initial_pos, Vector2(1, 1),
		"Undo after reset is no-op")

func test_stage32_full_reset_restores_initial_state() -> void:
	var scene := _create_scene()
	await get_tree().process_frame
	var game_mgr: Node = scene.get_node("GameManager")
	var player: Node2D = scene.get_node("Player")
	var cursor: Node2D = scene.get_node("Cursor")
	var arrow: Node2D = scene.get_node("ArrowAnimator")
	var initial_pos := player.global_position

	game_mgr.game_state.flags["wall_intact"] = false
	cursor.global_position = player.global_position + Vector2(100, 0)
	game_mgr._try_fire()
	arrow._finish_flight()
	await get_tree().process_frame

	game_mgr._try_reset()
	await get_tree().process_frame
	assert_false(game_mgr.game_state.flags.has("wall_intact"),
		"Reset restores initial game state (no wall_intact key)")
	assert_eq(game_mgr.plan.entries.size(), 0, "Plan cleared on reset")
	assert_eq(game_mgr.targets_hit.size(), 0, "Targets cleared on reset")

func test_stage32_checkpoint_saved_before_shot() -> void:
	var scene := _create_scene()
	await get_tree().process_frame
	var game_mgr: Node = scene.get_node("GameManager")
	var player: Node2D = scene.get_node("Player")
	var cursor: Node2D = scene.get_node("Cursor")
	var arrow: Node2D = scene.get_node("ArrowAnimator")
	var pre_fire_pos := player.global_position

	cursor.global_position = player.global_position + Vector2(100, 0)
	game_mgr._try_fire()
	arrow._finish_flight()
	await get_tree().process_frame

	game_mgr._try_undo()
	await get_tree().process_frame
	assert_almost_eq(player.global_position, pre_fire_pos, Vector2(1, 1),
		"Checkpoint saved BEFORE the shot (position matches pre-fire)")

func test_stage32_undo_indistinguishable_from_pre_shot() -> void:
	var scene := _create_scene()
	await get_tree().process_frame
	var game_mgr: Node = scene.get_node("GameManager")
	var player: Node2D = scene.get_node("Player")
	var cursor: Node2D = scene.get_node("Cursor")
	var arrow: Node2D = scene.get_node("ArrowAnimator")

	var pre_pos := player.global_position
	var pre_vel: Vector2 = player.velocity
	var pre_plan_size: int = game_mgr.plan.entries.size()
	var pre_targets_size: int = game_mgr.targets_hit.size()

	cursor.global_position = player.global_position + Vector2(100, 0)
	game_mgr._try_fire()
	arrow._finish_flight()
	await get_tree().process_frame
	player.global_position = Vector2(999, 999)

	game_mgr._try_undo()
	await get_tree().process_frame
	assert_almost_eq(player.global_position, pre_pos, Vector2(1, 1), "Position restored")
	assert_eq(player.velocity, pre_vel, "Velocity restored")
	assert_eq(game_mgr.plan.entries.size(), pre_plan_size, "Plan size restored")
	assert_eq(game_mgr.targets_hit.size(), pre_targets_size, "Targets restored")
