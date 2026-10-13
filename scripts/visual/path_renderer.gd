class_name PathRenderer
extends Node2D

const LINE_WIDTH := 2.0
const DASH_ON := 10.0
const DASH_OFF := 5.0
const MAX_DASH_LEN := 4000.0
var _draw_bounds: Rect2 = VisualConverter.DEFAULT_BOUNDS.grow(50.0)

enum DisplayMode { MERGED, PHYSICAL, PLANNED }
const DISPLAY_MODE_NAMES := ["MERGED", "PHYSICAL", "PLANNED"]

var display_mode: int = DisplayMode.MERGED
var _player: CharacterBody2D
var _cursor: Node2D
var _traced_path: TracePath = null
var _planned_path: TracePath = null
var _merged_steps: Array = []
var _physical_hits: Dictionary = {}
var last_solver := ""
# Plan source when the scene has no GameManager (invariant sweeps).
var plan_override: PlanManager = null
var last_beam_result: PrecisionLadder.Result = null

func _ready() -> void:
	_player = get_node_or_null("../Player")
	_cursor = get_node_or_null("../Cursor")
	z_index = 20

func _process(_delta: float) -> void:
	if not _player or not _cursor:
		return
	_compute_trace()
	queue_redraw()

func _compute_trace() -> void:
	if not _player or not _cursor:
		_traced_path = null
		_planned_path = null
		_merged_steps = []
		return

	var player_pos := _player.global_position
	var cursor_pos := _cursor.global_position
	if player_pos == cursor_pos:
		_traced_path = null
		_planned_path = null
		_merged_steps = []
		return

	var surfaces := _get_surfaces()
	var plan := _get_plan()
	var plan_entries: Array = []
	if plan and not plan.is_empty():
		plan_entries = plan.entries

	var bounds := VisualConverter.DEFAULT_BOUNDS
	# The precision-converging beam solver; null only for player = cursor (no
	# line, UX7).
	var pair: BeamTracer.Pair = BeamTracer.trace_pair(surfaces, player_pos, cursor_pos, plan_entries, get_flags())
	last_solver = "beam"
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
	var result := {}
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		if s.surface_id < 0:
			continue
		if not result.has(s.surface_id):
			result[s.surface_id] = []
		var has_continuation := i < path.steps.size() - 1
		result[s.surface_id].append({"side": s.hit_side, "on_segment": s.hit_on_segment, "has_continuation": has_continuation})
	return result

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
	print("[PathRenderer] Display mode: %s" % DISPLAY_MODE_NAMES[display_mode])
	queue_redraw()

# Ghost steps: paler and translucent.
static func ghost_color(col: Color) -> Color:
	var c := col.lerp(Color.WHITE, 0.6)
	c.a = col.a * 0.45
	return c

func _draw() -> void:
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

	_draw_endpoint_circles(_merged_steps, func(s: TraceStep) -> Color: return StepTypes.color(s.type))

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

	_draw_endpoint_circles(path.steps, func(_s: TraceStep) -> Color: return base_color)
	for i in path.steps.size():
		var step: TraceStep = path.steps[i]
		var mid := ((step.start + step.end) / 2.0) - global_position + Vector2(0, -10)
		if _draw_bounds.has_point(mid):
			var col := Color(base_color, 0.4)
			var font := ThemeDB.fallback_font
			var lbl := "%s%d" % [label_prefix, i]
			if step.hit == null:
				lbl += "*"
			draw_string(font, mid, lbl, HORIZONTAL_ALIGNMENT_CENTER, -1, 11, col)

func _draw_endpoint_circles(steps: Array, color_fn: Callable) -> void:
	for step: TraceStep in steps:
		var col: Color = color_fn.call(step)
		col.a = 0.4
		var s_local := step.start - global_position
		var e_local := step.end - global_position
		if _draw_bounds.has_point(s_local):
			draw_circle(s_local, 4.0, col)
		if _draw_bounds.has_point(e_local):
			draw_circle(e_local, 4.0, col)

func _draw_step(step: TraceStep, col: Color, dashed: bool) -> void:
	if step.is_arc_step and VisualConverter.is_arc(step.start, step.via, step.end):
		var p := VisualConverter.arc_params(step.start, step.via, step.end)
		var arc_center: Vector2 = p["center"] - global_position
		if dashed:
			_draw_dashed_arc(arc_center, p["radius"], p["start_angle"], p["end_angle"], p["point_count"], col)
		else:
			draw_arc(arc_center, p["radius"], p["start_angle"], p["end_angle"], p["point_count"], col, LINE_WIDTH)
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
			draw_line(from, to, col, LINE_WIDTH)

func _draw_dashed_arc(center: Vector2, radius: float, start_angle: float, end_angle: float, point_count: int, col: Color) -> void:
	var span := end_angle - start_angle
	var total_len := minf(radius * absf(span), 4000.0)
	if total_len == 0.0:
		return
	var drawn := 0.0
	var on := true
	while drawn < total_len:
		var seg_len := DASH_ON if on else DASH_OFF
		seg_len = minf(seg_len, total_len - drawn)
		if on:
			var a0 := start_angle + (drawn / total_len) * span
			var a1 := start_angle + ((drawn + seg_len) / total_len) * span
			var seg_points := maxi(2, int(point_count * seg_len / total_len))
			draw_arc(center, radius, a0, a1, seg_points, col, LINE_WIDTH)
		drawn += seg_len
		on = not on

func _draw_dashed(from: Vector2, to: Vector2, col: Color) -> void:
	var dir := to - from
	var total_len := minf(dir.length(), MAX_DASH_LEN)
	if total_len == 0.0:
		return
	var unit := dir / total_len
	var drawn := 0.0
	var on := true
	while drawn < total_len:
		var seg_len := DASH_ON if on else DASH_OFF
		seg_len = minf(seg_len, total_len - drawn)
		if on:
			draw_line(from + unit * drawn, from + unit * (drawn + seg_len), col, LINE_WIDTH)
		drawn += seg_len
		on = not on

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
