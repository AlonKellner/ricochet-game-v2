extends GutTest

# Stage 101: infinity as a point, and the pre-transform (GAME_SPEC 4.2, 9.5,
# 13.1). The frame's image of infinity Q is where the next ∞ entry is passed:
# the real infinity (the edge) in the identity frame, a point after an
# inversion or in a scene with a pre-transform. On screen it is drawn as a
# circled cross and is clickable like a surface; the ∞ entry first makes the
# path go the long way round the circle through Q.

const SCENE := "res://scenes/playground/pre_inverted.tscn"
const INF_ID := -3
const Q0 := Vector2(960, 540)        # the pre-transform circle's center
const PLAYER := Vector2(620, 420)
const CURSOR := Vector2(1300, 420)

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
	_cursor.global_position = CURSOR
	await get_tree().process_frame
	await get_tree().process_frame

func after_each() -> void:
	FeatureFlags.infinity_event_override = null

func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame

func _click(button: int) -> void:
	var ev := InputEventMouseButton.new()
	ev.button_index = button
	ev.pressed = true
	ev.position = _cursor.global_position
	_gm._input(ev)

func _kinds(events: Array) -> Array:
	return events.map(func(e): return e.kind)

func _pre_surface() -> Surface:
	for s in _scene.surfaces:
		if s.pre_transform:
			return s
	return null

# ---------------------------------------------------------------- the scene

func test_pre_transform_surface_is_not_a_surface_to_the_solver():
	var pre := _pre_surface()
	assert_not_null(pre, "the scene has a pre-transform circle")
	assert_false(pre.active_side_config(Side.Value.LEFT, GameState.new()).interactive)
	var defs := SceneDefs.defs_from_surfaces(_scene.surfaces)
	var pre_defs := defs.filter(func(d): return d.get("pre_transform", false))
	assert_eq(pre_defs.size(), 1)
	var scene := BeamScene.from_defs(NumCtx.f64(), defs)
	assert_false(scene.by_id.has(pre.id), "not among the solver's surfaces")
	assert_true(BeamTracer.scene_supported(defs))
	for info in InvariantChecker.surface_infos(_scene.surfaces, {}):
		assert_ne(info["id"], pre.id, "not a surface the invariant kernel checks")
	var det := ClickDetector.new()
	var r := det.detect_click(Q0 + Vector2(260, 0), _scene.surfaces, _gm.game_state)
	assert_true(r.is_empty(), "not clickable as a surface")

func test_infinity_point_follows_the_frame():
	var tracer := BeamTracer.new()
	tracer.emit_infinity = true
	var defs := SceneDefs.defs_from_surfaces(_scene.surfaces)
	assert_eq(tracer.infinity_point(defs, [], {}), Q0, "the pre-transform's image of infinity")
	# a mirror moves it to its mirror image
	var mirror: Surface = null
	for s in _scene.surfaces:
		if not s.pre_transform and s.segment.is_line() and s.segment.start.coords.x == 500.0 and s.segment.end.coords.x == 500.0:
			mirror = s
	assert_not_null(mirror)
	var after_mirror = tracer.infinity_point(defs, [PlanManager.PlanEntry.new(mirror.id, Side.Value.LEFT)], {})
	assert_eq(after_mirror, Vector2(40, 540), "reflected in x = 500")
	# the same mirror twice pops: back to Q0
	var twice = tracer.infinity_point(defs, [PlanManager.PlanEntry.new(mirror.id, Side.Value.LEFT), PlanManager.PlanEntry.new(mirror.id, Side.Value.LEFT)], {})
	assert_eq(twice, Q0)
	# an ∞ entry does not move it
	assert_eq(tracer.infinity_point(defs, [PlanManager.PlanEntry.new(INF_ID, Side.Value.LEFT)], {}), Q0)
	# the identity frame: infinity (null); after an inversion: the circle's center
	Surface.reset_id_counter()
	var main: Node = load("res://scenes/main.tscn").instantiate()
	add_child_autofree(main)
	await get_tree().process_frame
	var mdefs := SceneDefs.defs_from_surfaces(main.surfaces)
	assert_null(tracer.infinity_point(mdefs, [], {}), "no pre-transform: infinity")
	var inv: Surface = null
	for s in main.surfaces:
		if not s.segment.is_line() and s.active_side_config(Side.Value.LEFT, GameState.new()).effect is CircleInversionEffect:
			inv = s
	assert_not_null(inv)
	var q = tracer.infinity_point(mdefs, [PlanManager.PlanEntry.new(inv.id, Side.Value.LEFT)], {})
	assert_eq(q, inv.segment.get_carrier().center(), "after an inversion: its center")

# ---------------------------------------------------------------- the marker and the click

func test_infinity_point_marker_is_shown_and_clickable():
	assert_eq(_edge.get_point(), Q0, "the marker sits at the frame's image of infinity")
	_cursor.global_position = Q0 + Vector2(8, -6)
	await _frames(1)
	assert_true(_edge.is_hovered(), "hovering the marker")
	_click(MOUSE_BUTTON_LEFT)
	assert_eq(_gm.plan.entries.size(), 1)
	assert_eq(_gm.plan.entries[0].surface_id, INF_ID)
	assert_eq(_scene.get_node("PlanHUD")._label.text, "Plan:\n  1. ∞")
	await _frames(1)
	var marks: Array = _edge.get_marks()
	assert_eq(marks.size(), 1)
	assert_true(marks[0]["on_point"], "the mark is drawn at the point, not on the edge")
	assert_eq(marks[0]["point"], Q0)
	# the edge band is not the infinity entry while the point is on screen
	_cursor.global_position = Vector2(1910, 540)
	await _frames(1)
	assert_false(_edge.is_hovered())
	_click(MOUSE_BUTTON_RIGHT)
	assert_eq(_gm.plan.entries.size(), 0, "right-clicking empty space clears the plan")

func test_infinity_first_goes_the_long_way_round():
	# the direct realization: the cursor first, the frame's infinity after it
	var events: Array = _renderer.last_beam_result.planned
	var kinds := _kinds(events)
	assert_gt(kinds.find("cursor"), -1)
	var i_inf := kinds.find("infinity")
	assert_true(i_inf > kinds.find("cursor"), "direct: the point is passed after the cursor")
	assert_false(events[i_inf].applied)
	var q := Vector2(events[i_inf].point.fx(), events[i_inf].point.fy())
	assert_almost_eq(q.x, Q0.x, 1e-6)
	assert_almost_eq(q.y, Q0.y, 1e-6)
	assert_eq(_renderer.get_planned_path().steps[0].is_arc_step, true, "the beam is a circle through the point")
	# the ∞ entry first: the point before the cursor, the same circle the other way
	_gm.plan.add_entry(INF_ID, Side.Value.LEFT, Q0)
	await _frames(1)
	var long_events: Array = _renderer.last_beam_result.planned
	var lk := _kinds(long_events)
	var li := lk.find("infinity")
	assert_true(li >= 0 and li < lk.find("cursor"), "the entry first: the point is passed before the cursor")
	assert_true(long_events[li].applied, "it consumed the entry")
	var k_direct: float = events[0].beam.kappa.to_float()
	var k_long: float = long_events[0].beam.kappa.to_float()
	assert_almost_eq(k_direct + k_long, 0.0, 1e-12, "the same circle, reversed")
	var planned: TracePath = _renderer.get_planned_path()
	assert_gt(planned.cursor_index, 0, "the planned path reaches the cursor")
	var inf_steps := planned.steps.filter(func(s): return s.surface_id == INF_ID)
	assert_eq(inf_steps.size(), 1, "one step ends at the point")
	assert_almost_eq(inf_steps[0].end.distance_to(Q0), 0.0, 0.01)
	await _frames(1)
	var marks: Array = _edge.get_marks()
	assert_eq(marks.size(), 1)
	assert_true(marks[0]["valid"], "the physical trace passes the point: the entry is met")
	assert_eq(_kinds(_renderer.last_beam_result.physical).count("infinity") >= 1, true)

func test_unplanned_passage_is_not_a_divergence_and_planned_unmet_is():
	# a cursor near the player: the direct arc meets nothing before it, then the
	# beam passes the point unplanned and goes on; nothing diverges (post-cursor
	# steps are aligned post-planned)
	_cursor.global_position = Vector2(760, 470)
	await _frames(1)
	var kinds := _kinds(_renderer.last_beam_result.physical)
	assert_true(kinds.find("cursor") < kinds.find("infinity"), "the point is passed after the cursor, unplanned")
	var typed: Array = _renderer.get_typed_steps()
	assert_gt(typed.size(), 0)
	for st in typed:
		assert_true(st.type == StepTypes.Type.ALIGNED or st.type == StepTypes.Type.ALIGNED_POST_PLANNED,
			"no divergence from an unplanned passage (type %d)" % st.type)
	# the entry planned: the long way round goes over the top through the wall at
	# y = 150, where the physical arrow stops while the plan goes on: a divergence
	_gm.plan.add_entry(INF_ID, Side.Value.LEFT, Q0)
	await _frames(1)
	var physical: TracePath = _renderer.get_traced_path()
	assert_eq(physical.cursor_index, -1, "the physical arrow does not reach the cursor")
	var types: Array = _renderer.get_typed_steps().map(func(s): return s.type)
	assert_true(types.any(func(t): return t != StepTypes.Type.ALIGNED and t != StepTypes.Type.ALIGNED_POST_PLANNED),
		"the unmet entry shows as a divergence: %s" % str(types))
	await _frames(1)
	var marks: Array = _edge.get_marks()
	assert_eq(marks.size(), 1)
	assert_false(marks[0]["valid"], "the mark shows the entry unmet")

# ---------------------------------------------------------------- the invariants

func test_sweep_with_infinity_plans_in_the_pre_transform_scene():
	var mirror: Surface = null
	for s in _scene.surfaces:
		if not s.pre_transform and s.segment.is_line() and s.segment.start.coords.x == 500.0:
			mirror = s
	var plans: Array = [
		[],
		[PlanManager.PlanEntry.new(INF_ID, Side.Value.LEFT, Q0)],
		[PlanManager.PlanEntry.new(INF_ID, Side.Value.LEFT, Q0), PlanManager.PlanEntry.new(mirror.id, Side.Value.LEFT)],
		[PlanManager.PlanEntry.new(mirror.id, Side.Value.LEFT), PlanManager.PlanEntry.new(INF_ID, Side.Value.LEFT, Q0)],
	]
	var positions: Array[Vector2] = [PLAYER, CURSOR, Vector2(300, 450), Vector2(1100, 700), Vector2(800, 300), Vector2(1700, 900), Vector2(960, 540)]
	var failures := SweepRunner.sweep_native(_scene, positions, plans, true, true)
	assert_eq(failures.size(), 0, "no violation: %s" % str(failures.slice(0, 3)))
