extends GutTest

# Stage 113: the visibility region through projective plan entries (GAME_SPEC
# 15.2) through the binding and the renderer. The sweep pins the projective
# playgrounds (test_visibility_sweep.gd, native test_visibility.cpp); here,
# small hand-checked rooms, the fork of a circle normal by the click, and the
# renderer's recompute when a click changes.

const H := preload("res://tests/test_helpers.gd")
const BOUNDS := Rect2(0, 0, 1000, 600)

func before_each() -> void:
	H.reset_counters()
	FeatureFlags.visibility_override = null

func after_each() -> void:
	FeatureFlags.visibility_override = null

func _room() -> Array:
	return RoomBuilder.create_room_surfaces(BOUNDS)

func _region(surfaces: Array, player: Vector2, plan: Array = []) -> VisibilityRegion:
	return TestTrace.fresh_tracer().visibility(SceneDefs.defs_from_surfaces(surfaces), player, plan, {}, BOUNDS)

func _line_normal(x: float, y0: float, y1: float, back: bool = false) -> Surface:
	var seg := Segment.from_coords(Vector2(x, y0), Vector2(x, y1), Vector2(x, (y0 + y1) / 2.0))
	var cfg := SideConfig.new(LineNormalProjection.new(back), true)
	return Surface.new(seg, cfg, cfg, false, false)

func _circle_normal(start: Vector2, end_v: Vector2, via: Vector2, back: bool = false) -> Surface:
	var seg := Segment.from_coords(start, end_v, via)
	var cfg := SideConfig.new(CircleNormalProjection.new(back), true)
	return Surface.new(seg, cfg, cfg, false, false)

# The preview at the cursor: the merged steps before it ALIGNED, the cursor reached.
func _preview_visible(surfaces: Array, player: Vector2, cursor: Vector2, plan: Array = []) -> bool:
	var pair := TestTrace.trace_pair(surfaces, player, cursor, plan)
	if pair == null:
		return false
	var ci := pair.planned.cursor_index
	if ci < 0 or not pair.cursor_point_agrees:
		return false
	var merged := StepTreeMerge.merge(pair.planned.steps, pair.physical.steps, ci)
	for i in ci:
		if merged[i].type != StepTypes.Type.ALIGNED:
			return false
	return true

func _side_toward(surfaces: Array, player: Vector2, cursor: Vector2, sid: int) -> Side.Value:
	var pair := TestTrace.trace_pair(surfaces, player, cursor)
	for st in pair.physical.steps:
		if st.hit != null and st.surface_id == sid:
			return st.hit_side
	return Side.Value.LEFT

func _assert_pinned(surfaces: Array, player: Vector2, plan: Array, label: String) -> void:
	var region := _region(surfaces, player, plan)
	assert_true(region.is_supported(), label + ": supported")
	assert_eq(region.check(), [], label + ": the structure checks hold")
	for i in 11:
		for j in 7:
			var c := Vector2(40 + i * 92.0, 30 + j * 90.0)
			var m: int = region.contains(c)
			if m < 0:
				continue
			assert_eq(m == 1, _preview_visible(surfaces, player, c, plan), "%s: cursor %s" % [label, c])

func test_a_line_normal_casts_a_band_of_parallel_rays():
	var line := _line_normal(600, 150, 450)
	var surfaces := [line] + _room()
	var player := Vector2(200, 300)
	var plan := [PlanManager.PlanEntry.new(line.id, _side_toward(surfaces, player, Vector2(700, 300), line.id))]
	var region := _region(surfaces, player, plan)
	assert_true(region.is_supported(), "a projective plan builds a region")
	assert_eq(region.contains(Vector2(800, 300)), 1, "beyond the line, inside the band of its rays")
	assert_eq(region.contains(Vector2(800, 100)), 0, "beyond the line, outside the band")
	assert_eq(region.contains(Vector2(400, 300)), 0, "before the line: behind its exit, unreached")
	_assert_pinned(surfaces, player, plan, "line normal")

func test_a_circle_normal_fork_follows_the_click():
	# the right half of the circle centred (600, 300), radius 100: through, from
	# outside: the rays converge through the centre
	var arc := _circle_normal(Vector2(600, 200), Vector2(600, 400), Vector2(700, 300))
	var surfaces := [arc] + _room()
	var player := Vector2(900, 500)
	var lower := Vector2(655, 383) # a point of the arc the player sees directly
	var side := _side_toward(surfaces, player, lower, arc.id)
	# the cursor's two candidates: the lower right (on the arc) and the upper left (off it)
	var cursor := Vector2(620, 330)
	var near_lower := PlanManager.PlanEntry.new(arc.id, side)
	near_lower.click = Vector2(650, 370)
	var near_upper := PlanManager.PlanEntry.new(arc.id, side)
	near_upper.click = Vector2(650, 230)
	var r_lower := _region(surfaces, player, [near_lower])
	var r_upper := _region(surfaces, player, [near_upper])
	assert_eq(r_lower.contains(cursor), 1, "the click near the lower candidate: the cursor on its converging ray")
	assert_eq(r_upper.contains(cursor), 0, "the click near the upper candidate (off the arc): the plan never reaches it")
	assert_eq(_preview_visible(surfaces, player, cursor, [near_lower]), true, "the preview agrees (lower)")
	assert_eq(_preview_visible(surfaces, player, cursor, [near_upper]), false, "the preview agrees (upper)")
	_assert_pinned(surfaces, player, [near_lower], "circle normal, click lower")
	_assert_pinned(surfaces, player, [near_upper], "circle normal, click upper")

func test_a_directional_normal_along_its_line_is_an_empty_region():
	var seg := Segment.from_coords(Vector2(600, 150), Vector2(600, 450), Vector2(600, 300))
	var cfg := SideConfig.new(LineDirectionalProjection.new(Vector2(0, 1)), true)
	var line := Surface.new(seg, cfg, cfg, false, false)
	var surfaces := [line] + _room()
	var plan := [PlanManager.PlanEntry.new(line.id, Side.Value.LEFT)]
	var region := _region(surfaces, Vector2(200, 300), plan)
	assert_true(region.is_supported(), "supported")
	assert_eq(region.get_cell_count(), 0, "no exit family: nothing visible")
	assert_eq(region.contains(Vector2(800, 300)), 0)

func test_the_renderer_recomputes_when_a_click_changes():
	Surface.reset_id_counter()
	var scene: Node = load("res://scenes/playground/projective_arcs.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	var vis: VisibilityRenderer = scene.get_node("VisibilityRenderer")
	assert_not_null(vis, "the level adds the renderer")
	vis.async_compute = false
	scene.get_node("Cursor").set_process(false)
	scene.get_node("Player").set_physics_process(false)
	var plan: PlanManager = scene.get_node("PathRenderer")._get_plan()
	plan.add_entry(5, Side.Value.LEFT, Vector2(880, 395))
	await get_tree().process_frame
	await get_tree().process_frame
	var v0 := vis.visibility_version
	assert_true(vis.region != null and vis.region.is_supported(), "a region for the projective plan")
	await get_tree().process_frame
	assert_eq(vis.visibility_version, v0, "a still frame recomputes nothing")
	plan.entries[0].click = Vector2(880, 685)
	await get_tree().process_frame
	assert_eq(vis.visibility_version, v0 + 1, "the click moves the region")
