extends GutTest

# Stage 85: state changes (GAME_SPEC 10.6) along traces, and planning with
# forward state simulation (13.7). Expected values: the mpmath oracle (trace2)
# -- bench/fixtures/stage85_flags.json (gen_fixtures_stage85.py).

const FIXTURE := "res://bench/fixtures/stage85_flags.json"
const Rules := preload("res://tests/test_stage81_trace_rules.gd")
const POINT_TOL := 1e-9

var _fx: Dictionary = {}

func before_all():
	var f := FileAccess.open(FIXTURE, FileAccess.READ)
	for x in JSON.parse_string(f.get_as_text())["fixtures"]:
		_fx[x["name"]] = x

func _walk(name: String, ctx: NumCtx) -> Array:
	var fx: Dictionary = _fx[name]
	var scene := Rules.scene_of(ctx, fx["surfaces"])
	var player := ctx.cn(fx["player"][0], fx["player"][1])
	var cursor = null if fx["cursor"] == null else ctx.cn(fx["cursor"][0], fx["cursor"][1])
	var mode: int = BeamSolver.PHYSICAL if fx["mode"] == "physical" else BeamSolver.PLANNED
	var plan := Rules.plan_of(fx["plan"])
	var flags: Dictionary = fx["flags"]
	if fx["direction"] != null:
		return BeamSolver.trace_direction(ctx, scene, player, ctx.cn(fx["direction"][0], fx["direction"][1]), plan, cursor, mode,
			BeamSolver.MAX_HITS, flags)
	if mode == BeamSolver.PLANNED:
		return BeamSolver.planned(ctx, scene, player, cursor, plan, BeamSolver.MAX_HITS, flags)
	return BeamSolver.physical(ctx, scene, player, cursor, plan, BeamSolver.MAX_HITS, flags)

func _check(events: Array, expected: Array, label: String) -> bool:
	if events.size() != expected.size():
		fail_test("%s: %d events, oracle has %d (%s)" % [label, events.size(), expected.size(),
			", ".join(events.map(func(e): return e.describe()))])
		return false
	for i in expected.size():
		var e: BeamSolverEvent = events[i]
		var x: Dictionary = expected[i]
		var got := [e.kind, e.surface_id, ("LEFT" if e.left else "RIGHT") if e.kind == "hit" else "",
			e.applied, e.terminal, e.on_segment, e.rehit, e.state]
		var want := [x["kind"], int(x["surface_id"]), x["side"], x["applied"], x["terminal"], x["on_segment"], x["rehit"], x["state"]]
		if got != want:
			fail_test("%s event %d: got %s, oracle %s" % [label, i, str(got), str(want)])
			return false
		if x["flags"] != null and e.flags != x["flags"]:
			fail_test("%s event %d: flags %s, oracle %s" % [label, i, str(e.flags), str(x["flags"])])
			return false
		if x.has("x") and e.point != null:
			var d := Vector2(e.point.fx(), e.point.fy()).distance_to(Vector2(String(x["x"]).to_float(), String(x["y"]).to_float()))
			if d > POINT_TOL:
				fail_test("%s event %d: point off by %s" % [label, i, d])
				return false
	return true

func _check_fixture(name: String) -> void:
	assert_true(_fx.has(name), "fixture %s" % name)
	if not _fx.has(name):
		return
	for ctx: NumCtx in [NumCtx.f64(), NumCtx.with_limbs(4)]:
		if _check(_walk(name, ctx), _fx[name]["expected"], "%s [%s]" % [name, ctx.name()]):
			pass_test("%s [%s]" % [name, ctx.name()])

func test_set_and_toggle_ops():
	var flags := {}
	flags = StateChange.new("open", StateChange.Op.TOGGLE).apply(flags)
	assert_eq(flags, {"open": true}, "toggle of a missing key: true")
	flags = StateChange.new("open", StateChange.Op.TOGGLE).apply(flags)
	assert_eq(flags, {"open": false})
	var before := flags
	flags = StateChange.new("k", StateChange.Op.SET, 2).apply(flags)
	assert_eq(flags, {"open": false, "k": 2})
	assert_eq(before, {"open": false}, "apply returns a new dictionary")
	assert_eq(StateChange.flag_key(true), "true")
	assert_eq(StateChange.flag_key(false), "false")
	assert_eq(StateChange.flag_key(2), "2")
	assert_eq(StateChange.flag_key(2.0), "2")
	assert_eq(StateChange.flag_key("red"), "red")
	# scene-def form (oracle format)
	assert_eq(StateChange.new("k", StateChange.Op.SET, 2).to_def(), {"key": "k", "op": "set", "value": 2})
	assert_eq(StateChange.new("open", StateChange.Op.TOGGLE).to_def(), {"key": "open", "op": "toggle", "value": true})

func test_state_change_fires_only_when_side_fires():
	_check_fixture("state_change_off_segment_does_not_fire")
	_check_fixture("initial_flags_resolve")

func test_last_writer_wins_along_trace():
	_check_fixture("last_writer_wins_along_trace")

func test_effect_and_state_change_coexist():
	_check_fixture("effect_and_state_change_coexist")

func test_categorical_mirror_switches_mid_trace():
	_check_fixture("categorical_mirror_switches_mid_trace")

func test_planned_applies_state_on_consumed_entry():
	_check_fixture("planned_applies_state_on_consumed_entry")

func test_planner_forward_state_simulation():
	_check_fixture("planner_forward_state_simulation_pass")
	_check_fixture("planner_forward_state_simulation_planned")
	_check_fixture("planner_forward_state_simulation_physical")

func test_pops_restore_beams_not_flags():
	_check_fixture("pops_restore_beams_not_flags")
	var ev := _walk("pops_restore_beams_not_flags", NumCtx.f64())
	var popped := ev.filter(func(e): return e.kind == "hit" and e.popped)
	assert_eq(popped.size(), 1, "the circle re-hit pops")
	if popped.size() == 1:
		assert_eq(popped[0].flags, {"k": true}, "the toggle survives the pop")

func test_fast_twin_flags():
	var ctx := NumCtx.f64()
	for name in _fx:
		var fx: Dictionary = _fx[name]
		var gen := _walk(name, ctx)
		var sc := Rules.fast_scene_of(fx["surfaces"])
		var mode: int = BeamSolver.PHYSICAL if fx["mode"] == "physical" else BeamSolver.PLANNED
		var plan := Rules.plan_of(fx["plan"])
		var out
		if fx["direction"] != null:
			out = FastSolver.trace_direction(sc, PackedFloat64Array(fx["player"]), PackedFloat64Array(fx["direction"]), plan,
				null if fx["cursor"] == null else PackedFloat64Array(fx["cursor"]), mode, BeamSolver.MAX_HITS, fx["flags"])
		elif mode == BeamSolver.PLANNED:
			out = FastSolver.planned(sc, PackedFloat64Array(fx["player"]), PackedFloat64Array(fx["cursor"]), plan, BeamSolver.MAX_HITS, fx["flags"])
		else:
			out = FastSolver.physical(sc, PackedFloat64Array(fx["player"]), PackedFloat64Array(fx["cursor"]), plan, BeamSolver.MAX_HITS, fx["flags"])
		assert_eq(out.events.size(), gen.size(), "%s: event count" % name)
		for i in mini(out.events.size(), gen.size()):
			var a: BeamSolverEvent = out.events[i]
			var b: BeamSolverEvent = gen[i]
			assert_eq([a.kind, a.surface_id, a.left, a.applied, a.state, a.flags], [b.kind, b.surface_id, b.left, b.applied, b.state, b.flags],
				"%s event %d" % [name, i])

# The game: the preview runs on the game state's flags; firing promotes the
# physical trace's final flags into the game state; undo restores them.
func test_state_promoted_after_shot_and_undo_restores():
	Surface.reset_id_counter()
	var scene: Node = load("res://scenes/test_levels/flag_switches.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	await get_tree().process_frame
	var gm = scene.get_node("GameManager")
	assert_eq(gm.game_state.flags, {}, "no flags at start")
	# aim through the toggler, then the door
	scene.get_node("Player").global_position = Vector2(700, 520)
	scene.get_node("Cursor").global_position = Vector2(1300, 540)
	var pair := TestTrace.trace_pair(scene.surfaces, Vector2(700, 520), Vector2(1300, 540), [], gm.game_state.flags)
	assert_not_null(pair)
	if pair == null:
		return
	assert_eq(pair.final_flags.get("open"), true, "the toggler fired on the physical trace")
	gm._try_fire()
	gm._on_flight_completed()
	assert_eq(gm.game_state.flags.get("open"), true, "promoted after the shot")
	# the preview now resolves the door under the new state
	var pair2 := TestTrace.trace_pair(scene.surfaces, Vector2(700, 520), Vector2(1300, 540), [], gm.game_state.flags)
	assert_ne(str(pair2.physical.steps.map(func(s): return s.end)), str(pair.physical.steps.map(func(s): return s.end)),
		"the preview depends on the flags")
	gm._try_undo()
	assert_eq(gm.game_state.flags, {}, "undo restores the flags")
	get_tree().paused = false

# The world state is part of a frame: a state change starts a new frame. A
# toggler that only the physical trace fires (not planned) is a divergence --
# visible where it happens, not after the cursor; with the toggler planned,
# both traces toggle and stay aligned.
func test_state_change_starts_new_frame():
	Surface.reset_id_counter()
	var scene: Node = load("res://scenes/test_levels/flag_switches.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	await get_tree().process_frame
	var tog: Surface = null
	for s: Surface in scene.surfaces:
		var c: SideConfig = s.active_side_config(Side.Value.LEFT, GameState.new())
		if c != null and c.state_change != null:
			tog = s
	assert_not_null(tog)
	var player := Vector2(700, 520)
	var cursor := Vector2(1000, 530)
	var pair := TestTrace.trace_pair(scene.surfaces, player, cursor, [])
	var phys_frames: Array = pair.physical.steps.map(func(s): return s.frame_id)
	var plan_frames: Array = pair.planned.steps.map(func(s): return s.frame_id)
	assert_eq(phys_frames[0], TraceStep.IDENTITY_FRAME)
	assert_ne(phys_frames[1], TraceStep.IDENTITY_FRAME, "the toggle starts a new physical frame")
	assert_eq(plan_frames[1], TraceStep.IDENTITY_FRAME, "the planned trace passes the unplanned toggler")
	var ci: int = pair.planned.cursor_index if pair.planned.cursor_index >= 0 else pair.planned.steps.size()
	var merged := StepTreeMerge.merge(pair.planned.steps, pair.physical.steps, ci)
	assert_ne(merged[1].type, StepTypes.Type.ALIGNED, "diverged at the toggler")
	# planned toggler: both toggle, aligned through the cursor
	var side := Side.Value.LEFT if pair.physical.steps[0].hit_side == Side.Value.LEFT else Side.Value.RIGHT
	var pair2 := TestTrace.trace_pair(scene.surfaces, player, cursor, [PlanManager.PlanEntry.new(tog.id, side)])
	assert_ne(pair2.planned.cursor_index, -1, "the plan reaches the cursor")
	var ci2: int = pair2.planned.cursor_index
	var merged2 := StepTreeMerge.merge(pair2.planned.steps, pair2.physical.steps, ci2)
	for i in ci2:
		assert_eq(merged2[i].type, StepTypes.Type.ALIGNED, "step %d aligned" % i)
