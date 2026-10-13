extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

func _build_merged(player: Vector2, cursor: Vector2, surfaces: Array, plan_entries: Array = []) -> Array:
	var pair := TestTrace.pair_of(player, cursor, surfaces, plan_entries)
	if pair == null:
		return []
	var ci: int = pair.planned.cursor_index
	if ci < 0:
		ci = pair.planned.steps.size()
	return StepTreeMerge.merge(pair.planned.steps, pair.physical.steps, ci)

# --- Empty plan, mirror AFTER cursor → all green ---

func test_empty_plan_mirror_after_cursor() -> void:
	var m := H.mirror(400)
	var w := H.wall(100)
	var merged := _build_merged(Vector2(600, 300), Vector2(500, 300), [m, w])
	for i in merged.size():
		var ms: TraceStep = merged[i]
		assert_true(
			ms.type == StepTypes.Type.ALIGNED or ms.type == StepTypes.Type.ALIGNED_POST_PLANNED,
			"Step %d should be green (type=%d)" % [i, ms.type])

# --- Empty plan, mirror BEFORE cursor → divergence ---

func test_empty_plan_mirror_before_cursor() -> void:
	var m := H.mirror(400)
	var w := H.wall(700)
	var merged := _build_merged(Vector2(200, 300), Vector2(600, 300), [m, w])
	var has_diverged := false
	for i in merged.size():
		var ms: TraceStep = merged[i]
		if ms.type == StepTypes.Type.DIVERGED_PLANNED or ms.type == StepTypes.Type.DIVERGED_PHYSICAL:
			has_diverged = true
	assert_true(has_diverged, "Mirror before cursor should cause divergence")

# --- Plan matches physics → all green ---

func test_plan_matches_physics_first_step_aligned() -> void:
	var m := H.mirror(400)
	var w := H.wall(100)
	var plan: Array = [PlanManager.PlanEntry.new(m.id, Side.Value.LEFT)]
	var merged := _build_merged(Vector2(600, 300), Vector2(200, 300), [m, w], plan)
	var first: TraceStep = merged[0]
	assert_true(
		first.type == StepTypes.Type.ALIGNED or first.type == StepTypes.Type.ALIGNED_POST_PLANNED,
		"First step should be green when plan matches")

# --- Plan misses physics → divergence ---

func test_plan_misses_physics() -> void:
	var m := H.mirror(400)
	var w := H.wall(700)
	# Mirror between player and cursor, no plan → physical reflects, planned doesn't
	var merged := _build_merged(Vector2(200, 300), Vector2(600, 300), [m, w])
	var has_div_physical := false
	var has_div_planned := false
	for i in merged.size():
		var ms: TraceStep = merged[i]
		if ms.type == StepTypes.Type.DIVERGED_PHYSICAL:
			has_div_physical = true
		if ms.type == StepTypes.Type.DIVERGED_PLANNED:
			has_div_planned = true
	assert_true(has_div_physical, "Should have DIVERGED_PHYSICAL")
	assert_true(has_div_planned, "Should have DIVERGED_PLANNED")

# --- Wall blocks physical, planned continues → divergence at wall ---

func test_wall_blocks_physical_planned_continues() -> void:
	var w := H.wall(400)
	var merged := _build_merged(Vector2(300, 300), Vector2(500, 300), [w])
	var has_aligned := false
	var has_div_planned := false
	for i in merged.size():
		var ms: TraceStep = merged[i]
		if ms.type == StepTypes.Type.ALIGNED:
			has_aligned = true
		if ms.type == StepTypes.Type.DIVERGED_PLANNED:
			has_div_planned = true
	assert_true(has_aligned, "Should have ALIGNED before wall")
	assert_true(has_div_planned, "Planned continues through wall → DIVERGED_PLANNED to cursor")

# --- Physical reflects, never reaches cursor ---

func test_physical_diverges_before_cursor() -> void:
	# Mirror between player and cursor. Physical reflects away. Planned passes through.
	# Physical may never reach cursor.
	var m := H.mirror(400)
	var w_right := H.wall(700)
	var w_left := H.wall(100)
	var merged := _build_merged(Vector2(300, 300), Vector2(600, 300), [m, w_right, w_left])
	# Should have ALIGNED up to mirror, then DIVERGED
	var first: TraceStep = merged[0]
	assert_true(
		first.type == StepTypes.Type.ALIGNED or first.type == StepTypes.Type.ALIGNED_POST_PLANNED,
		"First step should be green")
	var has_div := false
	for i in merged.size():
		var ms: TraceStep = merged[i]
		if ms.type == StepTypes.Type.DIVERGED_PHYSICAL or ms.type == StepTypes.Type.DIVERGED_PLANNED:
			has_div = true
	assert_true(has_div, "Should have divergence")

# --- User bug repro ---

func test_user_bug_two_mirrors_empty_plan() -> void:
	var w_top := RoomBuilder.create_block_surface(Vector2(560, 240), Vector2(1360, 240), Vector2(960, 240))
	var w_bot := RoomBuilder.create_block_surface(Vector2(1360, 840), Vector2(560, 840), Vector2(960, 840))
	var w_left := RoomBuilder.create_block_surface(Vector2(560, 840), Vector2(560, 240), Vector2(560, 540))

	var m1_seg := Segment.from_coords(Vector2(800, 300), Vector2(800, 780), Vector2(800, 540))
	var m1_refl := ReflectionEffect.new(m1_seg.get_carrier())
	var m1_left := SideConfig.new(m1_refl, true)
	var m1_right := SideConfig.new(null, false)
	var mirror1 := Surface.new(m1_seg, m1_left, m1_right, false, false)

	var m2_seg := Segment.from_coords(Vector2(1200, 300), Vector2(1200, 780), Vector2(1200, 540))
	var m2_refl := ReflectionEffect.new(m2_seg.get_carrier())
	var m2_left := SideConfig.new(m2_refl, true)
	var m2_right := SideConfig.new(null, false)
	var mirror2 := Surface.new(m2_seg, m2_left, m2_right, false, false)

	var surfaces: Array = [w_top, w_bot, w_left, mirror1, mirror2]
	var merged := _build_merged(Vector2(1040.0, 827.9904), Vector2(914.9531, 708.3118), surfaces)

	for i in merged.size():
		var ms: TraceStep = merged[i]
		assert_true(
			ms.type == StepTypes.Type.ALIGNED or ms.type == StepTypes.Type.ALIGNED_POST_PLANNED,
			"Step %d should be aligned (type=%d) — empty plan, no obstacle before cursor" % [i, ms.type])

# --- Green from player ---

func test_first_step_green() -> void:
	var m := H.mirror(400)
	var w := H.wall(700)
	var merged := _build_merged(Vector2(200, 300), Vector2(600, 300), [m, w])
	assert_gt(merged.size(), 0, "Should have steps")
	var first: TraceStep = merged[0]
	assert_true(
		first.type == StepTypes.Type.ALIGNED or first.type == StepTypes.Type.ALIGNED_POST_PLANNED,
		"First step should be green (type=%d)" % first.type)
