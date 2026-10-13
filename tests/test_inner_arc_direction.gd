extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

func _full_circle_mirror(center: Vector2, r: float) -> Surface:
	var carrier := GeneralizedCircle.from_circle(center, r)
	var seg := Segment.full_from_carrier(carrier)
	var refl := ReflectionEffect.new(carrier)
	var config := SideConfig.new(refl, true)
	return Surface.new(seg, config, config, false, false)

func _inner_trace() -> Dictionary:
	var center := Vector2(960, 540)
	var r := 200.0
	var surf := _full_circle_mirror(center, r)
	var carrier := surf.segment.get_carrier()
	var surfaces: Array = [surf]
	var player := Vector2(960, 600)
	var aim := Direction.from_coords(player, Vector2(700, 540))
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())
	return {"path": path, "carrier": carrier, "center": center, "r": r, "surf": surf, "player": player}

func _outer_trace() -> Dictionary:
	var center := Vector2(960, 540)
	var r := 200.0
	var surf := _full_circle_mirror(center, r)
	var carrier := surf.segment.get_carrier()
	var surfaces: Array = [surf]
	var player := Vector2(600, 540)
	var aim := Direction.from_coords(player, Vector2(1300, 540))
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())
	return {"path": path, "carrier": carrier, "center": center, "r": r, "surf": surf, "player": player}

func _first_reflected_arc(path: TracePath) -> TraceStep:
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		# beam steps carry no frame: the first arc step in a non-identity frame
		# is the first arc after the (conjugating) reflection hit
		if s.frame_id != TraceStep.IDENTITY_FRAME and s.is_arc_step:
			return s
	return null

func _arc_midpoint(step: TraceStep) -> Vector2:
	var p := VisualConverter.arc_params(step.start, step.via, step.end)
	var ctr: Vector2 = p["center"]
	var r: float = p["radius"]
	var sa: float = p["start_angle"]
	var ea: float = p["end_angle"]
	var mid_angle := sa + (ea - sa) * 0.5
	return ctr + Vector2(cos(mid_angle), sin(mid_angle)) * r


func test_inner_arc_curves_through_center() -> void:
	var d := _inner_trace()
	var center: Vector2 = d["center"]

	var arc_step := _first_reflected_arc(d["path"])
	assert_not_null(arc_step, "Should have a reflected arc step")
	if arc_step == null:
		return

	var arc_mid := _arc_midpoint(arc_step)
	assert_lt(arc_mid.distance_to(center), 10.0,
		"Arc midpoint should be near circle center %s, got %s (dist=%.2f)" % [center, arc_mid, arc_mid.distance_to(center)])


func test_inner_arc_passes_through_center() -> void:
	var d_inner := _inner_trace()
	var center: Vector2 = d_inner["center"]

	var inner_arc := _first_reflected_arc(d_inner["path"])
	assert_not_null(inner_arc, "Should have a reflected arc step")
	if inner_arc == null:
		return

	var arc_mid := _arc_midpoint(inner_arc)
	assert_lt(arc_mid.distance_to(center), 10.0,
		"Inner-reflected arc midpoint should be near circle center. dist=%.2f" % arc_mid.distance_to(center))


func test_inner_arc_via_not_on_carrier() -> void:
	var d := _inner_trace()
	var carrier: GeneralizedCircle = d["carrier"]

	var arc_step := _first_reflected_arc(d["path"])
	assert_not_null(arc_step, "Should have a reflected arc step")
	if arc_step == null:
		return

	var eval_start := carrier.evaluate(arc_step.start)
	var eval_end := carrier.evaluate(arc_step.end)
	var eval_via := carrier.evaluate(arc_step.via)

	assert_lt(absf(eval_start), 1.0,
		"Start should be on carrier (eval=%.2f)" % eval_start)
	assert_lt(absf(eval_end), 1.0,
		"End should be on carrier (eval=%.2f)" % eval_end)
	assert_gt(absf(eval_via), 10.0,
		"Via should NOT be on carrier — arc must curve outward, not follow the circle surface. eval=%.2f" % eval_via)
