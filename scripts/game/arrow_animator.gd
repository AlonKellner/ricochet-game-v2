extends Node2D

const ARROW_SPEED := 1600.0
const ARROW_LENGTH := 40.0
const ARROW_HEAD_ANGLE := deg_to_rad(30.0)
const ARROW_HEAD_LENGTH := 16.0

signal flight_completed

class AdvanceResult extends RefCounted:
	var step_index: int
	var progress: float
	var position: Vector2
	var direction: Vector2
	var finished: bool

var _path: Tracer.TracedPath = null
var _current_step_index := 0
var _progress_along_step := 0.0
var _flying := false
var _arrow_position := Vector2.ZERO
var _arrow_direction := Vector2.RIGHT
var _speed_multiplier := 1.0
var _bounds: Rect2 = VisualConverter.DEFAULT_BOUNDS

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	visible = false

func start_flight(path: Tracer.TracedPath, bounds: Rect2 = VisualConverter.DEFAULT_BOUNDS) -> void:
	_path = path
	_current_step_index = 0
	_progress_along_step = 0.0
	_flying = true
	_speed_multiplier = 1.0
	_bounds = bounds
	visible = true
	if _path.steps.size() > 0:
		var step: Tracer.Step = _path.steps[0]
		_arrow_position = step.start
		if _is_arc(step):
			_arrow_direction = _arc_interpolate(step, 0.0).direction
		else:
			_arrow_direction = (step.end - step.start).normalized()
	queue_redraw()

func speed_up() -> void:
	if _flying:
		_speed_multiplier *= 10.0

func is_flying() -> bool:
	return _flying

func get_arrow_position() -> Vector2:
	return _arrow_position

func _process(delta: float) -> void:
	if not _flying or _path == null:
		return
	var distance := ARROW_SPEED * _speed_multiplier * delta
	var r := advance(_path.steps, _current_step_index, _progress_along_step,
		_arrow_position, distance, _bounds)
	_current_step_index = r.step_index
	_progress_along_step = r.progress
	_arrow_position = r.position
	_arrow_direction = r.direction
	queue_redraw()
	if r.finished:
		_finish_flight()

static func _is_arc(step: Tracer.Step) -> bool:
	return step.is_arc_step and VisualConverter.is_arc(step.start, step.via, step.end)

static func _arc_length(step: Tracer.Step) -> float:
	var p := VisualConverter.arc_params(step.start, step.via, step.end)
	return p.radius * p.span

static func _arc_interpolate(step: Tracer.Step, t: float) -> Dictionary:
	var p := VisualConverter.arc_params(step.start, step.via, step.end)
	var ctr: Vector2 = p.center
	var radius: float = p.radius
	var sa := (step.start - ctr).angle()
	var cw: bool = p.clockwise
	var angle: float = sa + t * p.span * (-1.0 if cw else 1.0)
	var pos := ctr + radius * Vector2(cos(angle), sin(angle))
	var rv := pos - ctr
	var tangent: Vector2
	if cw:
		tangent = Vector2(rv.y, -rv.x).normalized()
	else:
		tangent = Vector2(-rv.y, rv.x).normalized()
	return {"position": pos, "direction": tangent}

static func advance(steps: Array, step_index: int, progress: float,
		pos: Vector2, distance: float, bounds: Rect2) -> AdvanceResult:
	var tolerant := bounds.grow(2.0)
	var r := AdvanceResult.new()
	r.step_index = step_index
	r.progress = progress
	r.position = pos
	r.direction = Vector2.RIGHT

	while distance > 0.0 and r.step_index < steps.size():
		var step: Tracer.Step = steps[r.step_index]
		var arc := _is_arc(step)
		var step_length := _arc_length(step) if arc else step.start.distance_to(step.end)
		var remaining := step_length - r.progress

		if distance < remaining:
			r.progress += distance
			var t := r.progress / step_length if step_length > 0.0 else 1.0
			if arc:
				var interp := _arc_interpolate(step, t)
				r.position = interp.position
				r.direction = interp.direction
			else:
				r.position = step.start.lerp(step.end, t)
				r.direction = (step.end - step.start).normalized()
			distance = 0.0
		else:
			distance -= remaining
			r.position = step.end
			r.step_index += 1
			r.progress = 0.0

		if not tolerant.has_point(r.position):
			# Off screen mid-step. An arc may come back on screen within the same
			# step (e.g. a large arc out of screen and back to a hit): resume
			# where it re-enters the bounds. Otherwise complete the step.
			if r.progress > 0.0 and r.step_index < steps.size():
				var cur: Tracer.Step = steps[r.step_index]
				var resumed := false
				if _is_arc(cur):
					var arc_len := _arc_length(cur)
					var t_re := _arc_reentry(cur, r.progress / arc_len, bounds)
					if t_re >= 0.0:
						r.progress = t_re * arc_len
						r.position = _arc_interpolate(cur, t_re).position
						resumed = true
				if not resumed:
					r.position = cur.end
					r.step_index += 1
					r.progress = 0.0
			# Skip through off-bounds step endpoints until finding an on-bounds start
			while not tolerant.has_point(r.position) and r.step_index < steps.size():
				if tolerant.has_point(steps[r.step_index].start):
					r.position = steps[r.step_index].start
					r.progress = 0.0
					break
				r.position = steps[r.step_index].end
				r.step_index += 1
				r.progress = 0.0

		_update_direction(r, steps)

	r.finished = r.step_index >= steps.size()
	return r

# Smallest arc parameter t in (t_now, 1] at which the arc crosses the edge of
# `rect` (the next crossing after leaving it is where it re-enters), or -1.
static func _arc_reentry(step: Tracer.Step, t_now: float, rect: Rect2) -> float:
	var p := VisualConverter.arc_params(step.start, step.via, step.end)
	var ctr: Vector2 = p.center
	var radius: float = p.radius
	var span: float = p.span
	if span <= 0.0:
		return -1.0
	var sa := (step.start - ctr).angle()
	var dirn := -1.0 if p.clockwise else 1.0
	var pts: Array[Vector2] = []
	for x in [rect.position.x, rect.end.x]:
		var h2: float = radius * radius - (x - ctr.x) * (x - ctr.x)
		if h2 >= 0.0:
			for y in [ctr.y - sqrt(h2), ctr.y + sqrt(h2)]:
				if y >= rect.position.y and y <= rect.end.y:
					pts.append(Vector2(x, y))
	for y in [rect.position.y, rect.end.y]:
		var h2: float = radius * radius - (y - ctr.y) * (y - ctr.y)
		if h2 >= 0.0:
			for x in [ctr.x - sqrt(h2), ctr.x + sqrt(h2)]:
				if x >= rect.position.x and x <= rect.end.x:
					pts.append(Vector2(x, y))
	var best := -1.0
	for q in pts:
		var d := fposmod(((q - ctr).angle() - sa) * dirn, TAU)
		var t := d / span
		if t > t_now and t <= 1.0 and (best < 0.0 or t < best):
			best = t
	return best

static func _update_direction(r: AdvanceResult, steps: Array) -> void:
	if r.step_index < steps.size():
		var s: Tracer.Step = steps[r.step_index]
		if _is_arc(s):
			var arc_len := _arc_length(s)
			var t := r.progress / arc_len if arc_len > 0.0 else 0.0
			r.direction = _arc_interpolate(s, t).direction
		else:
			r.direction = (s.end - s.start).normalized()

func _finish_flight() -> void:
	_flying = false
	visible = false
	_path = null
	flight_completed.emit()

func _draw() -> void:
	if not _flying:
		return

	var tip := _arrow_position
	var tail := tip - _arrow_direction * ARROW_LENGTH
	draw_line(tail - global_position, tip - global_position, Color.WHITE, 2.0)

	var head_left := tip - _arrow_direction.rotated(ARROW_HEAD_ANGLE) * ARROW_HEAD_LENGTH
	var head_right := tip - _arrow_direction.rotated(-ARROW_HEAD_ANGLE) * ARROW_HEAD_LENGTH
	var head_points := PackedVector2Array([
		tip - global_position,
		head_left - global_position,
		head_right - global_position,
	])
	draw_polygon(head_points, PackedColorArray([Color.WHITE, Color.WHITE, Color.WHITE]))
