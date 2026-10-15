extends GutTest

# Stage 88d: plannable infinity in the game layer (GAME_SPEC 4.2). With the
# infinity switch on, clicking the band inside the level bounds' edge where no
# surface wins appends the ∞ entry; hovering it highlights the band; the HUD
# lists "∞"; right-clicking the edge removes the latest ∞ entry; the preview
# reaches the cursor the other way round, through the wrap; the invariants
# hold on a sweep with ∞ plans. With the switch off (the default until B9 is
# approved) the edge is not clickable and nothing changes.

const SCENE := "res://scenes/open_room_playable.tscn"
const INF_ID := -3
# a horizontal beam at y = 270 passes above the mirrors (y >= 300) and below the
# room's top wall (y = 240): it meets only the open right side and the walls
const PLAYER := Vector2(960, 270)
const CURSOR_LEFT := Vector2(300, 270)   # left of the open room, reached through the wrap
const CURSOR_RIGHT := Vector2(1500, 270) # right of the room: the reversed aim meets the left wall
const EDGE_POINT := Vector2(1910, 270)   # 10 px inside the right edge, no surface near

var _scene: Node
var _player: Node2D
var _cursor: Node2D
var _renderer: Node2D
var _gm: Node
var _edge: Node2D

func before_each() -> void:
	FeatureFlags.infinity_event_override = true
	Surface.reset_id_counter()
	_scene = load(SCENE).instantiate()
	add_child_autofree(_scene)
	_player = _scene.get_node("Player")
	_cursor = _scene.get_node("Cursor")
	_renderer = _scene.get_node("PathRenderer")
	_gm = _scene.get_node("GameManager")
	_edge = _scene.get_node("EdgeOverlay")
	_cursor.set_process(false)
	_player.set_physics_process(false)
	_player.global_position = PLAYER
	_cursor.global_position = CURSOR_LEFT
	await get_tree().process_frame
	await get_tree().process_frame

func after_each() -> void:
	FeatureFlags.infinity_event_override = null

func _click(button: int) -> void:
	var ev := InputEventMouseButton.new()
	ev.button_index = button
	ev.pressed = true
	ev.position = _cursor.global_position
	_gm._input(ev)

func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame

# ---------------------------------------------------------------- the click detector

func test_edge_band_is_the_infinity_entry():
	var det := ClickDetector.new()
	var bounds := Rect2(0, 0, 1920, 1080)
	var r := det.detect_click(EDGE_POINT, _scene.surfaces, _gm.game_state, Callable(), bounds, true)
	assert_true(r.get("infinity", false), "inside the band: the infinity entry")
	assert_almost_eq(r["distance"], 10.0, 1e-6)
	assert_true(det.detect_click(Vector2(1880, 540), _scene.surfaces, _gm.game_state, Callable(), bounds, true).is_empty(), "40 px in: nothing")
	assert_true(det.detect_click(Vector2(1930, 540), _scene.surfaces, _gm.game_state, Callable(), bounds, true).is_empty(), "outside the bounds: nothing")
	assert_true(det.detect_click(EDGE_POINT, _scene.surfaces, _gm.game_state, Callable(), bounds, false).is_empty(), "switch off: the edge is not clickable")
	assert_true(det.detect_click(EDGE_POINT, _scene.surfaces, _gm.game_state, Callable(), Rect2(), true).is_empty(), "no bounds: nothing")
	# a surface within tolerance wins over the edge
	var near_mirror := Vector2(806, 540)
	var m := det.detect_click(near_mirror, _scene.surfaces, _gm.game_state, Callable(), bounds, true)
	assert_true(m.has("surface"))
	assert_eq(ClickDetector.edge_distance(Vector2(5, 500), bounds), 5.0)
	assert_lt(ClickDetector.edge_distance(Vector2(-5, 500), bounds), 0.0)

# ---------------------------------------------------------------- the game manager

func test_left_click_on_the_edge_appends_infinity():
	_cursor.global_position = EDGE_POINT
	await _frames(1)
	assert_true(_edge.is_hovered(), "the edge band is hovered")
	_click(MOUSE_BUTTON_LEFT)
	assert_eq(_gm.plan.entries.size(), 1)
	var e: PlanManager.PlanEntry = _gm.plan.entries[0]
	assert_eq(e.surface_id, INF_ID)
	assert_eq(e.click, EDGE_POINT, "the entry remembers where the edge was clicked")
	assert_eq(_scene.get_node("PlanHUD")._label.text, "Plan:\n  1. ∞")
	await _frames(1)
	var marks: Array = _edge.get_marks()
	assert_eq(marks.size(), 1, "one mark on the edge")
	assert_eq(marks[0]["index"], 1)
	assert_eq(marks[0]["point"], EDGE_POINT)

func test_right_click_on_the_edge_removes_the_latest_infinity():
	var mirror: Surface = null
	for s in _scene.surfaces:
		if s.segment.start.coords.x == 1200.0 and s.segment.end.coords.x == 1200.0:
			mirror = s
	assert_not_null(mirror)
	_gm.plan.add_entry(INF_ID, Side.Value.LEFT, EDGE_POINT)
	_gm.plan.add_entry(mirror.id, Side.Value.LEFT)
	_gm.plan.add_entry(INF_ID, Side.Value.LEFT, Vector2(960, 1075))
	_cursor.global_position = EDGE_POINT
	await _frames(1)
	_click(MOUSE_BUTTON_RIGHT)
	assert_eq(_gm.plan.entries.map(func(e): return e.surface_id), [INF_ID, mirror.id], "the latest infinity entry is removed")
	_click(MOUSE_BUTTON_RIGHT)
	assert_eq(_gm.plan.entries.map(func(e): return e.surface_id), [mirror.id])
	_click(MOUSE_BUTTON_RIGHT)
	assert_eq(_gm.plan.entries.size(), 0, "no infinity entry left: the plan is cleared")

func test_hover_leaves_the_edge_with_the_cursor():
	_cursor.global_position = EDGE_POINT
	await _frames(1)
	assert_true(_edge.is_hovered())
	_cursor.global_position = CURSOR_LEFT
	await _frames(1)
	assert_false(_edge.is_hovered())

# ---------------------------------------------------------------- the preview

func _event_kinds(events: Array) -> Array:
	return events.map(func(e): return e.kind)

func test_infinity_plan_reaches_the_cursor_through_the_wrap():
	_gm.plan.add_entry(INF_ID, Side.Value.LEFT, EDGE_POINT)
	await _frames(1)
	var planned: TracePath = _renderer.get_planned_path()
	assert_not_null(planned)
	assert_gt(planned.cursor_index, 0, "the planned path reaches the cursor")
	var first: TraceStep = planned.steps[0]
	assert_gt(first.end.x, first.start.x, "the aim is reversed: away from the cursor on the left, toward the right edge")
	# the solver's events: the wrap consumed the entry before the cursor
	var events: Array = _renderer.last_beam_result.planned
	var kinds := _event_kinds(events)
	var wrap := kinds.find("infinity")
	assert_gt(wrap, 0, "an infinity event (after the carrier crossings): %s" % str(kinds))
	assert_true(events[wrap].applied, "it consumed the infinity entry")
	assert_eq(events[wrap].surface_id, INF_ID)
	assert_gt(kinds.find("cursor"), wrap, "the cursor comes after the wrap")
	assert_eq(_event_kinds(_renderer.last_beam_result.physical).count("infinity"), 1, "the physical trace wraps once")
	# the display path leaves through the right edge and returns from the left one
	var leaves := false
	var returns := false
	for i in planned.cursor_index:
		var st: TraceStep = planned.steps[i]
		leaves = leaves or st.end.x == 1920.0
		returns = returns or st.start.x == 0.0
	assert_true(leaves and returns, "out through the right edge, back from the left")
	await _frames(1)
	var marks: Array = _edge.get_marks()
	assert_eq(marks.size(), 1)
	assert_true(marks[0]["valid"], "the entry is met: the mark is valid")
	# the direct realization: no entry, the preview goes straight to the cursor
	_gm.plan.clear()
	await _frames(1)
	var direct: TracePath = _renderer.get_planned_path()
	assert_lt(direct.steps[0].end.x, PLAYER.x, "without the entry the aim points at the cursor")
	# (the direct aim still wraps after passing the wall in PLANNED mode; nothing consumes it)
	var consumed: Array = _renderer.last_beam_result.planned.filter(func(e): return e.kind == "infinity" and e.applied)
	assert_eq(consumed.size(), 0, "no infinity entry to consume")
	assert_eq(_edge.get_marks().size(), 0, "a cleared plan clears the marks")

func test_infinity_entry_unmet_when_the_physical_trace_cannot_wrap():
	# aimed away from a cursor to the right, the arrow hits the room's left wall:
	# the planned path passes it (unplanned surfaces pass in PLANNED mode) and
	# wraps, the physical one stops there; the entry is shown unmet
	_cursor.global_position = CURSOR_RIGHT
	_gm.plan.add_entry(INF_ID, Side.Value.LEFT, EDGE_POINT)
	await _frames(2)
	var physical: TracePath = _renderer.get_traced_path()
	assert_eq(physical.cursor_index, -1, "the physical trace does not reach the cursor")
	assert_eq(physical.steps.back().end.x, 560.0, "it ends at the left wall")
	assert_eq(_event_kinds(_renderer.last_beam_result.physical).count("infinity"), 0, "no wrap")
	var marks: Array = _edge.get_marks()
	assert_eq(marks.size(), 1)
	assert_false(marks[0]["valid"], "the mark shows the entry unmet")

func test_switch_off_leaves_the_edge_alone():
	FeatureFlags.infinity_event_override = false
	_cursor.global_position = EDGE_POINT
	# a fresh manager state: the trackers rebuild on the next frame
	_gm._hover_inputs = null
	await _frames(1)
	assert_false(_edge.is_hovered(), "switch off: no edge hover")
	_click(MOUSE_BUTTON_LEFT)
	assert_eq(_gm.plan.entries.size(), 0, "switch off: the edge is not plannable")

# ---------------------------------------------------------------- the invariants

func test_sweep_with_infinity_plans_holds_the_invariants():
	var plans: Array = [
		[PlanManager.PlanEntry.new(INF_ID, Side.Value.LEFT, EDGE_POINT)],
		[PlanManager.PlanEntry.new(INF_ID, Side.Value.LEFT, EDGE_POINT), PlanManager.PlanEntry.new(INF_ID, Side.Value.LEFT, EDGE_POINT)],
	]
	var positions: Array[Vector2] = [PLAYER, CURSOR_LEFT, CURSOR_RIGHT, Vector2(960, 540), Vector2(700, 300), Vector2(1700, 900), Vector2(100, 100)]
	var failures := SweepRunner.sweep_native(_scene, positions, plans, true, true)
	assert_eq(failures.size(), 0, "no violation: %s" % str(failures.slice(0, 3)))
