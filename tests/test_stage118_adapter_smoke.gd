extends GutTest

# Stage 118.4: the Godot adapter (native/hosts/godot: RicochetGame, the game on
# the C API alone) plays a level: it paints the draw list as layers in draw
# order, maps Godot's input onto the game's events, flies a shot.

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
	assert_eq(names.slice(0, 3), ["background", "player", "cursor"], "z 0 in the scene's tree order")
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
