extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

func _reflective_arc() -> Surface:
	var seg := Segment.from_coords(Vector2(400, 500), Vector2(500, 750), Vector2(350, 650))
	var refl := ReflectionEffect.new(seg.get_carrier())
	var config := SideConfig.new(refl, true)
	return Surface.new(seg, config, config, false, false)

func _step(path: TracePath, idx: int) -> TraceStep:
	return path.steps[idx]

# ============================================================
# Bug 1: Ray terminates at origin after circular wrap
# ============================================================

func test_bug1_reproduce_origin_termination() -> void:
	var arc := _reflective_arc()
	var player := Vector2(500, 450)
	var cursor := Vector2(383.4, 566.0)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, [arc], GameState.new(), ray, -1.0,
		TraceMode.PHYSICAL, TraceMode.PHYSICAL, [], null, cursor)

	var reflect_step: TraceStep = null
	for step in path.steps:
		if step.hit and step.hit.on_segment and step.surface_id == arc.id:
			reflect_step = step
			break
	assert_not_null(reflect_step, "Should have an on-segment reflection step")

	var post_reflect_count := 0
	var found_reflect := false
	for step in path.steps:
		if step == reflect_step:
			found_reflect = true
			continue
		if found_reflect and step.hit and step.hit.on_segment and step.surface_id == arc.id:
			post_reflect_count += 1
	assert_gt(post_reflect_count, 0,
		"BUG1: After circular reflection and full wrap, ray should reflect again at origin, not terminate")

func test_bug1_origin_is_on_segment() -> void:
	var arc := _reflective_arc()
	var player := Vector2(500, 450)
	var cursor := Vector2(383.4, 566.0)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, [arc], GameState.new(), ray, -1.0,
		TraceMode.PHYSICAL, TraceMode.PHYSICAL, [], null, cursor)

	var on_seg_reflect: TraceStep = null
	for step in path.steps:
		if step.hit and step.hit.on_segment and step.surface_id == arc.id:
			on_seg_reflect = step
			break
	assert_not_null(on_seg_reflect, "Should have on-seg reflection")
	var reflect_point := on_seg_reflect.end
	var side_from_reflect: Side.Value = arc.segment.determine_side(
		reflect_point + (reflect_point - arc.segment.via.coords).normalized() * 0.01)
	assert_true(side_from_reflect == Side.Value.LEFT or side_from_reflect == Side.Value.RIGHT,
		"Reflection point should be on the segment (determine_side valid)")

func test_bug1_arc_is_reflective_both_sides() -> void:
	var arc := _reflective_arc()
	var state := GameState.new()
	var left_config := arc.active_side_config(Side.Value.LEFT, state)
	var right_config := arc.active_side_config(Side.Value.RIGHT, state)
	assert_not_null(left_config.effect, "LEFT side should have an effect")
	assert_not_null(right_config.effect, "RIGHT side should have an effect")
	assert_eq(left_config.effect.kind(), Effect.Kind.TRANSFORMATIVE, "LEFT should be transformative")
	assert_eq(right_config.effect.kind(), Effect.Kind.TRANSFORMATIVE, "RIGHT should be transformative")

# ============================================================
# Bug 2: False plan divergence on off-segment carrier hit
# ============================================================

func test_bug2_reproduce_false_divergence() -> void:
	var arc := _reflective_arc()
	var player := Vector2(500, 450)
	var cursor := Vector2(392.4, 660.2)
	var plan_entries: Array = [PlanManager.PlanEntry.new(arc.id, Side.Value.LEFT)]
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)

	var phys := TestTrace.trace(player, aim, [arc], GameState.new(), ray, -1.0,
		TraceMode.PHYSICAL, TraceMode.PHYSICAL, plan_entries, null, cursor)
	var planned := TestTrace.trace(player, aim, [arc], GameState.new(), ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, plan_entries, null, cursor)

	var phys_first_hit: TraceStep = null
	for step in phys.steps:
		if step.surface_id == arc.id:
			phys_first_hit = step
			break
	assert_not_null(phys_first_hit, "Physical trace should hit the arc")
	assert_false(phys_first_hit.hit_on_segment,
		"Physical first hit should be off-segment (carrier extension)")

	var phys_on_seg: TraceStep = null
	for step in phys.steps:
		if step.surface_id == arc.id and step.hit_on_segment:
			phys_on_seg = step
			break
	assert_not_null(phys_on_seg, "Physical trace should have an on-segment hit")

	var planned_first_hit: TraceStep = null
	for step in planned.steps:
		if step.surface_id == arc.id:
			planned_first_hit = step
			break
	assert_not_null(planned_first_hit, "Planned trace should hit the arc")

	# The step created at the off-segment hit has frame_id=0 in both (step created BEFORE effect).
	# The real divergence shows up in the NEXT step: if PLANNED consumed the plan entry at the
	# off-segment hit, the next step would be in a different frame (conjugating).
	var phys_first_hit_idx := -1
	for i in phys.steps.size():
		if phys.steps[i] == phys_first_hit:
			phys_first_hit_idx = i
			break
	var planned_first_hit_idx := -1
	for i in planned.steps.size():
		if planned.steps[i] == planned_first_hit:
			planned_first_hit_idx = i
			break

	# Check the step AFTER the first arc hit — that's where divergence shows
	var phys_next_frame: int = phys.steps[phys_first_hit_idx + 1].frame_id if phys_first_hit_idx + 1 < phys.steps.size() else -1
	var planned_next_frame: int = planned.steps[planned_first_hit_idx + 1].frame_id if planned_first_hit_idx + 1 < planned.steps.size() else -1
	assert_eq(phys_next_frame, planned_next_frame,
		"BUG2: Step after off-seg hit should be in same frame for both traces (planned should not apply plan at off-seg hit)")

func test_bug2_physical_off_seg_then_on_seg() -> void:
	var arc := _reflective_arc()
	var player := Vector2(500, 450)
	var cursor := Vector2(392.4, 660.2)
	var plan_entries: Array = [PlanManager.PlanEntry.new(arc.id, Side.Value.LEFT)]
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)

	var phys := TestTrace.trace(player, aim, [arc], GameState.new(), ray, -1.0,
		TraceMode.PHYSICAL, TraceMode.PHYSICAL, plan_entries, null, cursor)

	var arc_hits: Array = []
	for step in phys.steps:
		if step.surface_id == arc.id:
			arc_hits.append(step)
	assert_gte(arc_hits.size(), 2, "Should have at least 2 hits on the arc carrier")
	assert_false(arc_hits[0].hit_on_segment, "First hit should be off-segment")
	assert_true(arc_hits[1].hit_on_segment, "Second hit should be on-segment")

# ============================================================
# Bug 2 diagnostic: side values prove the side-matching theory
# ============================================================

func test_bug2_circle_hit_sides() -> void:
	var arc := _reflective_arc()
	var player := Vector2(500, 450)
	var cursor := Vector2(392.4, 660.2)
	var plan_entries: Array = [PlanManager.PlanEntry.new(arc.id, Side.Value.LEFT)]
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)

	var phys := TestTrace.trace(player, aim, [arc], GameState.new(), ray, -1.0,
		TraceMode.PHYSICAL, TraceMode.PHYSICAL, plan_entries, null, cursor)

	var arc_hits: Array = []
	for step in phys.steps:
		if step.surface_id == arc.id:
			arc_hits.append(step)
	assert_gte(arc_hits.size(), 2, "Should have at least 2 hits on the arc carrier")
	assert_eq(arc_hits[0].hit_side, Side.Value.RIGHT,
		"First hit (entry from outside) should approach from RIGHT side")
	assert_eq(arc_hits[1].hit_side, Side.Value.LEFT,
		"Second hit (exit from inside) should approach from LEFT side — matches plan")

func test_bug2_line_carrier_hit_side() -> void:
	var w_top := RoomBuilder.create_block_surface(Vector2(560, 240), Vector2(1360, 240), Vector2(960, 240))
	var w_bot := RoomBuilder.create_block_surface(Vector2(1360, 840), Vector2(560, 840), Vector2(960, 840))
	var w_left := RoomBuilder.create_block_surface(Vector2(560, 840), Vector2(560, 240), Vector2(560, 540))
	var m1_seg := Segment.from_coords(Vector2(800, 300), Vector2(800, 780), Vector2(800, 540))
	var m1_refl := ReflectionEffect.new(m1_seg.get_carrier())
	var m1 := Surface.new(m1_seg, SideConfig.new(m1_refl, true), SideConfig.new(null, false), false, false)
	var m2_seg := Segment.from_coords(Vector2(1200, 300), Vector2(1200, 780), Vector2(1200, 540))
	var m2_refl := ReflectionEffect.new(m2_seg.get_carrier())
	var m2 := Surface.new(m2_seg, SideConfig.new(m2_refl, true), SideConfig.new(null, false), false, false)
	var surfaces := [w_top, w_bot, w_left, m1, m2]

	var player := Vector2(930.0002, 827.9246)
	var cursor := Vector2(717.7476, 825.5289)
	var plan_entries: Array = [PlanManager.PlanEntry.new(m1.id, Side.Value.LEFT)]
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)

	var phys := TestTrace.trace(player, aim, surfaces, GameState.new(), ray, -1.0,
		TraceMode.PHYSICAL, TraceMode.PHYSICAL, plan_entries, null, cursor)

	var m1_hit: TraceStep = null
	for step in phys.steps:
		if step.surface_id == m1.id:
			m1_hit = step
			break
	assert_not_null(m1_hit, "Should hit the line mirror")
	assert_eq(m1_hit.hit_side, Side.Value.LEFT,
		"Line carrier hit should approach from LEFT side — matches plan")

func test_bug2_working_case_player_inside_carrier() -> void:
	var arc := _reflective_arc()
	var player := Vector2(463.3, 500.0)
	var cursor := Vector2(392.4, 660.2)
	var plan_entries: Array = [PlanManager.PlanEntry.new(arc.id, Side.Value.LEFT)]
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)

	var phys := TestTrace.trace(player, aim, [arc], GameState.new(), ray, -1.0,
		TraceMode.PHYSICAL, TraceMode.PHYSICAL, plan_entries, null, cursor)
	var planned := TestTrace.trace(player, aim, [arc], GameState.new(), ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, plan_entries, null, cursor)

	var phys_first_hit: TraceStep = null
	for step in phys.steps:
		if step.surface_id == arc.id:
			phys_first_hit = step
			break
	assert_not_null(phys_first_hit, "Physical should hit the arc")
	assert_true(phys_first_hit.hit_on_segment,
		"Player inside carrier: first hit should be on-segment")

	var planned_first_hit: TraceStep = null
	for step in planned.steps:
		if step.surface_id == arc.id:
			planned_first_hit = step
			break
	assert_not_null(planned_first_hit, "Planned should hit the arc")
	assert_eq(phys_first_hit.frame_id, planned_first_hit.frame_id,
		"Player inside carrier: traces should agree (no false divergence)")
