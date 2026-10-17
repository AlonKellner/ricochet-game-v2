class_name VisibilityRenderer
extends Node2D

# Stage 110: the visibility region (GAME_SPEC 15) -- the cursor positions at
# which the preview's planned and physical paths agree up to the cursor -- as a
# white semi-transparent fill under the preview (z 10, GAME_SPEC 20.4, 22.2).
# Computed natively (BeamTracer.visibility) from the player's position, the plan
# (entries with their clicks and pieces), the flags and the surfaces; never from
# the cursor, so it is recomputed only when one of those changed (Stage 100).
# The departure points of the last plan entry that are visible on their own (a
# planned surface reached off its segment) are drawn as thin lines.
#
# A region takes a few to tens of milliseconds, so in the game it is computed on
# a worker thread (one task at a time, on copies of its inputs) and the newest
# finished region is drawn; `async_compute = false` computes in the frame (the
# tests).
#
# Debug builds compare the region with the preview at the cursor whenever both
# are fresh (LIVE VISIBILITY-EQUALS-NON-DIVERGENCE): the interactive check of
# the invariant the sweeps pin.

const FILL := Color(1.0, 1.0, 1.0, 0.10)
const WHISKER_WIDTH := 1.5

var region = null            # VisibilityRegion of the last computation (null: none)
var visibility_version := 0  # counts the recomputations (finished ones, when asynchronous)
var async_compute := true    # on a worker thread (the game); false: in the frame
var live_mismatches := 0     # debug: cursors where region and preview disagreed

var _tracer := BeamTracer.new()
var _defs_cache := SceneDefsCache.new()
var _inputs = null
var _polygons: Array = []
var _whiskers: Array = []
var _level: Node
var _player: Node2D
var _cursor: Node2D
var _renderer: Node2D
var _live_key = null
var _region_inputs = null    # the inputs `region` was computed for
var _task_id := -1           # the worker task in flight (-1: none)
var _task_inputs = null
var _task_result = null      # set by the worker
var _mutex := Mutex.new()

func _ready() -> void:
	_level = get_parent()
	_player = _level.get_node_or_null("Player")
	_cursor = _level.get_node_or_null("Cursor")
	_renderer = _level.get_node_or_null("PathRenderer")
	_tracer.set_emit_infinity(FeatureFlags.infinity_event())
	z_index = 10
	process_mode = Node.PROCESS_MODE_ALWAYS

func _process(_delta: float) -> void:
	if _player == null or _renderer == null:
		return
	modulate = _renderer.modulate   # dimmed with the path during a flight
	var inputs := current_inputs()
	if _inputs == null or inputs != _inputs:
		_inputs = inputs
		if async_compute:
			_request()
		else:
			recompute()
			queue_redraw()
	if async_compute:
		_collect()
	if OS.is_debug_build():
		_live_check()

func _exit_tree() -> void:
	if _task_id >= 0:
		WorkerThreadPool.wait_for_task_completion(_task_id)
		_task_id = -1

# A worker task for the current inputs, unless one is in flight (its result is
# drawn, then the newest inputs are requested).
func _request() -> void:
	if _task_id >= 0:
		return
	var job := _job()
	_task_inputs = _inputs
	_task_id = WorkerThreadPool.add_task(_work.bind(job), false, "visibility region")

func _work(job: Dictionary) -> void:
	var r = null
	if job["on"]:
		r = _tracer.visibility(job["defs"], job["player"], job["entries"], job["flags"], job["bounds"])
	_mutex.lock()
	_task_result = r
	_mutex.unlock()

func _collect() -> void:
	if _task_id < 0 or not WorkerThreadPool.is_task_completed(_task_id):
		return
	WorkerThreadPool.wait_for_task_completion(_task_id)
	_task_id = -1
	_mutex.lock()
	var r = _task_result
	_task_result = null
	_mutex.unlock()
	_apply(r, _task_inputs)
	queue_redraw()
	if _inputs != _task_inputs:
		_request()

# The region's inputs, copied on the main thread (the plan entries too: the
# player may edit the plan while a task runs).
func _job() -> Dictionary:
	var entries: Array = []
	var plan: PlanManager = _renderer._get_plan()
	if plan:
		for e in plan.entries:
			entries.append(e.copy())
	return {"on": FeatureFlags.visibility(), "defs": _defs_cache.defs(_surfaces()), "player": _player.global_position,
		"entries": entries, "flags": _renderer.get_flags().duplicate(true), "bounds": _bounds()}

func _apply(r, for_inputs) -> void:
	visibility_version += 1
	region = r
	_region_inputs = for_inputs
	_polygons = []
	_whiskers = []
	if region == null or not region.is_supported():
		return
	# a cell cut to a sliver by the bounds may not triangulate: not drawn
	for poly: PackedVector2Array in region.cell_polygons():
		if not Geometry2D.triangulate_polygon(poly).is_empty():
			_polygons.append(poly)
	_whiskers = region.whisker_polylines()

# What the region depends on: the player's position, the plan entries (by
# value, with their clicks and pieces), the flags (a copy), the surfaces (the
# defs cache's version), the infinity switch and the display switch.
func current_inputs() -> Array:
	var entries: Array = []
	var plan: PlanManager = _renderer._get_plan()
	if plan:
		for e in plan.entries:
			entries.append([e.surface_id, e.side, e.click, e.piece])
	return [_player.global_position, entries, _renderer.get_flags().duplicate(true), _defs_cache.version_of(_surfaces()),
		FeatureFlags.infinity_event(), FeatureFlags.visibility()]

func _surfaces() -> Array:
	if "world" in _level and _level.world != null:
		return _level.world.surfaces
	return _level.surfaces if "surfaces" in _level else []

func _bounds() -> Rect2:
	if "level_bounds" in _level:
		return _level.level_bounds
	return VisualConverter.DEFAULT_BOUNDS()

# The region for the current inputs, in the frame (public: the tests call it).
func recompute() -> void:
	var job := _job()
	var r = null
	if job["on"]:
		r = _tracer.visibility(job["defs"], job["player"], job["entries"], job["flags"], job["bounds"])
	_apply(r, current_inputs())

func _draw() -> void:
	for poly: PackedVector2Array in _polygons:
		draw_colored_polygon(poly, FILL)
	for line: PackedVector2Array in _whiskers:
		draw_polyline(line, FILL * Color(1, 1, 1, 3.0), WHISKER_WIDTH)

# The preview at the cursor: aligned up to it and reaching it (GAME_SPEC 15.1).
static func preview_visible(renderer: Node) -> bool:
	var planned: TracePath = renderer._planned_path
	if planned == null or planned.cursor_index < 0:
		return false
	# B4: both walks leave the cursor's own point in one frame
	if renderer.last_pair != null and not renderer.last_pair.cursor_point_agrees:
		return false
	var merged: Array = renderer._merged_steps
	# the display may drop a zero-length piece before the cursor without moving
	# its index: every drawn step before the cursor counts (inv::preview_visible)
	var ci: int = mini(planned.cursor_index, planned.steps.size())
	if merged.size() < ci:
		return false
	for i in ci:
		if merged[i].type != StepTypes.Type.ALIGNED:
			return false
	return true

func _live_check() -> void:
	if region == null or not region.is_supported() or _cursor == null or _renderer.last_pair == null or _region_inputs != _inputs:
		return
	var key := [_inputs, _cursor.global_position, _renderer.trace_version]
	if key == _live_key:
		return
	_live_key = key
	var c: Vector2 = _cursor.global_position
	var m: int = region.contains(c)
	if m < 0 or preview_visible(_renderer) == (m == 1):
		return
	live_mismatches += 1
	push_warning("LIVE VISIBILITY-EQUALS-NON-DIVERGENCE: at the cursor %s the region says %s, the preview %s (player %s, margin %.6f px)" % [
		c, "in" if m == 1 else "out", "visible" if m != 1 else "not", _player.global_position, region.margin(c)])
