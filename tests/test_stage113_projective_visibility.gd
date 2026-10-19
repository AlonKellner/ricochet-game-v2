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

# B9 follow-up (GAME_SPEC 23): a line directional whose normal runs along its
# carrier projects nowhere; a level with one is rejected at load.
func test_b9_a_directional_normal_along_its_line_is_rejected_at_load():
	var along := SideSpec.make({"type": "line_directional_projection", "normal": [0, 1]}, true, [])
	var across := SideSpec.make({"type": "line_directional_projection", "normal": [1, 0]}, true, [])
	var bad := SurfaceSpec.line(Vector2(750, 300), Vector2(750, 780), along, along)
	assert_false(bad.validate().is_empty(), "the normal along the line: rejected")
	assert_true(SurfaceSpec.line(Vector2(750, 300), Vector2(750, 780), across, across).validate().is_empty(), "across it: fine")
	var slanted := SideSpec.make({"type": "line_directional_projection", "normal": [1, 1]}, true, [])
	assert_true(SurfaceSpec.line(Vector2(750, 300), Vector2(750, 780), slanted, slanted).validate().is_empty(), "slanted: fine")
	var circ := SideSpec.make({"type": "circle_directional_projection", "normal": [0, 1]}, true, [])
	assert_true(SurfaceSpec.arc(Vector2(850, 390), Vector2(850, 690), Vector2(1000, 540), circ, circ).validate().is_empty(),
		"a circle has no direction to run along")

# B7 follow-up (GAME_SPEC 4.2): a click on the diameter across a circle
# directional's normal is moved off it by the UI tolerance, to the via's side.
func test_b7_a_click_on_the_diameter_across_the_normal_is_moved_off_it():
	var gs := GameState.new()
	# the right half of the circle centred (600, 300) r 100, n = (0, 1): the diameter across n is y = 300
	var seg := Segment.from_coords(Vector2(600, 200), Vector2(600, 400), Vector2(700, 300))
	var cfg := SideConfig.new(CircleDirectionalProjection.new(Vector2(0, 1)), true)
	var arc := Surface.new(seg, cfg, cfg, false, false)
	assert_eq(PlanManager.snap_click(arc, Side.Value.LEFT, Vector2(690, 330), gs), Vector2(690, 330), "off the diameter: unchanged")
	assert_eq(PlanManager.snap_click(arc, Side.Value.LEFT, Vector2(690, 300), gs), Vector2(690, 308), "on it: moved to +n (the via is on it)")
	assert_eq(PlanManager.snap_click(arc, Side.Value.LEFT, Vector2(690, 297), gs), Vector2(690, 308), "within the tolerance: the same")
	# n = (1, 0): the diameter is x = 600, the via (700, 300) on its +n side
	var cfg2 := SideConfig.new(CircleDirectionalProjection.new(Vector2(1, 0)), true)
	var arc2 := Surface.new(Segment.from_coords(Vector2(600, 200), Vector2(600, 400), Vector2(700, 300)), cfg2, cfg2, false, false)
	assert_eq(PlanManager.snap_click(arc2, Side.Value.LEFT, Vector2(600, 350), gs), Vector2(608, 350), "to the via's side")
	# a line normal keeps its click, and so does a circle normal
	assert_eq(PlanManager.snap_click(_line_normal(600, 150, 450), Side.Value.LEFT, Vector2(600, 300), gs), Vector2(600, 300))
	var cn := _circle_normal(Vector2(600, 200), Vector2(600, 400), Vector2(700, 300))
	assert_eq(PlanManager.snap_click(cn, Side.Value.LEFT, Vector2(600, 300), gs), Vector2(600, 300))

# An inversion centred on a circle normal's centre after it sends the centre to
# infinity: the pencil's poles swap and the region still builds.
func test_an_inversion_concentric_with_the_circle_normal_builds_a_region():
	Surface.reset_id_counter()
	var scene: Node = load("res://scenes/playground/projective_concentric.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	await get_tree().process_frame
	var player := Vector2(1100, 700)
	var plan := [PlanManager.PlanEntry.new(7, Side.Value.LEFT), PlanManager.PlanEntry.new(6, Side.Value.RIGHT)]
	var region := _region(scene.surfaces, player, plan)
	assert_true(region.is_supported(), "supported")
	assert_gt(region.get_cell_count(), 0, "cells")
	assert_eq(region.check(), [], "the structure checks hold")
	for i in 9:
		for j in 7:
			var c := Vector2(600 + i * 90.0, 270 + j * 90.0)
			var m: int = region.contains(c)
			if m >= 0:
				assert_eq(m == 1, _preview_visible(scene.surfaces, player, c, plan), "cursor %s" % c)
