extends Node

# The beam solver's tracer (owns the solve caches: one per owner).
var _tracer := BeamTracer.new()

const _DebugDumper := preload("res://scripts/game/debug_dumper.gd")
const MOVEMENT_ACTIONS := ["move_up", "move_down", "move_left", "move_right"]

var _player: CharacterBody2D
var _cursor: Node2D
var _arrow_animator: Node2D
var _path_renderer: Node2D
var _level_settings: Node2D
var _plan_hud: Control
var _camera: Camera2D
var _edge_overlay: Node2D = null   # the level bounds' edge (EdgeOverlayNode, Stage 88)

var plan := PlanManager.new()
var click_detector := ClickDetector.new()
var game_state := GameState.new()
var targets_hit: Dictionary = {}
var _checkpoints := CheckpointStack.new()
var _pending_flags = null   # the fired shot's final flags, promoted when it lands
var _hovered_node: Node2D = null
var _hover_partner_node: Node2D = null
# Stage 100: the per-frame work runs only when its inputs changed
var _defs_cache := SceneDefsCache.new()
var _hover_inputs = null     # [cursor, plan.version, flags] of the last hover update
var _overlay_inputs = null   # [trace_version, plan.version, flags] of the last overlay update

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
	_tracer.set_emit_infinity(FeatureFlags.infinity_event())
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
	_update_surface_overlays()   # runs only when its inputs changed (a cleared plan clears the marks)

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
		if _edge():
			_edge().set_hovered(false)
		_hover_inputs = null
		return

	# the hover depends on the cursor, the plan (sides it refuses) and the state
	var inputs := [_cursor.global_position, plan.version, game_state.flags.duplicate(true)]
	if _hover_inputs != null and inputs == _hover_inputs:
		return
	_hover_inputs = inputs

	var result := _detect_click()

	_clear_all_hover()
	if _edge():
		_edge().set_hovered(result.has("infinity"))

	if result.has("surface"):
		var surf: Surface = result.surface
		var side: Side.Value = result.side
		var overlays := ChevronOverlayBuilder.build_hover_overlays(surf, side, game_state)
		for child in _level_settings.get_children():
			if child.has_method("set_hover_overlays") and "surface" in child:
				var child_overlays: Array = overlays.get(child.surface.id, [])
				if not child_overlays.is_empty():
					child.set_hover_overlays(child_overlays)
					if child.surface == surf:
						_hovered_node = child
					else:
						_hover_partner_node = child

# The level bounds' edge overlay (added by the parent's _ready, after this
# node's: looked up on first use).
func _edge() -> Node2D:
	if _edge_overlay == null and _level_settings:
		_edge_overlay = _level_settings.get_node_or_null("EdgeOverlay")
	return _edge_overlay

# What the cursor is over: a surface side, the level bounds' edge, or nothing.
func _detect_click() -> Dictionary:
	var surfaces := _get_surfaces()
	return click_detector.detect_click(_cursor.global_position, surfaces, game_state, _plan_accepts.bind(surfaces),
		_level_bounds(), FeatureFlags.infinity_event())

func _level_bounds() -> Rect2:
	if _level_settings and "level_bounds" in _level_settings:
		return _level_settings.level_bounds
	return VisualConverter.DEFAULT_BOUNDS()

# Inside a ghost stretch of the plan only ghost togglers can be planned.
func _plan_accepts(surf: Surface, side: Side.Value, surfaces: Array) -> bool:
	return not _tracer.plan_refuses(_defs_cache.defs(surfaces), plan.entries, surf.id, side == Side.Value.LEFT, game_state.flags)

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
	var result := _detect_click()

	if result.is_empty():
		if not is_right_click:
			return
		plan.clear()
	else:
		# a surface side, or the level bounds' edge (the infinity entry, Stage 88)
		var sid: int = BeamSolver.INFINITY_ID if result.has("infinity") else result.surface.id
		if is_right_click:
			if plan.has_surface(sid):
				plan.remove_last_of(sid)
			else:
				plan.clear()
		else:
			plan.add_entry(sid, result.get("side", Side.Value.LEFT), _cursor.global_position)

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
	var path: TracePath
	var pair: BeamTracerPair = _tracer.trace_pair(_defs_cache.defs(surfaces), player_pos, cursor_pos, entries, game_state.flags, _defs_cache.segments(surfaces))
	if pair == null:
		_checkpoints.pop()
		return
	path = pair.physical
	_pending_flags = pair.final_flags

	if _path_renderer:
		_path_renderer.modulate.a = 0.25

	get_tree().paused = true

	if _arrow_animator:
		var bounds: Rect2 = VisualConverter.DEFAULT_BOUNDS()
		if _level_settings and "level_bounds" in _level_settings:
			bounds = _level_settings.level_bounds
		path = VisualConverter.prepare_for_display(path, bounds, surfaces)
		_arrow_animator.start_flight(path, bounds)
	else:
		# no arrow to animate (test levels): the shot lands at once instead of
		# leaving the game paused
		_on_flight_completed()

func _on_flight_completed() -> void:
	get_tree().paused = false
	# the shot's state changes take effect (GAME_SPEC 10.6): the physical
	# trace's final flags become the game state (the checkpoint pushed at
	# fire time keeps the previous one for undo)
	if _pending_flags != null:
		var kept: Dictionary = _pending_flags.duplicate(true)
		kept.erase(BeamScene.GHOST())   # ghost is per shot
		game_state = GameState.new(kept)
		_pending_flags = null
		_on_state_changed()
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
	_on_state_changed()
	plan.restore_from(data.plan_entries)
	targets_hit = data.targets_hit.duplicate()
	_refresh_ui()

func _on_state_changed() -> void:
	if is_inside_tree():
		get_tree().call_group("surface_nodes", "queue_redraw")

func _get_surfaces() -> Array:
	if _level_settings and "surfaces" in _level_settings:
		return _level_settings.surfaces
	return []

func _refresh_ui() -> void:
	_update_hud()
	_update_surface_overlays(true)

func _update_hud() -> void:
	if not _plan_hud:
		return
	_plan_hud.update_plan(plan, _get_surfaces())

# The plan overlays depend on the physical hits (the renderer's trace
# version), the plan and the state; unchanged inputs leave the nodes alone
# unless forced (the UI refresh after a click, undo or reset).
func _update_surface_overlays(force: bool = false) -> void:
	if not _level_settings:
		return
	var tv: int = _path_renderer.trace_version if _path_renderer and "trace_version" in _path_renderer else -1
	var inputs := [tv, plan.version, game_state.flags.duplicate(true)]
	if not force and _overlay_inputs != null and inputs == _overlay_inputs:
		return
	_overlay_inputs = inputs
	var physical_hits := {}
	if _path_renderer and _path_renderer.has_method("get_physical_hits"):
		physical_hits = _path_renderer.get_physical_hits()
	var surfaces := _get_surfaces()
	var lookup := func(id: int) -> Surface:
		for s in surfaces:
			if s.id == id:
				return s
		return null
	var overlays := ChevronOverlayBuilder.build_plan_overlays(plan, physical_hits, lookup, game_state)
	for child in _level_settings.get_children():
		if child.has_method("set_plan_overlays") and "surface" in child:
			var plan_indices: Array[int] = []
			for i in plan.entries.size():
				if plan.entries[i].surface_id == child.surface.id:
					plan_indices.append(i + 1)
			child.set_plan_overlays(overlays.get(child.surface.id, []), plan_indices)
	if _edge():
		# the infinity entries: the k-th one is met when the physical trace wraps k
		# times (its solver events: the display path clips the wrap to the edge)
		var wraps := 0
		var result = _path_renderer.last_beam_result if _path_renderer and "last_beam_result" in _path_renderer else null
		if result != null:
			for e in result.physical:
				if e.kind == "infinity":
					wraps += 1
		var marks: Array = []
		var k := 0
		for i in plan.entries.size():
			var entry: PlanManager.PlanEntry = plan.entries[i]
			if entry.surface_id == BeamSolver.INFINITY_ID:
				k += 1
				marks.append({"index": i + 1, "point": entry.click, "valid": k <= wraps})
		_edge().set_marks(marks)

func _dump_debug_state() -> void:
	_DebugDumper.dump(self)
