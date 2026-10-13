extends GutTest

const H := preload("res://tests/test_helpers.gd")

var _scene: Node

func before_each() -> void:
	H.reset_counters()
	_scene = null

func _build_room_with_wall() -> Dictionary:
	var room := RoomBuilder.create_room_surfaces(Rect2(0, 0, 1000, 600))
	var wall_seg := Segment.from_coords(Vector2(500, 0), Vector2(500, 600), Vector2(500, 300))
	var terminal := TerminalEffect.new()
	var wall := Surface.new(wall_seg, SideConfig.new(terminal), SideConfig.new(terminal))
	var surfaces: Array = []
	surfaces.append_array(room)
	surfaces.append(wall)
	return {"surfaces": surfaces, "wall": wall}

func _build_room_with_mirror() -> Dictionary:
	var room := RoomBuilder.create_room_surfaces(Rect2(0, 0, 1000, 600))
	var mirror_seg := Segment.from_coords(Vector2(500, 100), Vector2(500, 500), Vector2(500, 300))
	var refl := ReflectionEffect.new(mirror_seg.get_carrier())
	var mirror := Surface.new(mirror_seg, SideConfig.new(null, false), SideConfig.new(refl, true), false, false)
	var surfaces: Array = []
	surfaces.append_array(room)
	surfaces.append(mirror)
	return {"surfaces": surfaces, "mirror": mirror}

func test_physical_blocked_when_cursor_on_wall() -> void:
	var scene := _build_room_with_wall()
	var surfaces: Array = scene.surfaces
	var wall: Surface = scene.wall

	var player_pos := Vector2(200, 300)
	var cursor_pos := Vector2(500, 300)
	var dir := Direction.from_coords(player_pos, cursor_pos)

	var path := TestTrace.trace(player_pos, dir, surfaces, GameState.new(),
		null, -1.0, TraceMode.PHYSICAL, TraceMode.PHYSICAL,
		[], null, cursor_pos)

	assert_gt(path.steps.size(), 0, "Should have at least one step")

	var last: TraceStep = path.steps[path.steps.size() - 1]
	var wall_carrier := wall.segment.get_carrier()
	var f := wall_carrier.evaluate(last.end)
	var gx := 2.0 * wall_carrier.a * last.end.x + wall_carrier.b
	var gy := 2.0 * wall_carrier.a * last.end.y + wall_carrier.c
	var grad := sqrt(gx * gx + gy * gy)
	var dist_to_wall := absf(f) / maxf(grad, 1e-10)

	assert_lt(dist_to_wall, 2.0,
		"Physical trace should stop at the wall (dist=%.2f)" % dist_to_wall)

	for i in path.steps.size():
		var step: TraceStep = path.steps[i]
		assert_lt(step.end.x, 502.0,
			"Step %d should not cross the wall (end.x=%.2f)" % [i, step.end.x])

func test_effect_applied_when_cursor_on_mirror() -> void:
	var scene := _build_room_with_mirror()
	var surfaces: Array = scene.surfaces

	var player_pos := Vector2(200, 300)
	var cursor_pos := Vector2(500, 300)
	var dir := Direction.from_coords(player_pos, cursor_pos)

	var path := TestTrace.trace(player_pos, dir, surfaces, GameState.new(),
		null, -1.0, TraceMode.PHYSICAL, TraceMode.PHYSICAL,
		[], null, cursor_pos)

	gut.p("Mirror test: %d steps, cursor_index=%d" % [path.steps.size(), path.cursor_index])
	for i in path.steps.size():
		var step: TraceStep = path.steps[i]
		var hit_desc := "null"
		if step.hit != null:
			if step.hit.segment != null:
				hit_desc = "seg(on=%s, side=%d, bl=%s, br=%s)" % [
					step.hit.on_segment, step.hit.side,
					step.hit.blocked_left, step.hit.blocked_right]
			else:
				hit_desc = "null-seg"
		gut.p("  step[%d]: start=%s end=%s fid=%d hit=%s" % [
			i, step.start, step.end, step.frame_id, hit_desc])

	assert_gt(path.steps.size(), 1, "Should have multiple steps")

	var has_frame_change := false
	for i in range(1, path.steps.size()):
		if path.steps[i].frame_id != path.steps[0].frame_id:
			has_frame_change = true
			break

	assert_true(has_frame_change,
		"Physical trace should reflect off mirror even when cursor is on the mirror")

func _load_three_mirrors() -> void:
	_scene = load("res://scenes/test_levels/three_mirrors.tscn").instantiate()
	_scene.gravity = Vector2.ZERO
	add_child_autofree(_scene)
	await get_tree().process_frame
	await get_tree().process_frame

func _setup_scene_and_trace(player_pos: Vector2, cursor_pos: Vector2, plan_data: Array) -> void:
	var player := _scene.get_node("Player")
	var cursor := _scene.get_node("Cursor")
	var game_mgr := _scene.get_node_or_null("GameManager")
	player.position = player_pos
	cursor.position = cursor_pos
	if game_mgr and "plan" in game_mgr:
		game_mgr.plan.clear()
		for pe in plan_data:
			game_mgr.plan.add_entry(pe[0], pe[1])
	_scene.get_node("PathRenderer")._compute_trace()

func test_planned_trace_stops_at_floor_when_cursor_on_floor() -> void:
	await _load_three_mirrors()
	_setup_scene_and_trace(Vector2(960, 250), Vector2(960, 840), [[4, 0]])

	var renderer := _scene.get_node("PathRenderer")
	var planned: TracePath = renderer.get_planned_path()

	assert_not_null(planned, "Planned path should exist")
	assert_gt(planned.steps.size(), 0, "Should have at least one step")

	var last: TraceStep = planned.steps[planned.steps.size() - 1]
	assert_almost_eq(last.end.y, 840.0, 2.0,
		"Planned trace should stop at floor (y=840), got y=%.1f" % last.end.y)

func test_physical_trace_stops_at_floor_when_cursor_on_floor() -> void:
	await _load_three_mirrors()
	_setup_scene_and_trace(Vector2(960, 250), Vector2(960, 840), [[4, 0]])

	var renderer := _scene.get_node("PathRenderer")
	var physical: TracePath = renderer.get_traced_path()

	assert_not_null(physical, "Physical path should exist")
	assert_gt(physical.steps.size(), 0, "Should have at least one step")

	var last: TraceStep = physical.steps[physical.steps.size() - 1]
	assert_almost_eq(last.end.y, 840.0, 2.0,
		"Physical trace should stop at floor (y=840), got y=%.1f" % last.end.y)
