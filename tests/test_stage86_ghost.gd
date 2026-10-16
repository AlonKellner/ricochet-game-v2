extends GutTest

# Stage 86: ghost mode. The reserved flag "ghost": while it is true every side
# resolves to a pass except ghost togglers; ghost is per shot. Expected
# values: the mpmath oracle (trace2) -- bench/fixtures/stage86_ghost.json
# (gen_fixtures_stage86.py).

const FIXTURE := "res://bench/fixtures/stage86_ghost.json"
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
	if fx["direction"] != null:
		return BeamSolver.trace_direction(ctx, scene, player, ctx.cn(fx["direction"][0], fx["direction"][1]), plan, cursor, mode,
			BeamSolver.MAX_HITS, fx["flags"])
	if mode == BeamSolver.PLANNED:
		return BeamSolver.planned(ctx, scene, player, cursor, plan, BeamSolver.MAX_HITS, fx["flags"])
	return BeamSolver.physical(ctx, scene, player, cursor, plan, BeamSolver.MAX_HITS, fx["flags"])

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

func test_ghost_toggler_toggles_flag():
	_check_fixture("ghost_ends_at_next_toggler")
	var ev := _walk("ghost_ends_at_next_toggler", NumCtx.f64())
	assert_eq(ev[0].flags.get("ghost"), true, "the first toggler ghosts")
	assert_eq(ev[2].flags.get("ghost"), false, "the second materializes")

func test_ghost_passes_everything():
	_check_fixture("ghost_passes_everything")

func test_ghost_ends_at_next_toggler():
	_check_fixture("ghost_ends_at_next_toggler")

# A ghost arrow leaves through the wall, wraps through infinity, comes back
# through the opposite wall and materializes at the toggler, one loop later.
func test_ghost_loops_through_infinity():
	_check_fixture("ghost_loops_through_infinity")

func test_planned_and_physical_agree_through_ghost():
	_check_fixture("ghost_planned_planned")
	_check_fixture("ghost_planned_physical")

func test_plan_entry_inside_ghost_stretch_is_a_pass():
	_check_fixture("ghost_plan_entry_inside_stretch")

func test_fast_twin_ghost():
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
				null, mode, BeamSolver.MAX_HITS, fx["flags"])
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

func test_ghost_steps_flagged():
	var ev := _walk("ghost_ends_at_next_toggler", NumCtx.f64())
	var path := BeamTracer.new().to_path(ev, [], 0, {}, null)
	# steps: player -> toggler 1 (solid), -> mirror 2 (ghost), -> toggler 3
	# (ghost), -> mirror 4 (solid)
	var ghost: Array = path.steps.slice(0, 4).map(func(s): return s.ghost)
	assert_eq(ghost, [false, true, true, false])
	# the merge keeps the flag
	assert_true(path.steps[1].with_type(StepTypes.Type.ALIGNED).ghost)

func test_ghost_toggler_rendering():
	var tog := SideConfig.new(null, true, [StateChangeHit.new("ghost", StateChange.Op.TOGGLE)])
	var flag := SideConfig.new(null, true, [StateChangeHit.new("open", StateChange.Op.TOGGLE)])
	assert_eq(SurfaceNode.config_color(tog), SurfaceNode.GHOST_TOGGLER_COLOR)
	assert_eq(SurfaceNode.config_color(flag), SurfaceNode.FLAG_TOGGLER_COLOR)
	assert_ne(SurfaceNode.GHOST_TOGGLER_COLOR, SurfaceNode.PASSTHROUGH_COLOR)
	assert_ne(PathRenderer.ghost_color(Color.CYAN), Color.CYAN, "ghost steps are drawn distinctly")

# The demo level: through the toggler, the inner wall and the room wall, off
# screen and around through infinity, back in through the far wall, and
# materialized at the second toggler -- the mirror after it acts.
func test_ghost_room_demo():
	Surface.reset_id_counter()
	var scene: Node = load("res://scenes/test_levels/ghost_room.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	await get_tree().process_frame
	var pair := TestTrace.trace_pair(scene.surfaces, Vector2(900, 540), Vector2(1300, 540), [])
	assert_not_null(pair)
	if pair == null:
		return
	var steps: Array = pair.physical.steps
	assert_true(steps.any(func(s): return is_inf(s.end.x)), "the ghost leaves to infinity")
	var last_ghost := -1
	for i in steps.size():
		if steps[i].ghost:
			last_ghost = i
	assert_gt(last_ghost, 0, "ghost steps")
	var after: Array = steps.slice(last_ghost + 1)
	assert_true(after.any(func(s): return s.hit != null and s.surface_id >= 0 and not s.ghost), "materialized: hits after the ghost stretch")
	var applied := pair.result.physical.filter(func(e): return e.kind == "hit" and e.applied and not e.flags.get("ghost", false))
	assert_gt(applied.size(), 0, "an effect after materializing")

# Ghost is per shot: the preview always starts solid, and the game never keeps
# ghost after a shot -- even when the shot ends ghosted.
func test_ghost_is_per_shot():
	Surface.reset_id_counter()
	var scene: Node = load("res://scenes/test_levels/ghost_room.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	await get_tree().process_frame
	var gm = scene.get_node("GameManager")
	var p := TestTrace.trace_pair(scene.surfaces, Vector2(900, 540), Vector2(1300, 540), [], {"ghost": true})
	var q := TestTrace.trace_pair(scene.surfaces, Vector2(900, 540), Vector2(1300, 540), [], {})
	assert_eq(p.result.physical.size(), q.result.physical.size(), "a trace never starts ghosted")
	scene.get_node("Player").global_position = Vector2(900, 540)
	scene.get_node("Cursor").global_position = Vector2(1300, 540)
	gm._try_fire()
	gm._on_flight_completed()
	assert_false(gm.game_state.flags.has("ghost"), "ghost is not kept after the shot")
	get_tree().paused = false

# --- Planning inside a ghost stretch: while the plan's simulated state is
# ghosted (after an odd number of planned ghost togglers), only ghost togglers
# can be planned; every other side is refused -- at the click and at the hover.

func _ghost_room() -> Node:
	Surface.reset_id_counter()
	var scene: Node = load("res://scenes/test_levels/ghost_room.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	return scene

func _surf_at(scene: Node, x: float, y: float) -> Surface:
	for s in scene.surfaces:
		if s.segment.start.coords == Vector2(x, y):
			return s
	return null

func test_plan_refuses_non_toggler_inside_ghost_stretch():
	var scene := _ghost_room()
	await get_tree().process_frame
	var a := _surf_at(scene, 1000, 400)
	var b := _surf_at(scene, 650, 400)
	var block := _surf_at(scene, 1100, 400)
	var mirror := _surf_at(scene, 720, 480)
	var P := PlanManager.PlanEntry
	var L := Side.Value.LEFT
	var R := Side.Value.RIGHT
	# solid plan: everything interactive is accepted
	assert_false(TestTrace.plan_refuses(scene.surfaces, [], mirror.id, true))
	# after A the plan is ghosted: the mirror is refused (both sides), the
	# togglers are not
	var ghosted := [P.new(a.id, L)]
	assert_true(TestTrace.plan_refuses(scene.surfaces, ghosted, mirror.id, true), "mirror inside the stretch")
	assert_true(TestTrace.plan_refuses(scene.surfaces, ghosted, mirror.id, false), "both sides")
	assert_true(TestTrace.plan_refuses(scene.surfaces, ghosted, block.id, true), "non-interactive too")
	assert_false(TestTrace.plan_refuses(scene.surfaces, ghosted, b.id, false), "the second toggler")
	assert_false(TestTrace.plan_refuses(scene.surfaces, ghosted, a.id, true), "the same toggler again")
	# after A, B the plan is solid again
	assert_false(TestTrace.plan_refuses(scene.surfaces, [P.new(a.id, L), P.new(b.id, R)], mirror.id, true))
	# ghost is per shot: a plan starts solid whatever the game state holds
	assert_false(TestTrace.plan_refuses(scene.surfaces, [], mirror.id, true, {"ghost": true}))

func test_click_and_hover_refuse_inside_ghost_stretch():
	var scene := _ghost_room()
	await get_tree().process_frame
	var gm = scene.get_node("GameManager")
	var a := _surf_at(scene, 1000, 400)
	var b := _surf_at(scene, 650, 400)
	var mirror := _surf_at(scene, 720, 480)
	var cursor: Node2D = scene.get_node("Cursor")
	gm.plan.add_entry(a.id, Side.Value.LEFT)
	# a click on the mirror is refused; no hover highlight either
	cursor.global_position = Vector2(745, 530)
	gm._update_hover()
	assert_null(gm._hovered_node, "no hover on a refused side")
	gm._handle_plan_click(false)
	assert_eq(gm.plan.entries.size(), 1, "the mirror is refused")
	# the toggler B is accepted
	cursor.global_position = Vector2(655, 550)
	gm._update_hover()
	assert_not_null(gm._hovered_node, "hover on the toggler")
	gm._handle_plan_click(false)
	assert_eq(gm.plan.entries.size(), 2)
	assert_eq(gm.plan.entries[1].surface_id, b.id)
	# solid again: the mirror is accepted
	cursor.global_position = Vector2(745, 530)
	gm._handle_plan_click(false)
	assert_eq(gm.plan.entries.size(), 3)
	assert_eq(gm.plan.entries[2].surface_id, mirror.id)
	get_tree().paused = false

# A click within reach of both a refused side and a toggler takes the toggler.
func test_click_near_refused_side_takes_toggler():
	Surface.reset_id_counter()
	var tog := SideConfig.new(null, true, [StateChangeHit.new("ghost", StateChange.Op.TOGGLE)])
	var seg2 := Segment.from_coords(Vector2(10, -100), Vector2(10, 100), Vector2(10, 0))
	var mir := SideConfig.new(ReflectionEffect.new(seg2.get_carrier()), true)
	var s1 := Surface.new(Segment.from_coords(Vector2(0, -100), Vector2(0, 100), Vector2(0, 0)), tog, tog, false, false)
	var s2 := Surface.new(seg2, mir, mir, false, false)
	var s3 := Surface.new(Segment.from_coords(Vector2(-200, -100), Vector2(-200, 100), Vector2(-200, 0)), tog, tog, false, false)
	var surfaces := [s1, s2, s3]
	var plan := [PlanManager.PlanEntry.new(s3.id, Side.Value.RIGHT)]
	var accept := func(surf: Surface, side: Side.Value) -> bool:
		return not TestTrace.plan_refuses(surfaces, plan, surf.id, side == Side.Value.LEFT)
	var r: Dictionary = ClickDetector.new().detect_click(Vector2(8, 0), surfaces, null, accept)
	assert_false(r.is_empty())
	if not r.is_empty():
		assert_eq(r.surface.id, s1.id, "the mirror is nearer but refused")
