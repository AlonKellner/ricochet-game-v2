extends GutTest

func before_each() -> void:
	Surface.reset_id_counter()

func _make_surface(seg: Segment) -> Surface:
	var config := SideConfig.new()
	return Surface.new(seg, config, config)

func test_display_end_identity_frame_near_noop() -> void:
	var seg := Segment.from_coords(Vector2(100, 0), Vector2(100, 600), Vector2(100, 300))
	var surface := _make_surface(seg)
	var hit := TraceHit.new(1.0, Vector2(100, 300), seg, Side.Value.LEFT, true)
	var step := TraceStep.new(Vector2(50, 300), Vector2(100.001, 300), TraceStep.IDENTITY_FRAME, hit, null)
	step.surface_id = surface.id
	var display_end: Vector2 = VisualConverter.compute_display_end(step, [surface])
	assert_almost_eq(display_end.x, 100.0, 0.01, "Should snap to carrier at x=100")
	assert_almost_eq(display_end.y, 300.0, 0.01, "Y should stay unchanged")

func test_display_end_no_hit_returns_end() -> void:
	var step := TraceStep.new(Vector2(50, 300), Vector2(200, 300), TraceStep.IDENTITY_FRAME, null, null)
	var display_end: Vector2 = VisualConverter.compute_display_end(step)
	assert_eq(display_end, step.end, "No hit → display_end should equal step.end")

func test_display_end_null_segment_returns_end() -> void:
	var hit := TraceHit.new(1.0, Vector2(200, 300), null, Side.Value.LEFT, false)
	var step := TraceStep.new(Vector2(50, 300), Vector2(200, 300), TraceStep.IDENTITY_FRAME, hit, null)
	var display_end: Vector2 = VisualConverter.compute_display_end(step)
	assert_eq(display_end, step.end, "No surface_id → display_end should equal step.end")

func test_display_end_on_circle_carrier() -> void:
	var seg := Segment.from_coords(Vector2(300, 200), Vector2(300, 400), Vector2(400, 300))
	var surface := _make_surface(seg)
	var carrier := seg.get_carrier()
	var hit := TraceHit.new(1.0, Vector2(400, 300), seg, Side.Value.LEFT, true)
	var step := TraceStep.new(Vector2(200, 300), Vector2(401.0, 300.5), TraceStep.IDENTITY_FRAME, hit, null)
	step.surface_id = surface.id
	var display_end: Vector2 = VisualConverter.compute_display_end(step, [surface])
	var dist_before := absf(carrier.evaluate(step.end))
	var dist_after := absf(carrier.evaluate(display_end))
	assert_lt(dist_after, dist_before, "Display end should be closer to carrier than raw end")

func test_display_end_preserves_point_already_on_carrier() -> void:
	var seg := Segment.from_coords(Vector2(100, 0), Vector2(100, 600), Vector2(100, 300))
	var surface := _make_surface(seg)
	var hit := TraceHit.new(1.0, Vector2(100, 300), seg, Side.Value.LEFT, true)
	var step := TraceStep.new(Vector2(50, 300), Vector2(100, 300), TraceStep.IDENTITY_FRAME, hit, null)
	step.surface_id = surface.id
	var display_end: Vector2 = VisualConverter.compute_display_end(step, [surface])
	assert_almost_eq(display_end, step.end, Vector2(0.001, 0.001), "Point already on carrier should stay")
