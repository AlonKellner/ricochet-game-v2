extends GutTest

# The Godot host of the level editor (native/hosts/godot: RicochetEditor, the
# editor on the C API alone; docs/EDITOR.md): it paints the editor's draw list
# as layers, forwards Godot's keys, saves the document where asked, and plays
# it with a RicochetGame child (F5), back on Esc.

const LEVEL := "res://data/levels/test_levels/flag_switches.json"
const SAVE := "user://editor_host_test.json"

var _editor: RicochetEditor

func before_each() -> void:
	_editor = RicochetEditor.new()
	_editor.level_path = LEVEL
	_editor.save_path = SAVE
	add_child_autofree(_editor)
	await wait_process_frames(2)

func _key(code: Key, ctrl := false) -> void:
	var ev := InputEventKey.new()
	ev.physical_keycode = code
	ev.keycode = code
	ev.ctrl_pressed = ctrl
	ev.pressed = true
	Input.parse_input_event(ev)
	Input.flush_buffered_events()
	var up := ev.duplicate()
	up.pressed = false
	Input.parse_input_event(up)
	Input.flush_buffered_events()

func _command(c: Dictionary) -> Dictionary:
	return JSON.parse_string(_editor.command(JSON.stringify(c)))

func test_the_editor_is_painted_as_layers() -> void:
	assert_gt(_editor.layer_count(), 5)
	var names: Array = _editor.get_node("Layers").get_children().map(func(n): return String(n.name))
	assert_true(names.all(func(n: String): return n.begins_with("editor")), "every layer is the editor's")
	assert_eq(_editor.document(), FileAccess.get_file_as_string(LEVEL), "the level as it is on disk")

func test_commands_edit_the_document() -> void:
	var r := _command({"op": "add", "element": {"kind": "mirror", "start": [700, 300], "end": [700, 600]}})
	assert_true(r["ok"])
	assert_eq(r["name"], "mirror_1")
	assert_true(_editor.document().contains("\"mirror_1\""))
	var before := _editor.layer_count()
	_command({"op": "add", "element": {"kind": "wall", "start": [600, 300], "end": [600, 400]}})
	await wait_process_frames(1)
	assert_eq(_editor.layer_count(), before + 1, "one layer more: the new element's")

func test_keys_reach_the_editor() -> void:
	_key(KEY_L)
	assert_eq(_command({"op": "state"})["tool"], "line")
	_key(KEY_TAB)
	assert_ne(_command({"op": "state"})["kind"], "wall")

func test_a_save_writes_the_document() -> void:
	_command({"op": "add", "element": {"kind": "wall", "start": [600, 300], "end": [600, 400]}})
	_key(KEY_S, true)
	assert_eq(FileAccess.get_file_as_string(SAVE), _editor.document())
	assert_false(_command({"op": "state"})["dirty"])
	DirAccess.remove_absolute(ProjectSettings.globalize_path(SAVE))

func test_the_default_save_target_is_the_repository_file() -> void:
	var e := RicochetEditor.new()
	e.level_path = LEVEL
	var t: String = e.save_target()
	assert_true(t.ends_with("/data/levels/test_levels/flag_switches.json"), t)
	assert_false(t.contains("engines/godot"), "not the res://data copy")
	e.free()

func test_f5_plays_the_level_and_esc_returns() -> void:
	_key(KEY_F5)
	await wait_process_frames(2)
	assert_true(_editor.is_playing())
	var game: RicochetGame = _editor.get_node("PlayTest")
	assert_gt(game.layer_count(), 3, "the game draws")
	assert_false(_editor.get_node("Layers").visible, "the editor hidden")
	_key(KEY_ESCAPE)
	await wait_process_frames(2)
	assert_false(_editor.is_playing())
	assert_true(_editor.get_node("Layers").visible)
