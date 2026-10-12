extends Node

const _DebugDumper := preload("res://scripts/game/debug_dumper.gd")
const MOVEMENT_ACTIONS := ["move_up", "move_down", "move_left", "move_right"]

var _player: CharacterBody2D
var _cursor: Node2D
var _arrow_animator: Node2D
var _path_renderer: Node2D
var _level_settings: Node2D
var _plan_hud: Control
var _camera: Camera2D

var plan := PlanManager.new()
var click_detector := ClickDetector.new()
var game_state := GameState.new()
var targets_hit: Dictionary = {}
var _checkpoints := CheckpointStack.new()
var _hovered_node: Node2D = null
var _hover_partner_node: Node2D = null

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	var parent := get_parent()
	_player = parent.get_node_or_null("Player")
	_cursor = parent.get_node_or_null("Cursor")
	_arrow_animator = parent.get_node_or_null("ArrowAnimator")
	_path_renderer = parent.get_node_or_null("PathRenderer")
	_level_settings = parent
	_plan_hud = parent.get_node_or_null("PlanHUD")
	_camera = parent.get_node_or_null("Camera")
	if _arrow_animator:
		_arrow_animator.flight_completed.connect(_on_flight_completed)
	if _camera and _level_settings and "level_bounds" in _level_settings:
		var bounds: Rect2 = _level_settings.level_bounds
		_camera.limit_left = int(bounds.position.x)
		_camera.limit_top = int(bounds.position.y)
		_camera.limit_right = int(bounds.end.x)
		_camera.limit_bottom = int(bounds.end.y)
	_checkpoints.set_initial(_capture_checkpoint())

func _process(_delta: float) -> void:
	_update_camera()
	_update_hover()
	if not plan.is_empty():
		_update_surface_overlays()

func _update_camera() -> void:
	if not _camera:
		return
	if _arrow_animator and _arrow_animator.is_flying():
		_camera.global_position = _arrow_animator.get_arrow_position()
	elif _player:
		_camera.global_position = _player.global_position

func _update_hover() -> void:
	if not _cursor or not _level_settings:
		return
	if _arrow_animator and _arrow_animator.is_flying():
		_clear_all_hover()
		return

	var surfaces := _get_surfaces()
	var result := click_detector.detect_click(_cursor.global_position, surfaces)

	_clear_all_hover()

	if not result.is_empty():
		var surf: Surface = result.surface
		var side: Side.Value = result.side
		var overlays := ChevronOverlayBuilder.build_hover_overlays(surf, side)
		for child in _level_settings.get_children():
			if child.has_method("set_hover_overlays") and "surface" in child:
				var child_overlays: Array = overlays.get(child.surface.id, [])
				if not child_overlays.is_empty():
					child.set_hover_overlays(child_overlays)
					if child.surface == surf:
						_hovered_node = child
					else:
						_hover_partner_node = child

func _clear_all_hover() -> void:
	if _hovered_node and is_instance_valid(_hovered_node):
		_hovered_node.clear_hover()
	if _hover_partner_node and is_instance_valid(_hover_partner_node):
		_hover_partner_node.clear_hover()
	_hovered_node = null
	_hover_partner_node = null

func _input(event: InputEvent) -> void:
	if _arrow_animator and _arrow_animator.is_flying():
		if event is InputEventKey and event.pressed and not event.echo:
			var is_movement := false
			for action in MOVEMENT_ACTIONS:
				if event.is_action(action):
					is_movement = true
					break
			if not is_movement:
				_arrow_animator.speed_up()
		return

	if event is InputEventKey and event.pressed and event.physical_keycode == KEY_TAB:
		if _path_renderer:
			_path_renderer.cycle_display_mode()
		get_viewport().set_input_as_handled()
		return

	if event is InputEventKey and event.pressed and event.physical_keycode == KEY_F12:
		_dump_debug_state()
		return

	if event is InputEventKey and event.pressed and event.physical_keycode == KEY_G:
		if _level_settings:
			if _level_settings.gravity.length_squared() > 0.0:
				_level_settings.gravity = Vector2.ZERO
			else:
				_level_settings.gravity = Vector2(0, 980)
		return

	if event is InputEventKey and event.pressed and event.physical_keycode == KEY_Z:
		_try_undo()
		return

	if event is InputEventKey and event.pressed and event.physical_keycode == KEY_R:
		_try_reset()
		return

	if event is InputEventKey and event.pressed and event.physical_keycode == KEY_C:
		plan.clear()
		_refresh_ui()
		return

	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_LEFT:
			_handle_plan_click(false)
			return
		if event.button_index == MOUSE_BUTTON_RIGHT:
			_handle_plan_click(true)
			return

	if event.is_action_pressed("fire"):
		_try_fire()

func _handle_plan_click(is_right_click: bool) -> void:
	if not _cursor:
		return
	var surfaces := _get_surfaces()
	var result := click_detector.detect_click(_cursor.global_position, surfaces)

	if result.is_empty():
		if not is_right_click:
			return
		plan.clear()
	else:
		var surf: Surface = result.surface
		if is_right_click:
			if plan.has_surface(surf.id):
				plan.remove_last_of(surf.id)
			else:
				plan.clear()
		else:
			plan.add_entry(surf.id, result.side)

	_refresh_ui()

func _try_fire() -> void:
	if not _player or not _cursor:
		return

	var player_pos := _player.global_position
	var cursor_pos := _cursor.global_position
	if player_pos == cursor_pos:
		return

	_checkpoints.push(_capture_checkpoint())

	var surfaces := _get_surfaces()
	var entries: Array = plan.entries if not plan.is_empty() else []
	var path: Tracer.TracedPath
	var pair: BeamTracer.Pair = BeamTracer.trace_pair(surfaces, player_pos, cursor_pos, entries)
	if pair != null:
		path = pair.physical
	else:
		var dir := _compute_aim_direction(player_pos, cursor_pos, surfaces)
		path = Tracer.trace(player_pos, dir, surfaces, game_state)

	if _path_renderer:
		_path_renderer.modulate.a = 0.25

	get_tree().paused = true

	if _arrow_animator:
		var bounds: Rect2 = VisualConverter.DEFAULT_BOUNDS
		if _level_settings and "level_bounds" in _level_settings:
			bounds = _level_settings.level_bounds
		path = VisualConverter.prepare_for_display(path, bounds, surfaces)
		_arrow_animator.start_flight(path, bounds)

func _on_flight_completed() -> void:
	get_tree().paused = false
	if _path_renderer:
		_path_renderer.modulate.a = 1.0

func _try_undo() -> void:
	var data := _checkpoints.pop()
	if data == null:
		return
	_restore_checkpoint(data)

func _try_reset() -> void:
	var data := _checkpoints.reset()
	if data == null:
		return
	_restore_checkpoint(data)

func _capture_checkpoint() -> CheckpointData:
	var pos := _player.global_position if _player else Vector2.ZERO
	var vel := _player.velocity if _player else Vector2.ZERO
	return CheckpointData.new(pos, vel, game_state, plan.entries, targets_hit)

func _restore_checkpoint(data: CheckpointData) -> void:
	if _player:
		_player.global_position = data.player_position
		_player.velocity = data.player_velocity
	game_state = data.game_state.copy()
	plan.restore_from(data.plan_entries)
	targets_hit = data.targets_hit.duplicate()
	_refresh_ui()

func _compute_aim_direction(player_pos: Vector2, cursor_pos: Vector2, surfaces: Array) -> Direction:
	var entries: Array = plan.entries if not plan.is_empty() else []
	return Planner.compute_aim_direction(player_pos, cursor_pos, entries, surfaces, game_state)

func _get_surfaces() -> Array:
	if _level_settings and "surfaces" in _level_settings:
		return _level_settings.surfaces
	return []

func _refresh_ui() -> void:
	_update_hud()
	_update_surface_overlays()

func _update_hud() -> void:
	if not _plan_hud:
		return
	_plan_hud.update_plan(plan, _get_surfaces())

func _update_surface_overlays() -> void:
	if not _level_settings:
		return
	var physical_hits := {}
	if _path_renderer and _path_renderer.has_method("get_physical_hits"):
		physical_hits = _path_renderer.get_physical_hits()
	var surfaces := _get_surfaces()
	var lookup := func(id: int) -> Surface:
		for s in surfaces:
			if s.id == id:
				return s
		return null
	var overlays := ChevronOverlayBuilder.build_plan_overlays(plan, physical_hits, lookup)
	for child in _level_settings.get_children():
		if child.has_method("set_plan_overlays") and "surface" in child:
			var plan_indices: Array[int] = []
			for i in plan.entries.size():
				if plan.entries[i].surface_id == child.surface.id:
					plan_indices.append(i + 1)
			child.set_plan_overlays(overlays.get(child.surface.id, []), plan_indices)

func _dump_debug_state() -> void:
	_DebugDumper.dump(self)
