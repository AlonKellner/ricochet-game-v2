class_name PathRenderer
extends Node2D

# The beam solver's tracer (owns the solve caches: one per owner).
var _tracer := BeamTracer.new()

const LINE_WIDTH := 2.0
const DASH_ON := 10.0
const DASH_OFF := 5.0
const MAX_DASH_LEN := 4000.0
var _draw_bounds: Rect2 = VisualConverter.DEFAULT_BOUNDS().grow(50.0)

# Stage 100: the trace is recomputed only when one of its inputs changed
# (positions, plan entries, the game state's flags, the surfaces).
# `trace_version` counts the recomputations; a direct `_compute_trace()` call
# always recomputes (the invariant checker and the tests rely on that).
var trace_version := 0
var _defs_cache := SceneDefsCache.new()
var _last_inputs = null   # the inputs of the last computation (null: none yet)

enum DisplayMode { MERGED, PHYSICAL, PLANNED }
const DISPLAY_MODE_NAMES := ["MERGED", "PHYSICAL", "PLANNED"]

var display_mode: int = DisplayMode.MERGED
var _player: Node2D
var _cursor: Node2D
var _traced_path: TracePath = null
var _planned_path: TracePath = null
var _merged_steps: Array = []
var _physical_hits: Dictionary = {}
var last_solver := ""
# Plan source when the scene has no GameManager (invariant sweeps).
var plan_override: PlanManager = null
var last_beam_result: PrecisionLadderResult = null
# Stage 107: the last pair as traced (the raw physical path and the final
# flags): the spawn preview folds it exactly as the landing will.
var last_pair: BeamTracerPair = null

func _ready() -> void:
	_player = get_node_or_null("../Player")
	_cursor = get_node_or_null("../Cursor")
	_draw_bounds = _level_bounds().grow(50.0)
	_tracer.set_emit_infinity(FeatureFlags.infinity_event())
	z_index = 20

func _process(_delta: float) -> void:
	if not _player or not _cursor:
		return
	var inputs := _inputs()
	if _last_inputs != null and inputs == _last_inputs:
		return
	_compute_from(inputs)
	queue_redraw()

# What the traces depend on: the positions, the plan entries (by value), the
# flags (a copy: the state may be edited in place) and the surfaces (the defs
# cache's version: the same array with the same count is the same scene).
func _inputs() -> Array:
	var entries: Array = []
	var plan := _get_plan()
	if plan:
		for e in plan.entries:
			entries.append([e.surface_id, e.side, e.click])
	return [_player.global_position, _cursor.global_position, entries, get_flags().duplicate(true), _defs_cache.version_of(_get_surfaces())]

# The level's bounds (the parent's `level_bounds`; the default 1920x1080
# without one): the display clipping and the drawing bounds.
# Whether the traces are the current inputs' (false between an input change
# and the next _process: the tree is paused during a flight, so after a
# landing the traces lag the new flags by a frame).
func is_current() -> bool:
	return _last_inputs != null and _player != null and _cursor != null and _inputs() == _last_inputs

func _level_bounds() -> Rect2:
	var parent := get_parent()
	if parent and "level_bounds" in parent:
		return parent.level_bounds
	return VisualConverter.DEFAULT_BOUNDS()

func _compute_trace() -> void:
	if not _player or not _cursor:
		_traced_path = null
		_planned_path = null
		_merged_steps = []
		return
	_compute_from(_inputs())

func _compute_from(inputs: Array) -> void:
	_last_inputs = inputs
	trace_version += 1
	var player_pos: Vector2 = inputs[0]
	var cursor_pos: Vector2 = inputs[1]
	if player_pos == cursor_pos:
		_traced_path = null
		_planned_path = null
		_merged_steps = []
		last_pair = null
		return

	var surfaces := _get_surfaces()
	var plan := _get_plan()
	var plan_entries: Array = []
	if plan and not plan.is_empty():
		plan_entries = plan.entries

	var bounds := _level_bounds()
	# The precision-converging beam solver; null only for player = cursor (no
	# line, UX7).
	# the defs cache's version keys the defs: the tracer converts them once per version
	_tracer.defs_key = _defs_cache.version_of(surfaces)
	var pair: BeamTracerPair = _tracer.trace_pair(_defs_cache.defs(surfaces), player_pos, cursor_pos, plan_entries, get_flags(), _defs_cache.segments(surfaces))
	last_solver = "beam"
	last_pair = pair
	if pair == null:
		last_beam_result = null
		_traced_path = null
		_planned_path = null
		_merged_steps = []
		return
	last_beam_result = pair.result
	_traced_path = VisualConverter.prepare_for_display(pair.physical, bounds, surfaces)
	_planned_path = VisualConverter.prepare_for_display(pair.planned, bounds, surfaces)

	var ci: int = _planned_path.cursor_index
	if ci < 0:
		ci = _planned_path.steps.size()
	_merged_steps = StepTreeMerge.merge(_planned_path.steps, _traced_path.steps, ci)
	_physical_hits = build_physical_hits(_traced_path)

func get_physical_hits() -> Dictionary:
	return _physical_hits

static func build_physical_hits(path: TracePath) -> Dictionary:
	return path.physical_hits()

# The game state's flags (the traces start from them); none without a
# GameManager.
func get_flags() -> Dictionary:
	var game_mgr := get_node_or_null("../GameManager")
	if game_mgr and "game_state" in game_mgr:
		return game_mgr.game_state.flags
	return {}

func _get_plan() -> PlanManager:
	if plan_override != null:
		return plan_override
	var game_mgr := get_node_or_null("../GameManager")
	if game_mgr and "plan" in game_mgr:
		return game_mgr.plan
	return null

func _get_surfaces() -> Array:
	var parent := get_parent()
	if parent and "surfaces" in parent:
		return parent.surfaces
	return []

func cycle_display_mode() -> void:
	display_mode = (display_mode + 1) % 3
	queue_redraw()
	print("[PathRenderer] Display mode: %s" % DISPLAY_MODE_NAMES[display_mode])
	queue_redraw()

# Ghost steps: paler and translucent.
static func ghost_color(col: Color) -> Color:
	var c := col.lerp(Color.WHITE, 0.6)
	c.a = col.a * 0.45
	return c

func _draw() -> void:
	DrawRec.begin(self)
	if display_mode == DisplayMode.MERGED:
		_draw_merged()
	elif display_mode == DisplayMode.PHYSICAL:
		_draw_raw_trace(_traced_path, Color.CYAN, "P")
	elif display_mode == DisplayMode.PLANNED:
		_draw_raw_trace(_planned_path, Color.MAGENTA, "L")

func _draw_merged() -> void:
	if _merged_steps.size() == 0:
		return
	for i in _merged_steps.size():
		var ms: TraceStep = _merged_steps[i]
		if ms.start == ms.end:
			continue
		var col := StepTypes.color(ms.type)
		_draw_step(ms, ghost_color(col) if ms.ghost else col, not StepTypes.is_solid(ms.type))

	_draw_endpoint_circles(_merged_steps, true, Color.WHITE)
	for m in truncation_markers():
		_draw_stop_marker(m["point"] - global_position, m["color"])

# GAME_SPEC 12.6: where a path is cut by the event limit, a stop marker at its
# last point, in the colour the merged view draws the step ending there. The
# cursor is not reached on a cut path, so the shot is not a valid aim there
# (B2, Stage 110): [{point, color}], at most one per path.
func truncation_markers() -> Array:
	var out: Array = []
	for path: TracePath in [_traced_path, _planned_path]:
		if path == null or not path.truncated or path.steps.is_empty():
			continue
		var end: Vector2 = path.steps[path.steps.size() - 1].end
		if out.any(func(m): return m["point"] == end):
			continue
		var col := Color.WHITE
		for ms: TraceStep in _merged_steps:
			if ms.end == end:
				col = StepTypes.color(ms.type)
		out.append({"point": end, "color": col})
	return out

const STOP_MARKER_RADIUS := 7.0

func _draw_stop_marker(at: Vector2, col: Color) -> void:
	if not _draw_bounds.has_point(at):
		return
	var pts := PackedVector2Array()
	for k in 9:
		# an octagon (a stop sign), flat on top: vertices at 22.5 + 45 k degrees
		pts.append(at + Vector2.from_angle(PI / 8.0 + k * PI / 4.0) * STOP_MARKER_RADIUS)
	DrawRec.polyline(self, pts, col, LINE_WIDTH)
	DrawRec.line(self, at + Vector2(-0.5, 0) * STOP_MARKER_RADIUS, at + Vector2(0.5, 0) * STOP_MARKER_RADIUS, col, LINE_WIDTH)

func _draw_raw_trace(path: TracePath, base_color: Color, label_prefix: String) -> void:
	if path == null or path.steps.size() == 0:
		return
	var cursor_idx := path.cursor_index if path.cursor_index >= 0 else path.steps.size()
	for i in path.steps.size():
		var step: TraceStep = path.steps[i]
		if step.start == step.end:
			continue
		var col := base_color
		if i >= cursor_idx:
			col = Color(base_color, 0.35)
		if step.ghost:
			col = ghost_color(col)
		_draw_step(step, col, step.hit == null)

	_draw_endpoint_circles(path.steps, false, base_color)
	for i in path.steps.size():
		var step: TraceStep = path.steps[i]
		var mid := ((step.start + step.end) / 2.0) - global_position + Vector2(0, -10)
		if _draw_bounds.has_point(mid):
			var col := Color(base_color, 0.4)
			var font := ThemeDB.fallback_font
			var lbl := "%s%d" % [label_prefix, i]
			if step.hit == null:
				lbl += "*"
			DrawRec.text(self, mid, lbl, HORIZONTAL_ALIGNMENT_CENTER, 11, col)

# by_type: each step's type colour (the merged view), else base_color (Perf P4:
# no Callable per step)
func _draw_endpoint_circles(steps: Array, by_type: bool, base_color: Color) -> void:
	for step: TraceStep in steps:
		var col: Color = StepTypes.color(step.type) if by_type else base_color
		col.a = 0.4
		var s_local := step.start - global_position
		var e_local := step.end - global_position
		if _draw_bounds.has_point(s_local):
			DrawRec.circle(self, s_local, 4.0, col)
		if _draw_bounds.has_point(e_local):
			DrawRec.circle(self, e_local, 4.0, col)

func _draw_step(step: TraceStep, col: Color, dashed: bool) -> void:
	# (Perf P4: is_arc and arc_params in one call, no Dictionary)
	var a := VisualConverter.arc_draw(step.start, step.via, step.end) if step.is_arc_step else PackedFloat64Array()
	if a.size() > 0:
		var arc_center: Vector2 = Vector2(a[0], a[1]) - global_position
		if dashed:
			_draw_dashed_arc(arc_center, a[2], a[3], a[4], int(a[5]), col)
		else:
			DrawRec.arc(self, arc_center, a[2], a[3], a[4], int(a[5]), col, LINE_WIDTH)
	else:
		var from := step.start - global_position
		var to := step.end - global_position
		var clipped := _clip_line_to_rect(from, to, _draw_bounds)
		if clipped.is_empty():
			return
		from = clipped[0]
		to = clipped[1]
		if dashed:
			_draw_dashed(from, to, col)
		else:
			DrawRec.line(self, from, to, col, LINE_WIDTH)

# One draw call per dashed step (Perf P4): the dashes' segments, computed
# natively (VisualConverter.dash_arc / dash_line), in one multiline.
func _draw_dashed_arc(center: Vector2, radius: float, start_angle: float, end_angle: float, point_count: int, col: Color) -> void:
	var pts := VisualConverter.dash_arc(center, radius, start_angle, end_angle, point_count, DASH_ON, DASH_OFF, 4000.0)
	if pts.size() > 0:
		DrawRec.multiline(self, pts, col, LINE_WIDTH)

func _draw_dashed(from: Vector2, to: Vector2, col: Color) -> void:
	var pts := VisualConverter.dash_line(from, to, DASH_ON, DASH_OFF, MAX_DASH_LEN)
	if pts.size() > 0:
		DrawRec.multiline(self, pts, col, LINE_WIDTH)

func has_line() -> bool:
	return _merged_steps.size() > 0

func get_line_from() -> Vector2:
	if _merged_steps.size() > 0:
		var ms: TraceStep = _merged_steps[0]
		return ms.start
	return Vector2.ZERO

func get_line_direction() -> Vector2:
	if _merged_steps.size() > 0:
		var ms: TraceStep = _merged_steps[0]
		return (ms.end - ms.start).normalized()
	return Vector2.ZERO

func get_traced_path() -> TracePath:
	return _traced_path

func get_planned_path():
	return _planned_path

func get_typed_steps() -> Array:
	return _merged_steps

static func _clip_line_to_rect(from: Vector2, to: Vector2, rect: Rect2) -> Array:
	var dx := to.x - from.x
	var dy := to.y - from.y
	var p := [-dx, dx, -dy, dy]
	var q := [from.x - rect.position.x, rect.end.x - from.x,
			  from.y - rect.position.y, rect.end.y - from.y]
	var t0 := 0.0
	var t1 := 1.0
	for i in 4:
		if p[i] == 0.0:
			if q[i] < 0.0:
				return []
		elif p[i] < 0.0:
			t0 = maxf(t0, q[i] / p[i])
		else:
			t1 = minf(t1, q[i] / p[i])
	if t0 > t1:
		return []
	var c0 := from + Vector2(dx, dy) * t0
	var c1 := from + Vector2(dx, dy) * t1
	return [c0, c1]
