class_name GdArrowAdvance
extends RefCounted

# Test-support copy of the GDScript ArrowAnimator.advance (the arrow flight
# math) for tests/native/test_diff_visual.gd; deleted in Stage 99 with the
# other Gd* references. The game uses ArrowAdvance (native).

class AdvanceResult extends RefCounted:
	var step_index: int
	var progress: float
	var position: Vector2
	var direction: Vector2
	var finished: bool


static func _is_arc(step: TraceStep) -> bool:
	return step.is_arc_step and GdVisualConverter.is_arc(step.start, step.via, step.end)

static func _arc_length(step: TraceStep) -> float:
	var p := GdVisualConverter.arc_params(step.start, step.via, step.end)
	return p.radius * p.span

static func _arc_interpolate(step: TraceStep, t: float) -> Dictionary:
	var p := GdVisualConverter.arc_params(step.start, step.via, step.end)
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
		var step: TraceStep = steps[r.step_index]
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
				var cur: TraceStep = steps[r.step_index]
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
static func _arc_reentry(step: TraceStep, t_now: float, rect: Rect2) -> float:
	var p := GdVisualConverter.arc_params(step.start, step.via, step.end)
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
		var s: TraceStep = steps[r.step_index]
		if _is_arc(s):
			var arc_len := _arc_length(s)
			var t := r.progress / arc_len if arc_len > 0.0 else 0.0
			r.direction = _arc_interpolate(s, t).direction
		else:
			r.direction = (s.end - s.start).normalized()

