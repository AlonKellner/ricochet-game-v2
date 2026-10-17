extends GutTest

# Stage 110, B1 (GAME_SPEC 14.1): a planned entry that changes no frame -- a pass
# side, a terminal crossed off its segment -- is consumed by the physical walk
# at the same event as by the planned one. The preview's two paths then agree up
# to the cursor, as the merge says (oracle: gen_fixtures_stage110.py; native:
# test_quiet_entries.cpp).

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

func _room() -> Array:
	return RoomBuilder.create_room_surfaces(Rect2(0, 0, 1000, 600))

func _pass_line(a: Vector2, b: Vector2) -> Surface:
	var seg := Segment.from_coords(a, b, (a + b) / 2.0)
	return Surface.new(seg, SideConfig.new(null, true), SideConfig.new(null, true), false, false)

func _side_toward(surfaces: Array, player: Vector2, cursor: Vector2, sid: int) -> Side.Value:
	var pair := TestTrace.trace_pair(surfaces, player, cursor)
	for s in pair.physical.steps:
		if s.hit != null and s.surface_id == sid:
			return s.hit_side
	return Side.Value.LEFT

func _assert_agree(surfaces: Array, entry_surface: Surface, player: Vector2, cursor: Vector2, label: String) -> void:
	var plan := [PlanManager.PlanEntry.new(entry_surface.id, _side_toward(surfaces, player, cursor, entry_surface.id))]
	var pair := TestTrace.trace_pair(surfaces, player, cursor, plan)
	assert_not_null(pair, label)
	assert_true(pair.physical.cursor_index >= 0, label + ": the physical path reaches the cursor")
	assert_eq(pair.planned.cursor_index, pair.physical.cursor_index, label)
	for i in pair.planned.cursor_index + 1:
		assert_eq(pair.planned.steps[i].end, pair.physical.steps[i].end, "%s: step %d" % [label, i])
	var merged := StepTreeMerge.merge(pair.planned.steps, pair.physical.steps, pair.planned.cursor_index)
	for i in pair.planned.cursor_index:
		assert_eq(merged[i].type, StepTypes.Type.ALIGNED, "%s: merged step %d" % [label, i])

func test_planned_pass_entry_reaches_the_cursor():
	var door := _pass_line(Vector2(300, 100), Vector2(300, 500))
	var surfaces := [door] + _room()
	_assert_agree(surfaces, door, Vector2(100, 300), Vector2(600, 350), "pass entry")

func test_planned_terminal_crossed_off_segment_reaches_the_cursor():
	var block := H.wall_between(Vector2(300, 400), Vector2(300, 550))
	var surfaces := [block] + _room()
	_assert_agree(surfaces, block, Vector2(100, 300), Vector2(600, 250), "terminal off segment")

# Stage 110, B5: on test_levels/hit_effects, surfaces 15, 16 and 17 share the
# carrier x = 1100. Aiming through surface 16's carrier off its segment, the
# physical walk fires surface 15 there, which writes what the planned entry 16
# writes: it leaves the point in the planned frame, so the entry counts as
# consumed (match by frame). Both paths reach the cursor, and nothing after the
# cursor is drawn diverged (oracle: gen_fixtures_stage110_frames.py).
func test_a_co_carrier_write_of_the_planned_value_matches_the_entry():
	Surface.reset_id_counter()
	var scene: Node = load("res://scenes/test_levels/hit_effects.tscn").instantiate()
	add_child_autofree(scene)
	await get_tree().process_frame
	var surfaces: Array = scene.surfaces
	var plan := [PlanManager.PlanEntry.new(16, Side.Value.RIGHT)]
	var pair := TestTrace.trace_pair(surfaces, Vector2(960, 540), Vector2(1260, 324), plan)
	assert_not_null(pair)
	assert_true(pair.physical.cursor_index >= 0, "the physical path reaches the cursor")
	assert_eq(pair.planned.cursor_index, pair.physical.cursor_index)
	var merged := StepTreeMerge.merge(pair.planned.steps, pair.physical.steps, pair.planned.cursor_index)
	for i in merged.size():
		var t: int = merged[i].type
		assert_true(t == StepTypes.Type.ALIGNED or t == StepTypes.Type.ALIGNED_POST_PLANNED, "merged step %d is aligned (type %d)" % [i, t])
