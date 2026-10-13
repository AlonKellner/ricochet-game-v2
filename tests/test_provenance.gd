extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

# === Point ===

func test_point_at_sets_coords() -> void:
	var p := Point.at(Vector2(42, 99))
	assert_eq(p.coords, Vector2(42, 99), "Coords should match input")

func test_different_coords_not_same_position() -> void:
	var p := Point.at(Vector2(100, 200))
	var q := Point.at(Vector2(100, 201))
	assert_false(p.same_position(q), "Different coords should not be the same position")
	assert_true(p.same_position(Point.at(Vector2(100, 200))), "Equal coords should be the same position")

# === Segment with Point ===
func test_segment_from_coords_fields_are_points() -> void:
	var seg := Segment.from_coords(Vector2(0, 0), Vector2(10, 0), Vector2(5, 0))
	assert_true(seg.start is Point, "start should be Point")
	assert_true(seg.end is Point, "end should be Point")
	assert_true(seg.via is Point, "via should be Point")
	assert_eq(seg.start.coords, Vector2(0, 0), "start coords")
	assert_eq(seg.end.coords, Vector2(10, 0), "end coords")

func test_segment_carrier_uses_coords() -> void:
	var seg := Segment.from_coords(Vector2(0, 0), Vector2(10, 0), Vector2(5, 0))
	var carrier := seg.get_carrier()
	assert_true(carrier.is_line(), "Collinear points should produce a line carrier")

# === Direction and Ray ===

func test_direction_from_coords_fields_are_points() -> void:
	var d := Direction.from_coords(Vector2(10, 20), Vector2(30, 40))
	assert_true(d.start is Point, "start should be Point")
	assert_true(d.end is Point, "end should be Point")
	assert_eq(d.start.coords, Vector2(10, 20))
	assert_eq(d.end.coords, Vector2(30, 40))

func test_direction_to_vector_uses_coords() -> void:
	var d := Direction.from_coords(Vector2(10, 0), Vector2(30, 0))
	assert_eq(d.to_vector(), Vector2(20, 0))

func test_direction_is_zero_length_uses_coords() -> void:
	var d := Direction.from_coords(Vector2(5, 5), Vector2(5, 5))
	assert_true(d.is_zero_length())
	var d2 := Direction.from_coords(Vector2(5, 5), Vector2(6, 5))
	assert_false(d2.is_zero_length())

func test_ray_from_coords_origin_is_point() -> void:
	var d := Direction.from_coords(Vector2(0, 0), Vector2(1, 0))
	var r := Ray.from_coords(Vector2(100, 200), d)
	assert_true(r.origin is Point, "origin should be Point")
	assert_eq(r.origin.coords, Vector2(100, 200))


# === Trace (beam solver) ===

func test_tracer_reflection_terminates_and_aim_correct() -> void:
	var m := H.mirror(400)
	var player := Vector2(600, 300)
	var cursor := Vector2(500, 300)
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, [m], GameState.new())
	assert_true(path.steps.size() > 0, "Should have steps after reflection")
	assert_true(path.steps.size() < 20, "Should terminate quickly with single mirror")

func _build_three_mirrors_surfaces() -> Array:
	var surfaces: Array = []
	# Block lines (walls)
	surfaces.append(RoomBuilder.create_block_surface(
		Vector2(560, 240), Vector2(1360, 240), Vector2(960, 240)))
	surfaces.append(RoomBuilder.create_block_surface(
		Vector2(1360, 840), Vector2(560, 840), Vector2(960, 840)))
	surfaces.append(RoomBuilder.create_block_surface(
		Vector2(560, 840), Vector2(560, 240), Vector2(560, 540)))
	# Mirror at x=800 (left-side reflects)
	var seg1 := Segment.from_coords(Vector2(800, 300), Vector2(800, 780), Vector2(800, 540))
	var refl1 := ReflectionEffect.new(seg1.get_carrier())
	var m1 := Surface.new(seg1, SideConfig.new(refl1, true), SideConfig.new(null, false), false, false)
	surfaces.append(m1)
	# Mirror at x=1200 (left-side reflects)
	var seg2 := Segment.from_coords(Vector2(1200, 300), Vector2(1200, 780), Vector2(1200, 540))
	var refl2 := ReflectionEffect.new(seg2.get_carrier())
	var m2 := Surface.new(seg2, SideConfig.new(refl2, true), SideConfig.new(null, false), false, false)
	surfaces.append(m2)
	# Mirror at x=1000 (right-side reflects)
	var seg3 := Segment.from_coords(Vector2(1000, 400), Vector2(1000, 700), Vector2(1000, 550))
	var refl3 := ReflectionEffect.new(seg3.get_carrier())
	var m3 := Surface.new(seg3, SideConfig.new(null, false), SideConfig.new(refl3, true), false, false)
	surfaces.append(m3)
	return surfaces

func test_aim_does_not_jump_back_after_unplanned_reflections() -> void:
	Surface.reset_id_counter()
	var surfaces := _build_three_mirrors_surfaces()
	var player := Vector2(960, 827.9623)
	var cursor := Vector2(737.7685, 715.3248)
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new(),
		null, -1.0, TraceMode.PHYSICAL, TraceMode.PHYSICAL, [], null, cursor)
	assert_true(path.steps.size() >= 2, "Should have multiple steps")
	# The legacy frame back-transform check is gone (steps carry no frame); the
	# first step must still lie on the aim ray, and the path must be continuous.
	var aim_dir := aim.to_vector().normalized()
	var first: TraceStep = path.steps[0]
	assert_almost_eq((first.start - player).cross(aim_dir), 0.0, 1.0, "First step start on aim ray")
	assert_almost_eq((first.end - player).cross(aim_dir), 0.0, 1.0, "First step end on aim ray")
	for i in range(1, path.steps.size()):
		var step: TraceStep = path.steps[i]
		var prev: TraceStep = path.steps[i - 1]
		if prev.end != step.start and not step.after_portal:
			var gap := prev.end.distance_to(step.start)
			assert_almost_eq(gap, 0.0, 1.0,
				"Step %d should be visually continuous with step %d (gap=%f)" % [i, i - 1, gap])

func test_no_zero_length_carrier_at_bounds_corner() -> void:
	Surface.reset_id_counter()
	var surfaces := _build_three_mirrors_surfaces()
	var m1: Surface = surfaces[3]
	var plan := [PlanManager.PlanEntry.new(m1.id, Side.Value.LEFT)]
	var player := Vector2(1350, 250)
	var cursor := Vector2(1920, 1080)
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new(),
		null, -1.0, TraceMode.PHYSICAL, TraceMode.PHYSICAL, plan, null, cursor)
	assert_gt(path.steps.size(), 0, "Trace should have steps")
	for i in path.steps.size():
		var step: TraceStep = path.steps[i]
		# hit.t > 0 was the legacy "forward hit" test; the beam emits no zero-length steps at all.
		assert_false(step.start == step.end and step.hit != null,
			"No zero-length carrier step at %s" % step.start)

func test_planned_trace_aligns_with_physical_when_plan_matches() -> void:
	Surface.reset_id_counter()
	var surfaces := _build_three_mirrors_surfaces()
	var player := Vector2(960, 827.9623)
	var cursor := Vector2(861.8978, 703.3025)
	var plan := [PlanManager.PlanEntry.new(4, Side.Value.LEFT)]
	var aim := Direction.from_coords(player, cursor)
	var physical := TestTrace.trace(player, aim, surfaces, GameState.new(),
		null, -1.0, TraceMode.PHYSICAL, TraceMode.PHYSICAL, plan, null, cursor)
	var planned := TestTrace.trace(player, aim, surfaces, GameState.new(),
		null, -1.0, TraceMode.PLANNED, TraceMode.PHYSICAL, plan, null, cursor)

	assert_gt(planned.cursor_index, 0, "Planned trace should reach cursor")
	assert_true(planned.cursor_index <= 3, "Planned trace should reach cursor in few steps, got %d" % planned.cursor_index)
	assert_gt(physical.cursor_index, 0, "Physical trace should reach cursor when plan matches")

	var min_ci := mini(physical.cursor_index, planned.cursor_index)
	for i in min_ci:
		var ps: TraceStep = physical.steps[i]
		var ls: TraceStep = planned.steps[i]
		assert_almost_eq(ps.start, ls.start, Vector2(1, 1),
			"Step %d start should align: phys=%s plan=%s" % [i, ps.start, ls.start])
		assert_almost_eq(ps.end, ls.end, Vector2(1, 1),
			"Step %d end should align: phys=%s plan=%s" % [i, ps.end, ls.end])

