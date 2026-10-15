extends Node2D

const ARROW_SPEED := 1600.0
const ARROW_LENGTH := 40.0
const ARROW_HEAD_ANGLE := deg_to_rad(30.0)
const ARROW_HEAD_LENGTH := 16.0

signal flight_completed

var _path: TracePath = null
var _current_step_index := 0
var _progress_along_step := 0.0
var _flying := false
var _arrow_position := Vector2.ZERO
var _arrow_direction := Vector2.RIGHT
var _speed_multiplier := 1.0
var _bounds: Rect2 = VisualConverter.DEFAULT_BOUNDS()

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	visible = false

func start_flight(path: TracePath, bounds: Rect2 = VisualConverter.DEFAULT_BOUNDS()) -> void:
	_path = path
	_current_step_index = 0
	_progress_along_step = 0.0
	_flying = true
	_speed_multiplier = 1.0
	_bounds = bounds
	visible = true
	if _path.steps.size() > 0:
		var step: TraceStep = _path.steps[0]
		_arrow_position = step.start
		_arrow_direction = ArrowAdvance.start_direction(step)
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
	var r := ArrowAdvance.advance(_path.steps, _current_step_index, _progress_along_step,
		_arrow_position, distance, _bounds)
	_current_step_index = r.step_index
	_progress_along_step = r.progress
	_arrow_position = r.position
	_arrow_direction = r.direction
	queue_redraw()
	if r.finished:
		_finish_flight()

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
