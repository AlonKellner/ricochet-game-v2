extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

func _make_arc_step(start: Vector2, end_v: Vector2, via: Vector2) -> TraceStep:
	var ray := Ray.from_coords(start, Direction.from_coords(start, end_v))
	return TraceStep.make(start, end_v, TraceStep.IDENTITY_FRAME, null, ray, via, true)

func _make_line_step(start: Vector2, end_v: Vector2) -> TraceStep:
	var ray := Ray.from_coords(start, Direction.from_coords(start, end_v))
	var via := (start + end_v) / 2.0
	return TraceStep.make(start, end_v, TraceStep.IDENTITY_FRAME, null, ray, via, false)

# --- CCW semicircle: start=(2,0), end=(-2,0), via=(0,2) ---
# Center=(0,0), radius=2, span=PI, arc_length=2*PI

func test_arc_position_at_midpoint() -> void:
	var step := _make_arc_step(Vector2(2, 0), Vector2(-2, 0), Vector2(0, 2))
	var arc_len := 2.0 * PI
	var r := ArrowAdvance.advance([step], 0, 0.0, step.start, arc_len * 0.5,
		VisualConverter.DEFAULT_BOUNDS())
	assert_almost_eq(r.position, Vector2(0, 2), Vector2(0.01, 0.01),
		"Midpoint of CCW semicircle should be at top (0, 2)")

func test_arc_position_at_quarter() -> void:
	var step := _make_arc_step(Vector2(2, 0), Vector2(-2, 0), Vector2(0, 2))
	var arc_len := 2.0 * PI
	var r := ArrowAdvance.advance([step], 0, 0.0, step.start, arc_len * 0.25,
		VisualConverter.DEFAULT_BOUNDS())
	var expected := Vector2(sqrt(2.0), sqrt(2.0))
	assert_almost_eq(r.position, expected, Vector2(0.01, 0.01),
		"Quarter of CCW semicircle should be at 45 degrees")

func test_arc_constant_visual_speed() -> void:
	var step := _make_arc_step(Vector2(2, 0), Vector2(-2, 0), Vector2(0, 2))
	var chord_dist := step.start.distance_to(step.end)
	assert_almost_eq(chord_dist, 4.0, 0.01, "Chord distance should be 4")
	var arc_len := 2.0 * PI
	assert_gt(arc_len, chord_dist, "Arc length must exceed chord distance")
	var r := ArrowAdvance.advance([step], 0, 0.0, step.start, arc_len * 0.5,
		VisualConverter.DEFAULT_BOUNDS())
	assert_almost_eq(r.position, Vector2(0, 2), Vector2(0.01, 0.01),
		"Half arc_length should reach midpoint, not chord midpoint")

func test_arc_tangent_direction_at_midpoint() -> void:
	var step := _make_arc_step(Vector2(2, 0), Vector2(-2, 0), Vector2(0, 2))
	var arc_len := 2.0 * PI
	var r := ArrowAdvance.advance([step], 0, 0.0, step.start, arc_len * 0.5,
		VisualConverter.DEFAULT_BOUNDS())
	assert_almost_eq(r.direction, Vector2(-1, 0), Vector2(0.01, 0.01),
		"Tangent at top of CCW semicircle should point left (-1, 0)")

func test_arc_tangent_direction_at_start() -> void:
	var step := _make_arc_step(Vector2(2, 0), Vector2(-2, 0), Vector2(0, 2))
	var r := ArrowAdvance.advance([step], 0, 0.0, step.start, 0.001,
		VisualConverter.DEFAULT_BOUNDS())
	assert_almost_eq(r.direction, Vector2(0, 1), Vector2(0.02, 0.02),
		"Tangent at start of CCW semicircle should point up (0, 1)")

func test_clockwise_arc() -> void:
	var step := _make_arc_step(Vector2(0, 2), Vector2(0, -2), Vector2(2, 0))
	var arc_len := 2.0 * PI
	var r := ArrowAdvance.advance([step], 0, 0.0, step.start, arc_len * 0.5,
		VisualConverter.DEFAULT_BOUNDS())
	assert_almost_eq(r.position, Vector2(2, 0), Vector2(0.01, 0.01),
		"Midpoint of CW semicircle should be at (2, 0)")
	assert_almost_eq(r.direction, Vector2(0, -1), Vector2(0.01, 0.01),
		"CW tangent at (2,0) should point down (0, -1)")

func test_line_step_unchanged() -> void:
	var step := _make_line_step(Vector2(0, 0), Vector2(100, 0))
	var r := ArrowAdvance.advance([step], 0, 0.0, step.start, 50.0,
		VisualConverter.DEFAULT_BOUNDS())
	assert_almost_eq(r.position, Vector2(50, 0), Vector2(0.01, 0.01),
		"Line step should use linear interpolation")
	assert_almost_eq(r.direction, Vector2(1, 0), Vector2(0.01, 0.01),
		"Line step direction should be chord direction")

func test_arc_reaches_end() -> void:
	var step := _make_arc_step(Vector2(2, 0), Vector2(-2, 0), Vector2(0, 2))
	var arc_len := 2.0 * PI
	var r := ArrowAdvance.advance([step], 0, 0.0, step.start, arc_len + 1.0,
		VisualConverter.DEFAULT_BOUNDS())
	assert_almost_eq(r.position, Vector2(-2, 0), Vector2(0.01, 0.01),
		"Advancing past arc length should land at step end")
	assert_true(r.finished, "Should be finished after single step")

func test_multi_step_line_then_arc() -> void:
	var line := _make_line_step(Vector2(0, 0), Vector2(2, 0))
	var arc := _make_arc_step(Vector2(2, 0), Vector2(-2, 0), Vector2(0, 2))
	var arc_len := 2.0 * PI
	var r := ArrowAdvance.advance([line, arc], 0, 0.0, line.start,
		2.0 + arc_len * 0.5, VisualConverter.DEFAULT_BOUNDS())
	assert_eq(r.step_index, 1, "Should be on the arc step")
	assert_almost_eq(r.position, Vector2(0, 2), Vector2(0.01, 0.01),
		"After line + half arc, should be at arc midpoint")


# --- Large arc that leaves the screen and comes back (user report) ---
# start=(900,100) end=(1000,100) via=(950,-500): center (950,-197.9), r=302.1.
# The arc goes the long way: out through the top edge at x~721.7 and back in
# at x~1178.3, then down to the end. The arrow must re-appear at the top edge,
# not jump straight to the end.
func test_arc_out_of_screen_resumes_at_reentry_edge() -> void:
	var step := _make_arc_step(Vector2(900, 100), Vector2(1000, 100), Vector2(950, -500))
	var bounds := VisualConverter.DEFAULT_BOUNDS()
	var idx := 0
	var prog := 0.0
	var pos := step.start
	var seen_reentry := false
	var frames := 0
	while idx < 1 and frames < 2000:
		var r := ArrowAdvance.advance([step], idx, prog, pos, 20.0, bounds)
		idx = r.step_index
		prog = r.progress
		pos = r.position
		frames += 1
		if absf(pos.y) <= 2.0 and absf(pos.x - 1178.3) <= 3.0:
			seen_reentry = true
		if idx < 1:
			assert_true(bounds.grow(2.0).has_point(pos), "arrow stays on screen while flying: %s" % pos)
	assert_true(seen_reentry, "arrow re-appears where the arc re-enters the screen")
	assert_eq(pos, step.end, "flight still ends at the step end")
