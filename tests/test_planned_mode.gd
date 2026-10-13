extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

func _step(path: TracePath, idx: int) -> TraceStep:
	return path.steps[idx]

# --- PLANNED mode with empty plan ---

func test_planned_empty_plan_no_effects() -> void:
	var m := H.mirror(400)
	var w := H.wall(700)
	var player := Vector2(200, 300)
	var cursor := Vector2(300, 300)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, [m, w], GameState.new(),
		ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, [])
	var first_fid: int = _step(path, 0).frame_id
	for i in range(0, mini(path.cursor_index, path.steps.size())):
		assert_eq(_step(path, i).frame_id, first_fid,
			"Pre-cursor step %d should have no effect (empty plan)" % i)

func test_planned_empty_plan_physical_after_cursor() -> void:
	var m := H.mirror(400)
	var w := H.wall(100)
	var player := Vector2(600, 300)
	var cursor := Vector2(500, 300)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, [m, w], GameState.new(),
		ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, [])
	var found_frame_change := false
	var cursor_fid: int = _step(path, 0).frame_id
	for i in range(path.cursor_index, path.steps.size()):
		if _step(path, i).frame_id != cursor_fid:
			found_frame_change = true
			break
	assert_true(found_frame_change, "Post-cursor PHYSICAL mode should reflect off mirror")

# --- PLANNED mode with plan entries ---

func test_planned_mirror_in_plan() -> void:
	var m := H.mirror(400)
	var w := H.wall(100)
	var player := Vector2(600, 300)
	var cursor := Vector2(200, 300)
	var plan: Array = [PlanManager.PlanEntry.new(m.id, Side.Value.LEFT)]
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, [m, w], GameState.new(),
		ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, plan, null, cursor)
	var found_frame_change := false
	var first_fid: int = _step(path, 0).frame_id
	for i in range(1, path.steps.size()):
		if _step(path, i).frame_id != first_fid:
			found_frame_change = true
			break
	assert_true(found_frame_change, "Mirror in plan should cause frame change")

func test_planned_mirror_not_in_plan_passthrough() -> void:
	var m := H.mirror(400)
	var w := H.wall(700)
	var player := Vector2(200, 300)
	var cursor := Vector2(600, 300)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, [m, w], GameState.new(),
		ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, [])
	var first_fid: int = _step(path, 0).frame_id
	for i in path.steps.size():
		assert_eq(_step(path, i).frame_id, first_fid,
			"Mirror not in plan should be pass-through at step %d" % i)

# --- Both modes same hitpoints pre-divergence ---

func test_hitpoint_alignment_no_effects() -> void:
	var w := H.wall(600)
	var player := Vector2(200, 300)
	var cursor := Vector2(400, 300)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var physical := TestTrace.trace(player, aim, [w], GameState.new(), ray,
		-1.0, TraceMode.PHYSICAL, TraceMode.PHYSICAL, [], null)
	var planned := TestTrace.trace(player, aim, [w], GameState.new(),
		ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, [], null)
	assert_eq(physical.steps.size(), planned.steps.size(), "Same step count")
	for i in physical.steps.size():
		assert_eq(_step(physical, i).start, _step(planned, i).start, "Same start at %d" % i)
		assert_eq(_step(physical, i).end, _step(planned, i).end, "Same end at %d" % i)

# --- Plan entries consumed in order ---

func test_plan_entries_order() -> void:
	var m1 := H.mirror(300)
	var m2 := H.mirror(600)
	var plan: Array = [
		PlanManager.PlanEntry.new(m1.id, Side.Value.LEFT),
		PlanManager.PlanEntry.new(m2.id, Side.Value.RIGHT),
	]
	var player := Vector2(450, 300)
	var cursor := Vector2(200, 300)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var path := TestTrace.trace(player, aim, [m1, m2], GameState.new(),
		ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, plan, null, cursor)
	var frame_changes := 0
	for i in range(1, path.steps.size()):
		if _step(path, i).frame_id != _step(path, i - 1).frame_id:
			frame_changes += 1
	assert_gte(frame_changes, 2, "Two plan entries → at least 2 frame changes")

# --- Terminal stops physical not planned ---

func test_terminal_stops_physical_not_planned() -> void:
	var w := H.wall(400)
	var player := Vector2(200, 300)
	var cursor := Vector2(600, 300)
	var aim := Direction.from_coords(player, cursor)
	var ray := Ray.from_coords(player, aim)
	var physical := TestTrace.trace(player, aim, [w], GameState.new(), ray)
	var planned := TestTrace.trace(player, aim, [w], GameState.new(),
		ray, -1.0,
		TraceMode.PLANNED, TraceMode.PHYSICAL, [])
	assert_gt(planned.steps.size(), physical.steps.size(), "Planned passes through wall, physical stops")
