extends GutTest

const H := preload("res://tests/test_helpers.gd")
const SurfaceNodeScript = preload("res://scripts/game/surface_node.gd")
const PathRendererScript = preload("res://scripts/visual/path_renderer.gd")

func before_each() -> void:
	H.reset_counters()

func _mirror(x: float) -> Surface:
	return H.mirror(x)

func _left_only_mirror(x: float) -> Surface:
	var seg := Segment.from_coords(Vector2(x, 0), Vector2(x, 600), Vector2(x, 300))
	var refl := ReflectionEffect.new(seg.get_carrier())
	var left := SideConfig.new(refl, true)
	var right := SideConfig.new(null, false)
	return Surface.new(seg, left, right, false, false)

# ============================================================
# Phase 1: Step enrichment
# ============================================================

func test_step_defaults() -> void:
	var step := TraceStep.make()
	assert_eq(step.surface_id, -1, "Default surface_id should be -1")
	assert_eq(step.hit_side, -1, "Default hit_side should be -1")
	assert_eq(step.hit_on_segment, false, "Default hit_on_segment should be false")

func test_physical_trace_step_has_surface_id() -> void:
	var m := _mirror(400)
	var player := Vector2(200, 300)
	var aim := Direction.from_coords(player, Vector2(600, 300))
	var path := TestTrace.trace(player, aim, [m], GameState.new())
	var hit_step: TraceStep = null
	for step in path.steps:
		if step.hit and step.surface_id >= 0:
			hit_step = step
			break
	assert_not_null(hit_step, "Should have a step that hit the mirror")
	assert_eq(hit_step.surface_id, m.id, "Step surface_id should match the mirror")

func test_physical_trace_step_has_hit_side() -> void:
	var m := _mirror(400)
	var player := Vector2(200, 300)
	var aim := Direction.from_coords(player, Vector2(600, 300))
	var path := TestTrace.trace(player, aim, [m], GameState.new())
	var hit_step: TraceStep = null
	for step in path.steps:
		if step.surface_id == m.id:
			hit_step = step
			break
	assert_not_null(hit_step, "Should find step hitting mirror")
	assert_true(hit_step.hit_side == Side.Value.LEFT or hit_step.hit_side == Side.Value.RIGHT,
		"hit_side should be a valid Side value")

func test_physical_trace_step_has_on_segment() -> void:
	var m := _mirror(400)
	var player := Vector2(200, 300)
	var aim := Direction.from_coords(player, Vector2(600, 300))
	var path := TestTrace.trace(player, aim, [m], GameState.new())
	var hit_step: TraceStep = null
	for step in path.steps:
		if step.surface_id == m.id:
			hit_step = step
			break
	assert_not_null(hit_step, "Should find step hitting mirror")
	assert_true(hit_step.hit_on_segment, "Hit within segment bounds should be on_segment")

func test_escape_step_has_no_surface_id() -> void:
	var player := Vector2(200, 300)
	var aim := Direction.from_coords(player, Vector2(600, 300))
	var path := TestTrace.trace(player, aim, [], GameState.new())
	for step in path.steps:
		assert_eq(step.surface_id, -1, "Escape steps should have no surface_id")

func test_with_type_preserves_surface_fields() -> void:
	var step := TraceStep.make()
	step.surface_id = 42
	step.hit_side = Side.Value.RIGHT
	step.hit_on_segment = true
	var copy := step.with_type(StepTypes.Type.DIVERGED_PHYSICAL)
	assert_eq(copy.surface_id, 42, "with_type should preserve surface_id")
	assert_eq(copy.hit_side, Side.Value.RIGHT, "with_type should preserve hit_side")
	assert_eq(copy.hit_on_segment, true, "with_type should preserve hit_on_segment")

# ============================================================
# Phase 2: Physical hits dictionary
# ============================================================

func test_physical_hits_contains_hit_surface() -> void:
	var path := TracePath.new()
	var step := TraceStep.make()
	step.surface_id = 5
	step.hit_side = Side.Value.LEFT
	step.hit_on_segment = true
	path.steps.append(step)
	var hits := PathRendererScript.build_physical_hits(path)
	assert_true(hits.has(5), "Dict should contain surface_id 5")

func test_physical_hits_records_side_and_on_segment() -> void:
	var path := TracePath.new()
	var step := TraceStep.make()
	step.surface_id = 5
	step.hit_side = Side.Value.LEFT
	step.hit_on_segment = true
	path.steps.append(step)
	var hits := PathRendererScript.build_physical_hits(path)
	var entries: Array = hits[5]
	assert_eq(entries.size(), 1)
	assert_eq(entries[0].side, Side.Value.LEFT, "Should record correct side")
	assert_eq(entries[0].on_segment, true, "Should record on_segment")

func test_physical_hits_empty_for_no_hits() -> void:
	var path := TracePath.new()
	var step := TraceStep.make()
	path.steps.append(step)
	var hits := PathRendererScript.build_physical_hits(path)
	assert_eq(hits.size(), 0, "No enriched steps → empty dict")

func test_physical_hits_skips_negative_surface_id() -> void:
	var path := TracePath.new()
	var s1 := TraceStep.make()
	var s2 := TraceStep.make()
	s2.surface_id = 3
	s2.hit_side = Side.Value.RIGHT
	s2.hit_on_segment = true
	path.steps.append(s1)
	path.steps.append(s2)
	var hits := PathRendererScript.build_physical_hits(path)
	assert_false(hits.has(-1), "Should not contain surface_id -1")
	assert_true(hits.has(3), "Should contain surface_id 3")

# ============================================================
# Phase 3: Plan validity via ChevronOverlayBuilder
# ============================================================

func _lookup(surfaces: Array) -> Callable:
	return func(id: int) -> Surface:
		for s in surfaces:
			if s.id == id:
				return s
		return null

func test_plan_overlay_created_for_planned_surface() -> void:
	var m := _mirror(400)
	var plan := PlanManager.new()
	plan.add_entry(m.id, Side.Value.LEFT)
	var phits := {m.id: [{"side": Side.Value.LEFT, "on_segment": true, "has_continuation": true}]}
	var overlays := ChevronOverlayBuilder.build_plan_overlays(plan, phits, _lookup([m]))
	assert_true(overlays.has(m.id), "Should create overlay for planned surface")
	var ovs: Array = overlays[m.id]
	assert_eq(ovs.size(), 1, "Should have 1 overlay")
	assert_eq(ovs[0].side, Side.Value.LEFT, "Overlay should be on LEFT side")

func test_plan_valid_when_hit_correct_side_on_segment() -> void:
	var m := _mirror(400)
	var plan := PlanManager.new()
	plan.add_entry(m.id, Side.Value.LEFT)
	var phits := {m.id: [{"side": Side.Value.LEFT, "on_segment": true, "has_continuation": true}]}
	var overlays := ChevronOverlayBuilder.build_plan_overlays(plan, phits, _lookup([m]))
	var ov: ChevronOverlay = overlays[m.id][0]
	assert_eq(ov.incoming_color, ChevronOverlayBuilder.PLAN_VALID_COLOR,
		"Should be valid when hit correct side on-segment")

func test_plan_invalid_when_surface_not_hit() -> void:
	var m := _mirror(400)
	var plan := PlanManager.new()
	plan.add_entry(m.id, Side.Value.LEFT)
	var overlays := ChevronOverlayBuilder.build_plan_overlays(plan, {}, _lookup([m]))
	var ov: ChevronOverlay = overlays[m.id][0]
	assert_eq(ov.incoming_color, ChevronOverlayBuilder.PLAN_INVALID_COLOR,
		"Should be invalid when surface not hit at all")

func test_plan_invalid_when_wrong_side() -> void:
	var m := _mirror(400)
	var plan := PlanManager.new()
	plan.add_entry(m.id, Side.Value.LEFT)
	var phits := {m.id: [{"side": Side.Value.RIGHT, "on_segment": true, "has_continuation": true}]}
	var overlays := ChevronOverlayBuilder.build_plan_overlays(plan, phits, _lookup([m]))
	var ov: ChevronOverlay = overlays[m.id][0]
	assert_eq(ov.incoming_color, ChevronOverlayBuilder.PLAN_INVALID_COLOR,
		"Should be invalid when hit wrong side")

func test_plan_invalid_when_off_segment() -> void:
	var m := _mirror(400)
	var plan := PlanManager.new()
	plan.add_entry(m.id, Side.Value.LEFT)
	var phits := {m.id: [{"side": Side.Value.LEFT, "on_segment": false, "has_continuation": true}]}
	var overlays := ChevronOverlayBuilder.build_plan_overlays(plan, phits, _lookup([m]))
	var ov: ChevronOverlay = overlays[m.id][0]
	assert_eq(ov.incoming_color, ChevronOverlayBuilder.PLAN_INVALID_COLOR,
		"Should be invalid when off-segment")

func test_plan_valid_if_any_hit_matches() -> void:
	var m := _mirror(400)
	var plan := PlanManager.new()
	plan.add_entry(m.id, Side.Value.LEFT)
	var phits := {m.id: [
		{"side": Side.Value.RIGHT, "on_segment": true, "has_continuation": true},
		{"side": Side.Value.LEFT, "on_segment": true, "has_continuation": true},
	]}
	var overlays := ChevronOverlayBuilder.build_plan_overlays(plan, phits, _lookup([m]))
	var ov: ChevronOverlay = overlays[m.id][0]
	assert_eq(ov.incoming_color, ChevronOverlayBuilder.PLAN_VALID_COLOR,
		"Should be valid if any hit matches")

func test_plan_highlight_suppressed_on_hover_side() -> void:
	var m := _mirror(400)
	var node := SurfaceNodeScript.new()
	node.setup(m)
	add_child_autofree(node)
	var plan_ov := ChevronOverlay.plan(Side.Value.LEFT, true, Side.Value.LEFT,
		ChevronOverlayBuilder.PLAN_VALID_COLOR, ChevronOverlayBuilder.PLAN_VALID_COLOR,
		ChevronOverlayBuilder.PLAN_VALID_COLOR)
	node.set_plan_overlays([plan_ov], [1])
	var hover_ov := ChevronOverlay.hover(Side.Value.LEFT, true, Side.Value.LEFT,
		ChevronOverlayBuilder.HOVER_COLOR)
	node.set_hover_overlays([hover_ov])
	assert_eq(node._plan_overlays.size(), 1, "Plan overlay stored")
	assert_eq(node._hover_overlays.size(), 1, "Hover overlay stored")
	assert_eq(node._hover_overlays[0].side, node._plan_overlays[0].side,
		"Plan suppressed when hover covers same side")

func test_plan_highlight_color_selection() -> void:
	var m := _mirror(400)
	var plan := PlanManager.new()
	plan.add_entry(m.id, Side.Value.LEFT)
	plan.add_entry(m.id, Side.Value.RIGHT)
	var phits := {m.id: [{"side": Side.Value.LEFT, "on_segment": true, "has_continuation": true}]}
	var overlays := ChevronOverlayBuilder.build_plan_overlays(plan, phits, _lookup([m]))
	var ovs: Array = overlays[m.id]
	assert_eq(ovs[0].incoming_color, ChevronOverlayBuilder.PLAN_VALID_COLOR,
		"Valid entry should use PLAN_VALID_COLOR")
	assert_eq(ovs[1].incoming_color, ChevronOverlayBuilder.PLAN_INVALID_COLOR,
		"Invalid entry should use PLAN_INVALID_COLOR")
