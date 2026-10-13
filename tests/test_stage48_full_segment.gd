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

# --- Step 1: full property + factory ---

func test_default_segment_not_full() -> void:
	var seg := Segment.from_coords(Vector2(0, 0), Vector2(100, 0), Vector2(50, 0))
	assert_false(seg.full, "Default segment should not be full")

func test_full_from_carrier_circle() -> void:
	var center := Vector2(960, 540)
	var r := 200.0
	var carrier := GeneralizedCircle.from_circle(center, r)
	var seg := Segment.full_from_carrier(carrier)
	assert_true(seg.full, "Factory segment should be full")
	assert_false(seg.is_line(), "Circle carrier should not be a line")
	var derived_center := seg.get_carrier().center()
	var derived_radius := seg.get_carrier().radius()
	assert_almost_eq(derived_center, center, Vector2(0.1, 0.1),
		"Carrier center should match. Got %s" % derived_center)
	assert_almost_eq(derived_radius, r, 0.1,
		"Carrier radius should match. Got %s" % derived_radius)

func test_full_from_carrier_line() -> void:
	var carrier := GeneralizedCircle.from_line(1.0, 0.0, -500.0)
	var seg := Segment.full_from_carrier(carrier)
	assert_true(seg.full, "Factory segment should be full")
	assert_true(seg.is_line(), "Line carrier should be a line")

# --- Step 6: Integration — full circle reflects at the gap ---

func _count_frame_transitions(path: TracePath) -> Array:
	# beam steps carry no frame; with a single circle mirror in the scene the
	# reflected (conjugating) frame is exactly "frame_id is not identity"
	var transitions: Array = []
	var prev_conj := false
	for step in path.steps:
		var s: TraceStep = step
		var conj := s.frame_id != TraceStep.IDENTITY_FRAME
		if conj != prev_conj:
			transitions.append(conj)
			prev_conj = conj
	return transitions

func test_full_circle_reflects_at_old_gap() -> void:
	var center := Vector2(960, 540)
	var r := 200.0
	var surf := _full_circle_mirror(center, r)
	var gap_point := Vector2(center.x, center.y - r)
	var player := gap_point + Vector2(0, -100)
	var aim := Direction.from_coords(player, gap_point)
	var path := TestTrace.trace(player, aim, [surf], GameState.new())
	var transitions := _count_frame_transitions(path)
	assert_gte(transitions.size(), 2,
		"Ray at old gap location should reflect. Got transitions=%s" % [transitions])

func test_full_circle_reflects_horizontal() -> void:
	var center := Vector2(960, 540)
	var r := 200.0
	var surf := _full_circle_mirror(center, r)
	var player := Vector2(center.x - r - 100, center.y)
	var aim := Direction.from_coords(player, center)
	var path := TestTrace.trace(player, aim, [surf], GameState.new())
	var transitions := _count_frame_transitions(path)
	assert_gte(transitions.size(), 2,
		"Horizontal ray through center should reflect. Got transitions=%s" % [transitions])

func test_full_circle_no_slip_through_sweep() -> void:
	var center := Vector2(960, 540)
	var r := 200.0
	var surf := _full_circle_mirror(center, r)
	var slip_count := 0
	for angle_deg in range(0, 360, 5):
		var angle := deg_to_rad(angle_deg)
		var target := center + Vector2(cos(angle), sin(angle)) * r
		var player := target + (target - center).normalized() * 100
		var aim := Direction.from_coords(player, target)
		H.reset_counters()
		var path := TestTrace.trace(player, aim, [surf], GameState.new())
		var transitions := _count_frame_transitions(path)
		if transitions.size() < 2:
			slip_count += 1
	assert_eq(slip_count, 0,
		"No rays should slip through full circle. %d angles failed." % slip_count)
