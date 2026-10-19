extends Node2D

const VIOLATIONS_PATH := "res://violations.json"

var _violations: Array = []
var _current_index: int = 0
var _current_scene: Node = null
var _current_scene_path: String = ""
var _current_combo: Dictionary = {}
var _player: Node2D
var _cursor: Node2D
var _path_renderer: Node2D
var _game_mgr: Node
var _label: Label

func _ready() -> void:
	_label = Label.new()
	_label.position = Vector2(20, 20)
	_label.add_theme_font_size_override("font_size", 14)
	_label.add_theme_color_override("font_color", Color.WHITE)
	_label.add_theme_color_override("font_shadow_color", Color.BLACK)
	_label.add_theme_constant_override("shadow_offset_x", 1)
	_label.add_theme_constant_override("shadow_offset_y", 1)
	_label.z_index = 100
	add_child(_label)

	if not _load_violations():
		_label.text = "No violations file found at %s\nRun ./run_tests.sh first." % VIOLATIONS_PATH
		return

	_show_violation(0)

func _load_violations() -> bool:
	if not FileAccess.file_exists(VIOLATIONS_PATH):
		return false
	var file := FileAccess.open(VIOLATIONS_PATH, FileAccess.READ)
	if not file:
		return false
	var json := JSON.new()
	var err := json.parse(file.get_as_text())
	file.close()
	if err != OK:
		_label.text = "Failed to parse violations JSON: %s" % json.get_error_message()
		return false
	_violations = json.data
	return _violations.size() > 0

func _show_violation(index: int) -> void:
	_current_index = clampi(index, 0, _violations.size() - 1)
	var v: Dictionary = _violations[_current_index]
	var scene_path: String = v.scene

	var combo: Dictionary = v.get("combo", {})
	if scene_path != _current_scene_path or combo != _current_combo:
		if _current_scene:
			_current_scene.queue_free()
			_current_scene = null
			_player = null
			_cursor = null
			_path_renderer = null
			_game_mgr = null
		await get_tree().process_frame

		Surface.reset_id_counter()
		_current_scene = load(scene_path).instantiate()
		_current_scene.gravity = Vector2.ZERO
		if combo.size() > 0:
			SweepPlans.apply_line_pair(_current_scene, int(combo.lines))
			SweepPlans.apply_circle_pair(_current_scene, int(combo.circles))
		add_child(_current_scene)
		_current_scene_path = scene_path
		_current_combo = combo

		await get_tree().process_frame

		_player = _current_scene.get_node_or_null("Player")
		_cursor = _current_scene.get_node_or_null("Cursor")
		_path_renderer = _current_scene.get_node_or_null("PathRenderer")
		_game_mgr = _current_scene.get_node_or_null("GameManager")

		if _cursor:
			_cursor.set_process(false)
		if _player:
			_player.set_process(false)
			_player.set_physics_process(false)
		if _game_mgr:
			_game_mgr.set_process_input(false)

	var player_pos := Vector2(v.player_pos[0], v.player_pos[1])
	var cursor_pos := Vector2(v.cursor_pos[0], v.cursor_pos[1])

	if _player:
		_player.global_position = player_pos
	if _cursor:
		_cursor.global_position = cursor_pos

	if _game_mgr and "plan" in _game_mgr:
		_game_mgr.plan.clear()
		var plan_data: Array = v.plan
		for entry in plan_data:
			var sid: int = int(entry.surface_id)
			var side: int = int(entry.side)
			_game_mgr.plan.add_entry(sid, side)
		if _game_mgr.has_method("_update_surface_overlays"):
			_game_mgr._update_surface_overlays()

	if _path_renderer:
		_path_renderer._compute_trace()
		_path_renderer.queue_redraw()

	_update_label(v)

func _update_label(v: Dictionary) -> void:
	var scene_name: String
	var combo: Dictionary = v.get("combo", {})
	if combo.size() > 0:
		scene_name = "combo: " + str(combo.get("label", ""))
	else:
		scene_name = v.scene.get_file()
	var plan_str := "(none)"
	var plan_data: Array = v.plan
	if plan_data.size() > 0:
		var parts: Array[String] = []
		for entry in plan_data:
			var side_name := "L" if int(entry.side) == Side.Value.LEFT else "R"
			parts.append("%d/%s" % [int(entry.surface_id), side_name])
		plan_str = "[%s]" % ", ".join(parts)

	_label.text = "Violation %d/%d — %s\nPlayer: (%s, %s)  Cursor: (%s, %s)\nPlan: %s\n%s\n\n[←/→ navigate] [Tab: display mode] [F12: dump]" % [
		_current_index + 1, _violations.size(), scene_name,
		v.player_pos[0], v.player_pos[1],
		v.cursor_pos[0], v.cursor_pos[1],
		plan_str, v.violation]

func _input(event: InputEvent) -> void:
	if not event is InputEventKey or not event.pressed or event.echo:
		get_viewport().set_input_as_handled()
		return

	if event.physical_keycode == KEY_RIGHT or event.physical_keycode == KEY_DOWN:
		if _current_index < _violations.size() - 1:
			_show_violation(_current_index + 1)
		get_viewport().set_input_as_handled()
	elif event.physical_keycode == KEY_LEFT or event.physical_keycode == KEY_UP:
		if _current_index > 0:
			_show_violation(_current_index - 1)
		get_viewport().set_input_as_handled()
	elif event.physical_keycode == KEY_TAB:
		if _path_renderer:
			_path_renderer.cycle_display_mode()
		get_viewport().set_input_as_handled()
	elif event.physical_keycode == KEY_F12:
		if _game_mgr and _game_mgr.has_method("_dump_debug_state"):
			_game_mgr._dump_debug_state()
		get_viewport().set_input_as_handled()
	else:
		get_viewport().set_input_as_handled()
