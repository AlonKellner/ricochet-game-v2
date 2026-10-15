class_name InvariantChecker
extends RefCounted

# The invariant checker: a thin driver over the native InvariantKernel
# (rc/invariants.hpp holds the checks). It positions the scene's player and
# cursor, sets the plan, lets the PathRenderer compute the trace, gathers what
# the renderer and the scene expose and hands them to the kernel. The sweeps
# run the kernel without a scene tree (SweepRunner.sweep_native / SweepKernel);
# this driver is the live-renderer path the smoke combos and unit tests use.

var _scene: Node
var _player: CharacterBody2D
var _cursor: Node2D
var _renderer: Node2D
var _game_mgr: Node
var _plan: PlanManager = null

func setup(scene: Node) -> void:
	_scene = scene
	_player = scene.get_node_or_null("Player")
	_cursor = scene.get_node_or_null("Cursor")
	_renderer = scene.get_node_or_null("PathRenderer")
	_game_mgr = scene.get_node_or_null("GameManager")
	# the plan the renderer reads: the GameManager's, or the checker's own
	# (scenes without a GameManager)
	if _game_mgr and "plan" in _game_mgr:
		_plan = _game_mgr.plan
	elif _renderer and "plan_override" in _renderer:
		_plan = PlanManager.new()
		_renderer.plan_override = _plan
	_defs_cache = SceneDefsCache.new()
	_scene_cache = null

func check_all(player_pos: Vector2, cursor_pos: Vector2, plan_entries: Array = []) -> Array[String]:
	_position_nodes(player_pos, cursor_pos)
	var plan_applied := false
	if _plan != null:
		_plan.clear()
		for entry in plan_entries:
			_plan.add_entry(entry.surface_id, entry.side)
		plan_applied = plan_entries.size() > 0
	if _renderer:
		_renderer._compute_trace()
	var out: Array[String] = []
	out.assign(InvariantKernel.check_all(player_pos, cursor_pos, _traced_path(), _planned_path(), _typed_steps(), _beam_result(),
		surface_infos(_surfaces(), _flags()), _defs(), _flags(), plan_entries, _renderer != null, plan_applied, FeatureFlags.infinity_event()))
	return out

# DIRECTION-ONLY alone, for the traces the renderer computes for these
# positions and this plan.
func check_DIRECTION_ONLY(player_pos: Vector2, cursor_pos: Vector2, plan_entries: Array) -> Array[String]:
	_position_nodes(player_pos, cursor_pos)
	if _plan != null:
		_plan.clear()
		for entry in plan_entries:
			_plan.add_entry(entry.surface_id, entry.side)
	if _renderer:
		_renderer._compute_trace()
	var out: Array[String] = []
	out.assign(InvariantKernel.check_direction_only(player_pos, cursor_pos, _beam_result(), _defs(), _flags(), plan_entries, FeatureFlags.infinity_event()))
	return out

# ---------------------------------------------------------------- the kernel's statics

static func _is_infinity_gap(prev_end: Vector2, curr_start: Vector2) -> bool:
	return InvariantKernel.is_infinity_gap(prev_end, curr_start)

static func _is_at_bounds(p: Vector2, bounds: Rect2) -> bool:
	return InvariantKernel.is_at_bounds(p, bounds)

static func _geometric_carrier_dist(point: Vector2, carrier: GeneralizedCircle) -> float:
	return InvariantKernel.geometric_carrier_dist(point, carrier)

# One segment per frame, collinear stages merged: [{start, end}]
static func _extract_trace_geometry(path: TracePath) -> Array:
	return InvariantKernel.extract_trace_geometry(path)

static func check_effect_law(events: Array, scene: BeamScene) -> Array[String]:
	var out: Array[String] = []
	out.assign(InvariantKernel.check_effect_law(events, scene))
	return out

static func check_event_on_beam(events: Array, scene: BeamScene) -> Array[String]:
	var out: Array[String] = []
	out.assign(InvariantKernel.check_event_on_beam(events, scene))
	return out

static func check_beam_order(events: Array) -> Array[String]:
	var out: Array[String] = []
	out.assign(InvariantKernel.check_beam_order(events))
	return out

static func check_plan_effects_applied_events(events: Array, plan: Array, cursor_reached: bool) -> Array[String]:
	var out: Array[String] = []
	out.assign(InvariantKernel.check_plan_effects_applied_events(events, plan, cursor_reached))
	return out

# The game surfaces as the kernel reads them: id, segment, and whether a
# trace may not cross the surface (a transformative side under `flags`, or
# player_solid).
static func surface_infos(surfaces: Array, flags: Dictionary) -> Array:
	var out: Array = []
	var state := GameState.new(flags)
	for surf in surfaces:
		var s: Surface = surf
		var has_effect := false
		for side in [Side.Value.LEFT, Side.Value.RIGHT]:
			var cfg: SideConfig = s.active_side_config(side, state)
			if cfg != null and cfg.effect != null and cfg.effect.is_transformative():
				has_effect = true
		if s.player_solid:
			has_effect = true
		out.append({"id": s.id, "segment": s.segment, "has_effect": has_effect})
	return out

# ---------------------------------------------------------------- the renderer's state

func _position_nodes(player_pos: Vector2, cursor_pos: Vector2) -> void:
	if _player:
		_player.global_position = player_pos
	if _cursor:
		_cursor.global_position = cursor_pos

func _traced_path():
	return _renderer.get_traced_path() if _renderer else null

func _planned_path():
	return _renderer.get_planned_path() if _renderer else null

func _typed_steps() -> Array:
	return _renderer.get_typed_steps() if _renderer else []

func _surfaces() -> Array:
	if not _renderer:
		return []
	var parent := _renderer.get_parent()
	return parent.surfaces if parent and "surfaces" in parent else []

# The flags the renderer's traces started from (the game state's).
func _flags() -> Dictionary:
	return _renderer.get_flags() if _renderer and _renderer.has_method("get_flags") else {}

func _beam_result():
	return _renderer.last_beam_result if _renderer and "last_beam_result" in _renderer else null

var _defs_cache := SceneDefsCache.new()
var _scene_cache: BeamScene = null
var _scene_version := -1

# Scene defs of the checked scene, built once per surface set (the same
# surfaces array with the same surfaces: sweeps never edit surfaces).
func _defs() -> Array:
	var defs := _defs_cache.defs(_surfaces())
	if _scene_cache == null or _defs_cache.version != _scene_version:
		_scene_version = _defs_cache.version
		_scene_cache = BeamScene.from_defs(NumCtx.f64(), defs)
		_scene_cache.emit_infinity = FeatureFlags.infinity_event()
	return defs

func _beam_scene() -> BeamScene:
	_defs()
	return _scene_cache
