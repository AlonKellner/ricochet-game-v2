extends GutTest

# Stage 110, B2 (GAME_SPEC 12.6): a path cut by the event limit says so
# (TracePath.truncated, kept by the display conversion) and the preview marks
# its last point with a stop marker, in the colour of the step ending there.

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

# Two facing mirrors: a horizontal beam between them bounces until the limit.
func _trapped() -> BeamTracerPair:
	return TestTrace.trace_pair([H.mirror(0), H.mirror(100)], Vector2(50, 300), Vector2(90, 300))

func test_a_path_cut_by_the_limit_is_truncated():
	var pair := _trapped()
	assert_true(pair.physical.truncated, "the physical path is cut by the limit")
	var display := VisualConverter.prepare_for_display(pair.physical, VisualConverter.DEFAULT_BOUNDS(), [])
	assert_true(display.truncated, "the display conversion keeps the flag")

func test_a_path_that_ends_is_not_truncated():
	var pair := TestTrace.trace_pair([H.wall(400)], Vector2(100, 300), Vector2(200, 300))
	assert_false(pair.physical.truncated, "a path ending at a block is not cut")

func test_the_preview_marks_the_cut():
	var pair := _trapped()
	var renderer = load("res://scripts/visual/path_renderer.gd").new()
	add_child_autofree(renderer)
	var bounds := VisualConverter.DEFAULT_BOUNDS()
	renderer._traced_path = VisualConverter.prepare_for_display(pair.physical, bounds, [])
	renderer._planned_path = VisualConverter.prepare_for_display(pair.planned, bounds, [])
	renderer._merged_steps = StepTreeMerge.merge(renderer._planned_path.steps, renderer._traced_path.steps,
		renderer._planned_path.cursor_index)
	var markers: Array = renderer.truncation_markers()
	assert_eq(markers.size(), 1, "one marker for the one (shared) trajectory")
	var last: TraceStep = renderer._traced_path.steps[renderer._traced_path.steps.size() - 1]
	assert_eq(markers[0]["point"], last.end, "at the path's last point")
	assert_eq(markers[0]["color"], StepTypes.color(StepTypes.Type.ALIGNED_POST_PLANNED), "in the step's colour")
