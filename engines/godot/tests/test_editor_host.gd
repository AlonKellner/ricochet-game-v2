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

func test_the_level_menu_lists_and_opens_levels() -> void:
	var st := _command({"op": "state"})
	assert_gt(int(st["levels"]), 40, "the host listed the levels")
	assert_eq(st["level"], "test_levels/flag_switches", "the open level by name")
	assert_true(_command({"op": "open", "name": "objectives/lantern"})["ok"])
	assert_eq(_command({"op": "state"})["level"], "objectives/lantern", "the host loaded it")
	assert_true(_editor.document().contains("objectives/lantern"))

func test_a_new_level_and_a_copy_save_under_their_names() -> void:
	assert_true(_command({"op": "new_level", "name": "scratch/gut_new"})["ok"])
	_editor.save_path = ""
	assert_true(_editor.save_target().ends_with("data/levels/scratch/gut_new.json"), _editor.save_target())
	_command({"op": "saved"})
	assert_true(_command({"op": "new_level", "name": "scratch/gut_copy", "copy_of": "objectives/lantern"})["ok"])
	assert_eq(_command({"op": "state"})["level"], "scratch/gut_copy", "the host loaded the copy under its name")
	assert_true(_editor.document().contains("scratch/gut_copy"))
	assert_true(_editor.save_target().ends_with("data/levels/scratch/gut_copy.json"))
	assert_false(FileAccess.file_exists(_editor.save_target()), "nothing written until a save")

func test_a_rename_saves_at_once_and_removes_only_the_levels_own_file() -> void:
	var repo := ProjectSettings.globalize_path("res://").path_join("../../data/levels/test_levels/flag_switches.json").simplify_path()
	assert_true(FileAccess.file_exists(repo))
	_key(KEY_F2)
	assert_true(_command({"op": "state"})["rename_prompt"], "F2: the rename's prompt")
	_key(KEY_ESCAPE)
	assert_true(_command({"op": "rename_level", "to": "test_levels/gut_renamed"})["ok"])
	var st := _command({"op": "state"})
	assert_eq(st["level"], "test_levels/gut_renamed")
	assert_false(st["dirty"], "saved at once")
	assert_eq(FileAccess.get_file_as_string(SAVE), _editor.document(), "written where the host saves (save_path)")
	assert_true(FileAccess.file_exists(repo), "a save elsewhere removes no level")
	assert_true(_editor.document().contains("test_levels/gut_renamed"))
	DirAccess.remove_absolute(ProjectSettings.globalize_path(SAVE))
