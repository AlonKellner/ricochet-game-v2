extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

func _inversion_surface() -> Surface:
	var seg := Segment.from_coords(Vector2(200, 100), Vector2(200, 300), Vector2(300, 200))
	var carrier := seg.get_carrier()
	var inv := CircleInversionEffect.new(carrier)
	var left := SideConfig.new(inv, true)
	var right := SideConfig.new(null, false)
	return Surface.new(seg, left, right, false, false)

func _s16_2_surfaces() -> Array:
	var inv_surf := _inversion_surface()
	var w_left := H.wall(0)
	var w_right := H.wall(600)
	var w_top := RoomBuilder.create_block_surface(Vector2(0, 0), Vector2(600, 0), Vector2(300, 0))
	var w_bot := RoomBuilder.create_block_surface(Vector2(0, 400), Vector2(600, 400), Vector2(300, 400))
	return [inv_surf, w_left, w_right, w_top, w_bot]

func _trace_both(surfaces: Array, player: Vector2, cursor: Vector2, plan: Array) -> Dictionary:
	var pair := TestTrace.pair_of(player, cursor, surfaces, plan)
	return {"physical": pair.physical, "planned": pair.planned}

# --- Test 2: Bounce point on carrier ---

# The aim line meets the carrier twice ahead: (100,200) off the arc, reached
# from outside (LEFT side), and (300,200) on the arc, reached from inside
# (RIGHT side). Beam/GAME_SPEC §14.1: PLANNED applies the next plan entry's
# effect at its carrier hit regardless of is_on_segment, so the planned LEFT
# bounce is (100,200) (legacy Planner picked the on-arc point (300,200), whose
# side is RIGHT). The physical trace passes (100,200) without effect.
func test_stage43_bounce_point_on_carrier() -> void:
	var inv_surf := _inversion_surface()
	var plan := [PlanManager.PlanEntry.new(inv_surf.id, Side.Value.LEFT)]
	var player := Vector2(50, 200)
	var cursor := Vector2(400, 200)
	var both := _trace_both([inv_surf], player, cursor, plan)
	var planned_path: TracePath = both.planned
	assert_gt(planned_path.steps.size(), 1, "Should have steps")
	var first_step: TraceStep = planned_path.steps[0]
	assert_true(first_step.hit != null and first_step.surface_id == inv_surf.id,
		"First planned step ends on the inversion surface's carrier")
	assert_eq(first_step.hit_side, Side.Value.LEFT, "Planned bounce is on the planned (LEFT) side")
	assert_almost_eq(first_step.end, Vector2(100, 200), Vector2(1, 1),
		"Planned bounce is the first LEFT-side carrier crossing (100,200)")
	assert_ne(planned_path.steps[1].frame_id, TraceStep.IDENTITY_FRAME, "Inversion applied at the planned bounce")
	var physical: TracePath = both.physical
	assert_eq(physical.steps[1].frame_id, TraceStep.IDENTITY_FRAME, "Physical: off-arc crossing is a pass-through")

# --- Test 3: Full S16.2 plan-physical merge produces valid result ---

func test_stage43_S16_2_plan_physical_aligned() -> void:
	var surfaces := _s16_2_surfaces()
	var inv_surf: Surface = surfaces[0]
	var plan := [PlanManager.PlanEntry.new(inv_surf.id, Side.Value.LEFT)]
	var player := Vector2(50, 200)
	var cursor := Vector2(400, 200)
	var result := _trace_both(surfaces, player, cursor, plan)
	var physical: TracePath = result.physical
	var planned: TracePath = result.planned
	assert_gt(physical.steps.size(), 0, "Physical trace should have steps")
	assert_gt(planned.steps.size(), 0, "Planned trace should have steps")
	var ci := planned.cursor_index if planned.cursor_index >= 0 else planned.steps.size()
	var merged := StepTreeMerge.merge(planned.steps, physical.steps, ci)
	assert_gt(merged.size(), 0, "Merged steps should not be empty")
	var first: TraceStep = merged[0]
	assert_eq(first.type, StepTypes.Type.ALIGNED,
		"First step should be ALIGNED (both traces agree on path to arc surface)")

# --- Test 5: Planned visual path has arc ---

# Cursor off the y=200 axis: the beam aims at a line through the inversion
# center for cursor (400,200), and such a line inverts to itself (no arc).
func test_stage43_planned_visual_path_has_arc() -> void:
	var surfaces := _s16_2_surfaces()
	var inv_surf: Surface = surfaces[0]
	var plan := [PlanManager.PlanEntry.new(inv_surf.id, Side.Value.LEFT)]
	var player := Vector2(50, 200)
	var cursor := Vector2(400, 250)
	var planned: TracePath = _trace_both(surfaces, player, cursor, plan).planned
	var found_arc := false
	for step in planned.steps:
		var s: TraceStep = step
		if s.is_arc_step:
			found_arc = true
	assert_true(found_arc, "Planned path should have an arc step after inversion")

# --- Test 8: Planned trace applies inversion (frame_id changes) ---

func test_stage43_S5_aligned_provenance() -> void:
	var surfaces := _s16_2_surfaces()
	var inv_surf: Surface = surfaces[0]
	var plan := [PlanManager.PlanEntry.new(inv_surf.id, Side.Value.LEFT)]
	var player := Vector2(50, 200)
	var cursor := Vector2(400, 200)
	var result := _trace_both(surfaces, player, cursor, plan)
	var planned: TracePath = result.planned
	assert_gt(planned.steps.size(), 1, "Planned trace should have multiple steps")
	var first_step: TraceStep = planned.steps[0]
	assert_eq(first_step.frame_id, TraceStep.IDENTITY_FRAME,
		"First planned step should be in identity frame (before inversion)")
	var found_inverted := false
	for step in planned.steps:
		var s: TraceStep = step
		if s.frame_id != TraceStep.IDENTITY_FRAME:
			found_inverted = true
	assert_true(found_inverted, "Planned trace should have steps in inverted frame")

# --- Test 9: Merged alignment has correct step types ---

func test_stage43_S6_aligned_match() -> void:
	var surfaces := _s16_2_surfaces()
	var inv_surf: Surface = surfaces[0]
	var plan := [PlanManager.PlanEntry.new(inv_surf.id, Side.Value.LEFT)]
	var player := Vector2(50, 200)
	var cursor := Vector2(400, 200)
	var result := _trace_both(surfaces, player, cursor, plan)
	var physical: TracePath = result.physical
	var planned: TracePath = result.planned
	var ci := planned.cursor_index if planned.cursor_index >= 0 else planned.steps.size()
	var merged := StepTreeMerge.merge(planned.steps, physical.steps, ci)
	var aligned_count := 0
	for step in merged:
		var s: TraceStep = step
		if s.type == StepTypes.Type.ALIGNED or s.type == StepTypes.Type.ALIGNED_POST_PLANNED:
			aligned_count += 1
	assert_gt(aligned_count, 0, "Should have at least one aligned step in merge")
