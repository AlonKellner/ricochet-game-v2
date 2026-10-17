extends GutTest

# Stage 110: the visibility region (GAME_SPEC 15) through the binding and the
# renderer. The region is the non-divergence set: a cursor inside it previews
# aligned up to itself; a cursor outside does not. The sweep pins this over the
# levels (test_visibility_sweep.gd, native test_visibility.cpp); here, small
# hand-checked scenes and the renderer's recompute-on-change.

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

func test_empty_room_is_all_visible():
	var region := _region(_room(), Vector2(200, 300))
	assert_true(region.is_supported())
	assert_gt(region.get_cell_count(), 0)
	assert_eq(region.contains(Vector2(800, 100)), 1, "a far corner of the empty room")
	assert_eq(region.contains(Vector2(200, 300)), -1, "the player itself is not classified")
	assert_eq(region.contains(Vector2(5000, 5000)), -1, "outside the level bounds: not classified")

func test_a_block_casts_a_shadow():
	var surfaces := [H.wall_between(Vector2(500, 200), Vector2(500, 400))] + _room()
	var region := _region(surfaces, Vector2(200, 300))
	assert_eq(region.contains(Vector2(400, 300)), 1, "in front of the block")
	assert_eq(region.contains(Vector2(700, 300)), 0, "behind the block")
	assert_eq(region.contains(Vector2(500, 300)), 1, "on the block: the cursor is reached first")
	_assert_pinned(surfaces, Vector2(200, 300), [], "block")

func test_a_planned_mirror_narrows_the_region():
	var mirror := H.mirror(600, 150, 450)
	var surfaces := [mirror] + _room()
	var plan := [PlanManager.PlanEntry.new(mirror.id, _side_toward(surfaces, Vector2(200, 300), Vector2(700, 300), mirror.id))]
	var region := _region(surfaces, Vector2(200, 300), plan)
	assert_eq(region.contains(Vector2(300, 320)), 1, "back toward the player, through the mirror")
	assert_eq(region.contains(Vector2(800, 300)), 0, "behind the mirror")
	_assert_pinned(surfaces, Vector2(200, 300), plan, "mirror")

func test_a_terminal_in_the_plan_keeps_only_off_segment_aims():
	var block := H.wall_between(Vector2(500, 250), Vector2(500, 350))
	var surfaces := [block] + _room()
	var plan := [PlanManager.PlanEntry.new(block.id, Side.Value.LEFT)]
	_assert_pinned(surfaces, Vector2(200, 300), plan, "terminal entry")

func test_the_renderer_recomputes_only_on_change():
	Surface.reset_id_counter()
	var scene: Node = load("res://scenes/test_levels/room_with_mirror.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	var vis: VisibilityRenderer = scene.get_node("VisibilityRenderer")
	assert_not_null(vis, "the level adds the renderer")
	vis.async_compute = false
	assert_eq(vis.z_index, 10, "between the surfaces and the preview")
	scene.get_node("Cursor").set_process(false)
	scene.get_node("Player").set_physics_process(false)
	await get_tree().process_frame
	await get_tree().process_frame
	var v0 := vis.visibility_version
	assert_true(vis.region != null and vis.region.is_supported(), "a region")
	await get_tree().process_frame
	assert_eq(vis.visibility_version, v0, "a still frame recomputes nothing")
	scene.get_node("Cursor").global_position += Vector2(37, 11)
	await get_tree().process_frame
	assert_eq(vis.visibility_version, v0, "the cursor does not move the region")
	scene.get_node("Player").global_position += Vector2(20, 0)
	await get_tree().process_frame
	assert_eq(vis.visibility_version, v0 + 1, "the player does")
	FeatureFlags.visibility_override = false
	await get_tree().process_frame
	assert_eq(vis.visibility_version, v0 + 2, "the switch does")
	assert_null(vis.region, "switched off: nothing drawn")

func test_the_renderer_computes_on_a_worker_thread():
	Surface.reset_id_counter()
	var scene: Node = load("res://scenes/test_levels/room_with_mirror.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	var vis: VisibilityRenderer = scene.get_node("VisibilityRenderer")
	scene.get_node("Cursor").set_process(false)
	scene.get_node("Player").set_physics_process(false)
	for i in 120:
		await get_tree().process_frame
		if vis.region != null:
			break
	assert_true(vis.region != null and vis.region.is_supported(), "the worker's region arrives")
	var sync := VisibilityRenderer.new()
	assert_true(vis.async_compute, "the game computes asynchronously")
	sync.free()
	# the same region as computed in the frame
	var cells: int = vis.region.get_cell_count()
	vis.async_compute = false
	vis.recompute()
	assert_eq(vis.region.get_cell_count(), cells, "the worker computed the same region")
