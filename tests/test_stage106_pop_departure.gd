extends GutTest

# Stage 106: the departure point after a pop (GAME_SPEC 11.4; K2). A pop tags
# the pop point as the restored beam's departure point, like a push does, so a
# known point there (the player or the cursor exactly on a circle mirror) never
# snaps the departing carrier's other crossing onto it; on a resumed beam the
# departing carrier's root nearest the departure parameter is the departure.
# bench/fixtures/stage106_pop_departure.json holds the oracle's traces
# (docs/research/oracle/gen_fixtures_stage106.py).

const FIXTURES := "res://bench/fixtures/stage106_pop_departure.json"
const Rules := preload("res://tests/test_stage81_trace_rules.gd")
const POINT_TOL := 1e-9

const TANGENT := "res://scenes/playground/tangent_infinity.tscn"
const CIRCLE_NO_WALLS := "res://scenes/test_levels/circle_no_walls.tscn"
# K2: the player on the inversion circle's top, the cursor whose image under
# the mirror plan is the circle's bottom
const K2_PLAYER := Vector2(1160, 440)
const K2_CURSOR := Vector2(760, 640)
const K2_IMAGE := Vector2(1160, 640)
# K3: the cursor the player's mirror image in the planned mirror x = 960: every
# direction solves the plan; the convention is straight at the cursor
const K3_PLAYER := Vector2(835, 665)
const K3_CURSOR := Vector2(1085, 665)

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
	assert_eq(_fx.size(), 11, "11 Stage 106 fixtures")
	for name in ["k2_player_on_inversion_circle_physical", "k2_reference_aimed_at_the_image", "cursor_on_arc_pop_planned",
			"periodic_orbit_direction", "portal_pop_by_rehit_direction"]:
		assert_true(_fx.has(name), name)

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

func test_solver_matches_stage106_fixtures():
	for name in _fx:
		for ctx: NumCtx in [NumCtx.f64(), NumCtx.with_limbs(4)]:
			if _check(_walk(name, ctx), _fx[name]["expected"], "%s [%s]" % [name, ctx.name()]):
				pass_test("%s [%s]" % [name, ctx.name()])

func test_no_fixture_re_applies_in_place():
	for name in _fx:
		var events := _walk(name, NumCtx.f64())
		assert_eq(InvariantChecker.check_reapply_in_place(events), [] as Array[String], name)

# ---------------------------------------------------------------- live scenes

func _load(path: String) -> Node:
	FeatureFlags.infinity_event_override = true
	Surface.reset_id_counter()
	var scene: Node = load(path).instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	scene.get_node("Cursor").set_process(false)
	scene.get_node("Player").set_physics_process(false)
	return scene

func _hits(events: Array) -> Array:
	return events.filter(func(e): return e.kind == "hit").map(func(e): return [e.surface_id, e.applied])

# K2 live: the planned shot and a shot aimed straight at the image are one walk;
# every invariant holds, every tier agrees
func test_k2_on_the_tangent_infinity_playground():
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
	var plan := [PlanManager.PlanEntry.new(mirror.id, Side.Value.LEFT)]
	assert_eq(checker.check_all(K2_PLAYER, K2_CURSOR, plan), [] as Array[String], "no violation (DIRECTION-ONLY included)")
	var planned_shot: Array = _hits(renderer.last_beam_result.physical)
	assert_eq(checker.check_DIRECTION_ONLY(K2_PLAYER, K2_CURSOR, plan), [] as Array[String], "every tier agrees")
	assert_eq(checker.check_all(K2_PLAYER, K2_IMAGE, []), [] as Array[String])
	assert_eq(planned_shot, _hits(renderer.last_beam_result.physical), "the same walk as the shot aimed at the image")
	assert_gt(planned_shot.size(), 3)
	assert_eq(planned_shot[0][0], planned_shot[1][0], "in at the image, out at the player: the same circle")
	# K3: the degenerate aim: no violation (REAPPLY-IN-PLACE included), the planned path reaches the cursor
	assert_eq(checker.check_all(K3_PLAYER, K3_CURSOR, plan), [] as Array[String], "K3: no violation")
	assert_true(renderer.last_beam_result.planned.any(func(e): return e.kind == "cursor"), "K3: the planned path reaches the cursor")
	assert_eq(InvariantChecker.check_reapply_in_place(renderer.last_beam_result.physical), [] as Array[String])

# The cursor exactly on the circle mirror of circle_no_walls: the preview
# reflects there and continues through the circle's far side; no zero-length
# loop at the cursor (the old phantom re-hit)
func test_cursor_on_the_circle_of_circle_no_walls():
	var scene := _load(CIRCLE_NO_WALLS)
	await get_tree().process_frame
	var circle: Surface = null
	for s in scene.surfaces:
		if not s.segment.is_line():
			circle = s
	assert_not_null(circle)
	var c := circle.segment.get_carrier().center()
	var r := circle.segment.get_carrier().radius()
	var cursor := c + Vector2(r, 0)
	var player := c + Vector2(-r - 100, 40)
	var checker := InvariantChecker.new()
	checker.setup(scene)
	var renderer: Node2D = scene.get_node("PathRenderer")
	assert_eq(checker.check_all(player, cursor, []), [] as Array[String])
	var physical: Array = renderer.last_beam_result.physical
	assert_eq(InvariantChecker.check_reapply_in_place(physical), [] as Array[String])
	var hits := _hits(physical)
	assert_gt(hits.size(), 2)
	# the physical trace enters the circle, leaves it, and the arrow goes on
	var on_circle := hits.filter(func(h): return h[0] == circle.id and h[1])
	assert_gt(on_circle.size(), 1, "the circle is entered and left")
