extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

func test_grazing_arc_radius_equals_half_surface_radius() -> void:
	var center := Vector2(960, 540)
	var r := 200.0

	var seg := Segment.from_coords(
		Vector2(center.x + r, center.y + 0.01),
		Vector2(center.x + r, center.y - 0.01),
		Vector2(center.x - r, center.y))
	var carrier := seg.get_carrier()
	var refl := ReflectionEffect.new(carrier)
	var config := SideConfig.new(refl, true)
	var surf := Surface.new(seg, config, config, false, false)
	var room := RoomBuilder.create_room_surfaces(Rect2(0, 0, 1920, 1080))
	var surfaces: Array = room + [surf]

	var player := Vector2(200, 341)
	var cursor := Vector2(1700, 341)
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())

	var arc_step: TraceStep = null
	for step in path.steps:
		if step.frame_id != 0 and step.is_arc_step:
			arc_step = step
			break

	assert_not_null(arc_step, "Should have a reflected arc step")
	if arc_step == null:
		return

	var is_arc := VisualConverter.is_arc(arc_step.start, arc_step.via, arc_step.end)
	if is_arc:
		var p := VisualConverter.arc_params(arc_step.start, arc_step.via, arc_step.end)
		assert_almost_eq(p["radius"], r / 2.0, 50.0,
			"Grazing arc radius should be ~r/2=%s, got %s" % [r/2.0, p["radius"]])
	else:
		fail_test("Reflected arc step should render as arc, not line. " +
			"start=%s via=%s end=%s" % [arc_step.start, arc_step.via, arc_step.end])

func test_grazing_arc_radius_multiple_angles() -> void:
	var center := Vector2(960, 540)
	var r := 200.0

	var seg := Segment.from_coords(
		Vector2(center.x + r, center.y + 0.01),
		Vector2(center.x + r, center.y - 0.01),
		Vector2(center.x - r, center.y))
	var carrier := seg.get_carrier()
	var refl := ReflectionEffect.new(carrier)
	var config := SideConfig.new(refl, true)
	var surf := Surface.new(seg, config, config, false, false)
	var room := RoomBuilder.create_room_surfaces(Rect2(0, 0, 1920, 1080))
	var surfaces: Array = room + [surf]

	for y_offset in [1.0, 5.0, 10.0, 20.0, 50.0]:
		H.reset_counters()
		var player := Vector2(200, 340 + y_offset)
		var cursor := Vector2(1700, 340 + y_offset)
		var aim := Direction.from_coords(player, cursor)
		var path := TestTrace.trace(player, aim, surfaces, GameState.new())

		var d: float = r - y_offset
		var expected_radius: float = r * r / (2.0 * d)

		for step in path.steps:
			if step.frame_id != 0 and step.is_arc_step:
				var is_arc := VisualConverter.is_arc(step.start, step.via, step.end)
				assert_true(is_arc, "y_offset=%s: should produce valid arc" % y_offset)
				if is_arc:
					var p := VisualConverter.arc_params(step.start, step.via, step.end)
					assert_almost_eq(p["radius"], expected_radius, 1.0,
						"y_offset=%s: radius should be r²/(2d)=%s, got %s" % [
							y_offset, expected_radius, p["radius"]])
				break
