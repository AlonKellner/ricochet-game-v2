extends GutTest

# Stage 105: a beam tangent to a surface's carrier at the frame's infinity
# point Q is a touch, not a crossing (GAME_SPEC 12.5, 31), decided exactly
# from provenance (the pole record: Q and the beam's direction there, exact).
# bench/fixtures/stage105_tangent_at_infinity.json holds the oracle's traces
# (docs/research/oracle/gen_fixtures_stage105.py); K1, the known finding of
# Stage 103 on the portals playground, is resolved.

const FIXTURES := "res://bench/fixtures/stage105_tangent_at_infinity.json"
const Rules := preload("res://tests/test_stage81_trace_rules.gd")
const POINT_TOL := 1e-9

const PORTALS := "res://scenes/playground/portals.tscn"
const K1_PLAYER := Vector2(1243.333, 690)
const K1_CURSOR := Vector2(1243.333, 390)
const K1_MIRROR := 5

const TANGENT := "res://scenes/playground/tangent_infinity.tscn"
const Q0 := Vector2(960, 540)
const PLAYER := Vector2(760, 640)
# the circle through PLAYER and Q0 centered on y = 540 (at (835, 540), radius
# 125) is tangent to the vertical mirror x = 960 at Q0: a cursor on it grazes
const GRAZE := Vector2(835, 665)

var _fx: Dictionary = {}

func before_all():
	var f := FileAccess.open(FIXTURES, FileAccess.READ)
	assert_not_null(f, "fixture file exists")
	var d: Dictionary = JSON.parse_string(f.get_as_text())
	for x in d["fixtures"]:
		_fx[x["name"]] = x

func after_each() -> void:
	FeatureFlags.infinity_event_override = null

# ---------------------------------------------------------------- the fixtures

func test_fixture_schema():
	assert_eq(_fx.size(), 14, "14 Stage 105 fixtures")
	for name in ["pre_tangent_mirror_direction", "pre_transversal_mirror_direction", "k1_portals_vertical_physical",
			"departing_from_q_rehit", "straight_beam_concurrent_at_q"]:
		assert_true(_fx.has(name), name)

# Walk a fixture with the solver at `ctx`, the infinity event on.
func _walk(name: String, ctx: NumCtx) -> Array:
	var fx: Dictionary = _fx[name]
	var scene := BeamScene.from_defs(ctx, fx["surfaces"])
	scene.emit_infinity = true
	var player := ctx.cn(fx["player"][0], fx["player"][1])
	var cursor = null if fx["cursor"] == null else ctx.cn(fx["cursor"][0], fx["cursor"][1])
	var mode: int = BeamSolver.PHYSICAL if fx["mode"] == "physical" else BeamSolver.PLANNED
	var plan := Rules.plan_of(fx["plan"])
	var flags: Dictionary = fx["flags"] if fx.get("flags") != null else {}
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
			e.applied, e.terminal, e.on_segment, e.rehit]
		var want := [x["kind"], int(x["surface_id"]), x["side"], x["applied"], x["terminal"], x["on_segment"], x["rehit"]]
		if got != want:
			fail_test("%s event %d: got %s, oracle %s" % [label, i, str(got), str(want)])
			return false
		if x.has("x") and e.point != null:
			var d := Vector2(e.point.fx(), e.point.fy()).distance_to(Vector2(String(x["x"]).to_float(), String(x["y"]).to_float()))
			if d > POINT_TOL:
				fail_test("%s event %d: point off by %s" % [label, i, d])
				return false
	return true

func test_solver_matches_stage105_fixtures():
	for name in _fx:
		for ctx: NumCtx in [NumCtx.f64(), NumCtx.with_limbs(4)]:
			if _check(_walk(name, ctx), _fx[name]["expected"], "%s [%s]" % [name, ctx.name()]):
				pass_test("%s [%s]" % [name, ctx.name()])

func test_tangent_fixtures_have_no_hit_on_the_grazed_surface():
	for name in ["pre_tangent_mirror_direction", "pre_tangent_mirror_direct_physical", "k1_portals_vertical_physical"]:
		var sid := K1_MIRROR if name.begins_with("k1") else 1
		var hits: Array = _walk(name, NumCtx.f64()).filter(func(e): return e.kind == "hit" and e.surface_id == sid)
		assert_eq(hits.size(), 0, "%s: a touch at Q, no hit on %d" % [name, sid])
	# the control: a transversal aim crosses the mirror at Q0
	var control: Array = _walk("pre_transversal_mirror_direction", NumCtx.f64())
	assert_gt(control.filter(func(e): return e.kind == "hit" and e.surface_id == 1).size(), 0)

# ---------------------------------------------------------------- live scenes

func _load(path: String) -> Node:
	FeatureFlags.infinity_event_override = true
	Surface.reset_id_counter()
	var scene: Node = load(path).instantiate()
	add_child_autofree(scene)
	scene.get_node("Cursor").set_process(false)
	scene.get_node("Player").set_physics_process(false)
	return scene

func _hits_on(events: Array, sid: int) -> int:
	return events.filter(func(e): return e.kind == "hit" and e.surface_id == sid).size()

# K1: the vertical aim on the portals playground; the arc moves Q onto mirror 5
# through the portal, the circle beam is tangent to it there.
func test_k1_resolved_on_the_portals_playground():
	var scene := _load(PORTALS)
	await get_tree().process_frame
	var checker := InvariantChecker.new()
	checker.setup(scene)
	var renderer: Node2D = scene.get_node("PathRenderer")
	for plan in [[], [PlanManager.PlanEntry.new(8, Side.Value.LEFT), PlanManager.PlanEntry.new(8, Side.Value.LEFT)]]:
		var v := checker.check_all(K1_PLAYER, K1_CURSOR, plan)
		assert_eq(v, [] as Array[String], "plan of %d entries: no violations" % plan.size())
		assert_eq(_hits_on(renderer.last_beam_result.physical, K1_MIRROR), 0, "no hit on mirror 5")
		assert_eq(checker.check_DIRECTION_ONLY(K1_PLAYER, K1_CURSOR, plan), [] as Array[String], "every tier agrees")

# The new playground: the aim that grazes the mirror at the cross makes no
# reflection there; a few pixels to either side, the arc crosses it.
func test_graze_at_the_cross_is_a_touch():
	var scene := _load(TANGENT)
	await get_tree().process_frame
	var mirror: Surface = null
	for s in scene.surfaces:
		if not s.pre_transform and s.segment.is_line() and s.segment.start.coords.x == 960.0 and s.segment.end.coords.x == 960.0:
			mirror = s
	assert_not_null(mirror, "the vertical mirror through Q0")
	var checker := InvariantChecker.new()
	checker.setup(scene)
	var renderer: Node2D = scene.get_node("PathRenderer")
	assert_eq(checker.check_all(PLAYER, GRAZE, []), [] as Array[String])
	var physical: Array = renderer.last_beam_result.physical
	assert_eq(_hits_on(physical, mirror.id), 0, "a touch at Q0, not a crossing")
	assert_true(physical.any(func(e): return e.kind == "infinity"), "the beam passes Q0")
	assert_eq(_hits_on(renderer.last_beam_result.planned, mirror.id), 0, "the plan agrees")
	for dx in [-4.0, 4.0]:
		assert_eq(checker.check_all(PLAYER, GRAZE + Vector2(dx, 0), []), [] as Array[String])
		assert_gt(_hits_on(renderer.last_beam_result.physical, mirror.id), 0, "off the graze (dx %s) the arc crosses the mirror" % dx)
	# the same at every tier (the touch is exact, not a rounding outcome)
	assert_eq(checker.check_DIRECTION_ONLY(PLAYER, GRAZE, []), [] as Array[String])
