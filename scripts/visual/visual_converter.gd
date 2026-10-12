class_name VisualConverter
extends RefCounted

const POINTS_PER_FULL_CIRCLE := 256
const MAX_ARC_RADIUS := 100000.0
const DEFAULT_BOUNDS := Rect2(0, 0, 1920, 1080)

static func compute_display_end(step: Tracer.Step, surfaces: Array = []) -> Vector2:
	if step.surface_id < 0:
		return step.end
	for surf in surfaces:
		var s: Surface = surf
		if s.id == step.surface_id:
			var carrier: GeneralizedCircle = s.segment.get_carrier()
			var projected := Intersection.project_onto_carrier(step.end, carrier)
			if step.end.distance_to(projected) > 5.0:
				return step.end
			return projected
	return step.end

static func prepare_for_display(path: Tracer.TracedPath, bounds: Rect2, surfaces: Array = []) -> Tracer.TracedPath:
	var grown := bounds.grow(1.0)
	var result := Tracer.TracedPath.new()
	result.targets_hit = path.targets_hit
	result.cursor_index = path.cursor_index
	var cursor_shift := 0
	for i in path.steps.size():
		var step: Tracer.Step = path.steps[i]
		var end_inf := is_inf(step.end.x) or is_inf(step.end.y)
		var start_inf := is_inf(step.start.x) or is_inf(step.start.y)
		var step_via := step.via
		var via_inf := is_inf(step_via.x) and is_inf(step_via.y)
		if (end_inf or start_inf) and via_inf and step.ray != null:
			# For steps to/from infinity `via` is the direction to clip along
			# (see Tracer._add_escape_steps), not a point.
			var dir := step.ray.direction.to_normalized()
			if end_inf and not start_inf:
				step_via = dir
			elif start_inf and not end_inf:
				step_via = -dir
			via_inf = is_inf(step_via.x) and is_inf(step_via.y)
		if not via_inf and not end_inf and not start_inf:
			result.steps.append(_snap_step(step, surfaces))
			continue
		# Steps to / from infinity are clipped to the bounds. A step without a
		# hit that leaves (enters) the bounds exactly at its finite end keeps a
		# zero-length piece there, so the path still starts at the player; a
		# zero-length piece is never emitted for a hit.
		if end_inf and not start_inf and not via_inf:
			var clip := _clip_to_bounds(step.start, step_via.normalized(), bounds)
			if clip != step.start or step.hit == null:
				var via_mid := (step.start + clip) / 2.0
				result.steps.append(_piece(step, step.start, clip, step.hit, via_mid, true))
			continue
		if start_inf and not end_inf and not via_inf:
			var clip := _clip_to_bounds(step.end, step_via.normalized(), bounds)
			if clip != step.end or step.hit == null:
				var via_mid := (clip + step.end) / 2.0
				result.steps.append(_piece(step, clip, step.end, step.hit, via_mid, true))
			continue
		var start_in := grown.has_point(step.start)
		var end_in := grown.has_point(step.end)
		if step.hit != null or (start_in and end_in):
			var vis_dir := (step.end - step.start).normalized()
			if vis_dir == Vector2.ZERO:
				var via_mid := (step.start + step.end) / 2.0
				result.steps.append(_piece(step, step.start, step.end, step.hit, via_mid, true))
				continue
			var esc := _clip_to_bounds(step.start, -vis_dir, bounds)
			var ret := _clip_to_bounds(step.end, vis_dir, bounds)
			var via_out := (step.start + esc) / 2.0 if step.start != esc else step.start
			var via_in := (ret + step.end) / 2.0 if ret != step.end else step.end
			result.steps.append(_piece(step, step.start, esc, null, via_out, true))
			result.steps.append(_piece(step, ret, step.end, step.hit, via_in, false))
			if i < path.cursor_index:
				cursor_shift += 1
		elif start_in:
			var vis_dir := (step.end - step.start).normalized()
			var clip := _clip_to_bounds(step.start, vis_dir, bounds)
			var via_mid := (step.start + clip) / 2.0
			result.steps.append(_piece(step, step.start, clip, step.hit, via_mid, true))
		elif end_in:
			var vis_dir := (step.end - step.start).normalized()
			var clip := _clip_to_bounds(step.end, -vis_dir, bounds)
			var via_mid := (clip + step.end) / 2.0
			result.steps.append(_piece(step, clip, step.end, step.hit, via_mid, true))
		else:
			var via_mid := (step.start + step.end) / 2.0
			result.steps.append(_piece(step, step.start, step.end, step.hit, via_mid, true))
	if path.cursor_index >= 0:
		result.cursor_index = path.cursor_index + cursor_shift
	return result

# A display piece of `step`: geometry from the arguments, metadata from the
# step. The piece carrying the hit gets the hit's surface/side data; only the
# first piece of a split keeps after_portal (it starts the step).
static func _piece(step: Tracer.Step, p_start: Vector2, p_end: Vector2, hit, via: Vector2, first: bool) -> Tracer.Step:
	var piece := Tracer.Step.new(p_start, p_end, step.frame_id, hit, step.ray, step.frame, via, false)
	piece.type = step.type
	piece.est_error = step.est_error
	piece.precision_bits = step.precision_bits
	piece.after_portal = step.after_portal if first else false
	if hit != null:
		piece.surface_id = step.surface_id
		piece.hit_side = step.hit_side
		piece.hit_on_segment = step.hit_on_segment
	return piece

static func _snap_step(step: Tracer.Step, surfaces: Array = []) -> Tracer.Step:
	var display_end := compute_display_end(step, surfaces)
	if display_end == step.end or display_end == step.start:
		return step
	var result := Tracer.Step.new(step.start, display_end, step.frame_id, step.hit, step.ray, step.frame, step.via, step.is_arc_step)
	result.type = step.type
	result.copy_meta_from(step)
	return result

static func _clip_to_bounds(origin: Vector2, dir: Vector2, bounds: Rect2) -> Vector2:
	var min_t := INF
	if dir.x > 0.0:
		min_t = minf(min_t, (bounds.end.x - origin.x) / dir.x)
	elif dir.x < 0.0:
		min_t = minf(min_t, (bounds.position.x - origin.x) / dir.x)
	if dir.y > 0.0:
		min_t = minf(min_t, (bounds.end.y - origin.y) / dir.y)
	elif dir.y < 0.0:
		min_t = minf(min_t, (bounds.position.y - origin.y) / dir.y)
	if is_inf(min_t) or min_t < 0.0:
		min_t = 100.0
	return origin + dir * min_t

static func is_arc(start: Vector2, via: Vector2, end_v: Vector2) -> bool:
	if is_inf(end_v.x) or is_inf(end_v.y):
		return false
	if is_inf(start.x) or is_inf(start.y):
		return false
	if start == end_v:
		return false
	var seg := Segment.from_coords(start, end_v, via)
	var carrier := seg.get_carrier()
	if carrier.is_line():
		return false
	return carrier.radius() < MAX_ARC_RADIUS

static func arc_params(start: Vector2, via: Vector2, end_v: Vector2) -> Dictionary:
	var seg := Segment.from_coords(start, end_v, via)
	var carrier := seg.get_carrier()
	if carrier.is_line():
		return {"center": Vector2.ZERO, "radius": 0.0, "start_angle": 0.0, "end_angle": 0.0, "clockwise": false, "point_count": 2, "span": 0.0}
	var ctr := carrier.center()
	var r := carrier.radius()

	var sa := (start - ctr).angle()
	var ea := (end_v - ctr).angle()

	var cross := (start - ctr).cross(via - ctr)
	var clockwise := cross < 0.0

	var span: float
	if clockwise:
		span = sa - ea
		if span < 0.0:
			span += TAU
	else:
		span = ea - sa
		if span < 0.0:
			span += TAU

	var via_angle := (via - ctr).angle()
	var via_diff: float
	if clockwise:
		via_diff = fposmod(sa - via_angle, TAU)
	else:
		via_diff = fposmod(via_angle - sa, TAU)
	if via_diff > span:
		clockwise = not clockwise
		span = TAU - span

	var point_count := maxi(4, int(POINTS_PER_FULL_CIRCLE * span / TAU))

	var draw_start := sa
	var draw_end := sa + span * (-1.0 if clockwise else 1.0)

	return {
		"center": ctr,
		"radius": r,
		"start_angle": draw_start,
		"end_angle": draw_end,
		"clockwise": clockwise,
		"point_count": point_count,
		"span": span,
	}
