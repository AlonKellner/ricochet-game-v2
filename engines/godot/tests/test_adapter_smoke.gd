extends GutTest

# The Godot host (native/hosts/godot: RicochetGame, the game on the C API
# alone; Stage 118.4, the final Godot suite since Stage 119): it plays a level --
# it paints the draw list as layers in draw order, maps Godot's input onto the
# game's events, flies a shot, follows the camera, reports errors to Godot,
# and gives back every object it made.

var _game: RicochetGame

func before_each() -> void:
	_game = RicochetGame.new()
	_game.level_path = "res://data/levels/main.json"
	add_child_autofree(_game)
	await wait_physics_frames(2)

func _record() -> Dictionary:
	return JSON.parse_string(_game.record())

func _key(code: Key) -> void:
	var ev := InputEventKey.new()
	ev.physical_keycode = code
	ev.keycode = code
	ev.pressed = true
	Input.parse_input_event(ev)
	Input.flush_buffered_events()
	var up := ev.duplicate()
	up.pressed = false
	Input.parse_input_event(up)
	Input.flush_buffered_events()

func test_the_draw_list_becomes_layers_in_draw_order() -> void:
	assert_gt(_game.layer_count(), 3)
	var layers: Node = _game.get_node("Layers")
	var names: Array = layers.get_children().map(func(n): return String(n.name))
	# (Stage 164: a level's colliders come right after the background, under the player)
	var lead: Array = names.slice(0, 4).filter(func(n): return n != "colliders")
	assert_eq(lead.slice(0, 3), ["background", "player", "cursor"], "z 0 in the scene's tree order")
	var z := -1000
	for n: Node2D in layers.get_children():
		assert_true(n.z_index >= z, "layers by z")
		z = n.z_index
	assert_true(names.any(func(n: String): return n.begins_with("surface")))

func test_the_record_is_the_sessions() -> void:
	var r := _record()
	assert_true(r.has("draw") and r.has("plan") and r.has("world"))
	assert_eq(int(r["checkpoints"]), 0)
	assert_eq(_game.hud(), "")

func test_tab_cycles_the_display() -> void:
	assert_eq(int(_record()["display_mode"]), 0)
	_key(KEY_TAB)
	await wait_process_frames(1)
	assert_eq(int(_record()["display_mode"]), 1)

func test_a_shot_flies_and_lands() -> void:
	# the cursor is the mouse (headless: the origin), the player away from it
	_key(KEY_SPACE)
	assert_true(_game.is_flying(), "the fire action launches the arrow")
	assert_not_null(_game.get_node_or_null("Layers/arrow"), "the arrow drawn in flight")
	var frames := 0
	while _game.is_flying() and frames < 600:
		await wait_process_frames(1)
		frames += 1
	assert_false(_game.is_flying(), "landed")
	assert_eq(int(_record()["checkpoints"]), 1)
	await wait_process_frames(1)
	assert_null(_game.get_node_or_null("Layers/arrow"))

func test_engine_is_at_least_4_7() -> void:
	var v := Engine.get_version_info()
	assert_true(v["major"] > 4 or (v["major"] == 4 and v["minor"] >= 7), "Godot %s" % v["string"])

func test_errors_reach_godot() -> void:
	var bad := RicochetGame.new()
	bad.level_path = "res://data/levels/no_such_level.json"
	add_child_autofree(bad)
	await wait_process_frames(1)
	assert_push_error("RicochetGame", "a level that does not load is a Godot error")
	assert_eq(bad.layer_count(), 0, "and nothing is drawn")

func test_the_camera_follows_the_level() -> void:
	var camera: Camera2D = _game.get_node("Camera")
	assert_not_null(camera, "main has a camera")
	assert_true(camera.position_smoothing_enabled)
	assert_almost_eq(camera.position_smoothing_speed, 5.0, 0.01)
	assert_eq([camera.limit_left, camera.limit_top, camera.limit_right, camera.limit_bottom], [0, 0, 1920, 1080], "limited to the bounds")

func _levels() -> Array[String]:
	var out: Array[String] = []
	for dir in ["res://data/levels", "res://data/levels/test_levels", "res://data/levels/playground"]:
		for fn in DirAccess.get_files_at(dir):
			if fn.ends_with(".json"):
				out.append(dir + "/" + fn)
	return out

func _play(path: String) -> void:
	var game := RicochetGame.new()
	game.level_path = path
	game.aim(Vector2(1200, 400))
	add_child(game)
	await wait_process_frames(2)
	remove_child(game)
	game.free()

func test_levels_give_back_their_objects() -> void:
	var levels := _levels()
	assert_true(levels.size() >= 30, "every level: %d" % levels.size())
	for path in levels:   # warm-up: the classes and the font load once
		await _play(path)
	var before := int(Performance.get_monitor(Performance.OBJECT_COUNT))
	for path in levels:
		await _play(path)
	var after := int(Performance.get_monitor(Performance.OBJECT_COUNT))
	assert_lt(after - before, 8, "objects before %d, after %d: a level leaked" % [before, after])
	assert_eq(int(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT)), 0, "no orphan nodes")
