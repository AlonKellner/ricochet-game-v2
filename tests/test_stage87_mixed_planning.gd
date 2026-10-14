extends GutTest

# Stage 87: plannable projectives (GAME_SPEC 13.3 / 13.4). A projective plan
# entry is applied in PLANNED mode; the aim is found by back-propagation: the
# hit point H from which the projected beam reaches the target behind it.
# Expected values: the mpmath oracle (trace2) --
# bench/fixtures/stage87_projective_plans.json (gen_fixtures_stage87.py).

const FIXTURE := "res://bench/fixtures/stage87_projective_plans.json"
const Rules := preload("res://tests/test_stage81_trace_rules.gd")
const POINT_TOL := 1e-9

var _fx: Dictionary = {}

func before_all():
	var f := FileAccess.open(FIXTURE, FileAccess.READ)
	for x in JSON.parse_string(f.get_as_text())["fixtures"]:
		_fx[x["name"]] = x

func _walk(name: String, ctx: NumCtx) -> Array:
	var fx: Dictionary = _fx[name]
	var scene := BeamScene.from_defs(ctx, fx["surfaces"])
	var player := ctx.cn(fx["player"][0], fx["player"][1])
	var cursor := ctx.cn(fx["cursor"][0], fx["cursor"][1])
	var plan := Rules.plan_of(fx["plan"])
	if fx["mode"] == "planned":
		return BeamSolver.planned(ctx, scene, player, cursor, plan)
	return BeamSolver.physical(ctx, scene, player, cursor, plan)

func _check(events: Array, expected: Array, label: String) -> bool:
	if events.size() != expected.size():
		fail_test("%s: %d events, oracle has %d (%s)" % [label, events.size(), expected.size(),
			", ".join(events.map(func(e): return e.describe()))])
		return false
	for i in expected.size():
		var e: BeamSolver.Event = events[i]
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

func _check_fixture(name: String) -> void:
	assert_true(_fx.has(name), "fixture %s" % name)
	if not _fx.has(name):
		return
	for ctx: NumCtx in [NumCtx.f64(), NumCtx.with_limbs(4)]:
		if _check(_walk(name, ctx), _fx[name]["expected"], "%s [%s]" % [name, ctx.name()]):
			pass_test("%s [%s]" % [name, ctx.name()])

# Closed forms of the back-propagated hit point (13.3).
func test_back_propagation_per_type():
	var ctx := NumCtx.f64()
	var mk := func(kind: String, back: bool, normal):
		var c := {"type": kind, "interactive": true, "back": back}
		if normal != null:
			c["normal"] = normal
		return c
	# line normal: the foot of X on the line x = 300
	var ln := BeamScene.from_defs(ctx, [{"id": 1, "line": true, "start": [300.0, -400.0], "end": [300.0, 400.0], "via": [300.0, 0.0],
		"full": false, "left": mk.call("line_normal_projection", false, null), "right": mk.call("line_normal_projection", false, null)}])
	var H = BeamEffects.back_propagate(ln.by_id[1], false, ctx.cn(600.0, 200.0))
	assert_eq([H.fx(), H.fy()], [300.0, 200.0], "line normal: the foot")
	# circle normal: c - R u when the beam enters from outside, through
	var cn := BeamScene.from_defs(ctx, [{"id": 1, "line": false, "start": [700.0, 0.0], "end": [400.0, -173.20508075688772],
		"via": [400.0, 173.20508075688772], "full": true, "circle": {"center": [500.0, 0.0], "radius": 200.0},
		"left": mk.call("circle_normal_projection", false, null), "right": mk.call("circle_normal_projection", false, null)}])
	var X := ctx.cn(500.0 + 300.0, 400.0)       # u = (0.6, 0.8)
	var s1: BeamScene.Surf = cn.by_id[1]
	var Hc = BeamEffects.back_propagate(s1, BeamGeom.side_left(s1.seg, ctx.cn(500.0 - 200.0, 0.0), ctx.cn(1.0, 0.0)), X)
	assert_almost_eq(Vector2(Hc.fx(), Hc.fy()), Vector2(500.0 - 120.0, -160.0), Vector2(1e-9, 1e-9),
		"circle normal (entering, through): the exit through the center reaches X directly")
	# directional on a line: the line through X along the normal
	var ld := BeamScene.from_defs(ctx, [{"id": 1, "line": true, "start": [300.0, -400.0], "end": [300.0, 400.0], "via": [300.0, 0.0],
		"full": false, "left": mk.call("line_directional_projection", false, [1.0, 1.0]), "right": mk.call("line_directional_projection", false, [1.0, 1.0])}])
	var Hd = BeamEffects.back_propagate(ld.by_id[1], false, ctx.cn(600.0, 250.0))
	assert_eq([Hd.fx(), Hd.fy()], [300.0, -50.0], "directional: X - s n on the line")
	# a directional normal parallel to the line: no hit point
	var lp := BeamScene.from_defs(ctx, [{"id": 1, "line": true, "start": [300.0, -400.0], "end": [300.0, 400.0], "via": [300.0, 0.0],
		"full": false, "left": mk.call("line_directional_projection", false, [0.0, 1.0]), "right": mk.call("line_directional_projection", false, [0.0, 1.0])}])
	assert_null(BeamEffects.back_propagate(lp.by_id[1], false, ctx.cn(600.0, 250.0)))

func test_line_normal_plans():
	for n in ["line_normal_through_planned", "line_normal_through_physical", "line_normal_back_planned", "line_normal_back_physical"]:
		_check_fixture(n)

func test_circle_normal_plans():
	for n in ["circle_normal_through_planned", "circle_normal_through_physical", "circle_normal_back_planned",
			"circle_normal_back_physical", "circle_normal_both_crossings_planned", "circle_normal_both_crossings_physical"]:
		_check_fixture(n)

func test_directional_plans():
	for n in ["line_directional_planned", "line_directional_physical", "circle_directional_planned",
			"circle_directional_physical", "circle_directional_both_crossings_planned", "circle_directional_both_crossings_physical"]:
		_check_fixture(n)

func test_mixed_chain_reaches_cursor():
	_check_fixture("mixed_chain_planned")
	_check_fixture("mixed_chain_physical")

# Legacy quirk 3: a projective last plan entry is applied and the cursor is
# reached after it.
func test_projective_last_entry_injects_cursor():
	var ev := _walk("line_normal_through_planned", NumCtx.f64())
	assert_true(ev[0].applied and ev[0].projective, "the projective entry is applied in PLANNED mode")
	assert_eq(ev[1].kind, "cursor", "then the cursor")

func test_physical_matches_planned_when_realizable():
	for base in ["line_normal_through", "line_normal_back", "circle_normal_back", "line_directional",
			"circle_normal_both_crossings", "circle_directional_both_crossings", "mixed_chain"]:
		var p := _walk(base + "_planned", NumCtx.f64())
		var q := _walk(base + "_physical", NumCtx.f64())
		var ci := -1
		for i in p.size():
			if p[i].kind == "cursor":
				ci = i
		assert_gt(ci, 0, base + ": planned reaches the cursor")
		for i in ci + 1:
			if i < q.size():
				assert_eq([p[i].kind, p[i].surface_id, p[i].left, p[i].applied], [q[i].kind, q[i].surface_id, q[i].left, q[i].applied],
					"%s event %d" % [base, i])

func test_fast_twin_projective_plans():
	var ctx := NumCtx.f64()
	for name in _fx:
		var fx: Dictionary = _fx[name]
		var gen := _walk(name, ctx)
		var sc := FastSolver.Scene.from_defs(fx["surfaces"])
		var plan := Rules.plan_of(fx["plan"])
		var out = FastSolver.planned(sc, PackedFloat64Array(fx["player"]), PackedFloat64Array(fx["cursor"]), plan) \
			if fx["mode"] == "planned" else FastSolver.physical(sc, PackedFloat64Array(fx["player"]), PackedFloat64Array(fx["cursor"]), plan)
		assert_eq(out.events.size(), gen.size(), "%s: event count" % name)
		for i in mini(out.events.size(), gen.size()):
			var a: BeamSolver.Event = out.events[i]
			var b: BeamSolver.Event = gen[i]
			assert_eq([a.kind, a.surface_id, a.left, a.applied], [b.kind, b.surface_id, b.left, b.applied], "%s event %d" % [name, i])
			if a.point != null and b.point != null:
				assert_eq([a.point.fx(), a.point.fy()], [b.point.fx(), b.point.fy()], "%s event %d: bit-identical" % [name, i])

# Found by the projective plan sweep: the beam leaving a planned projective is
# exactly straight (a projective exit is a line by definition). Built as a
# circle through rounded points it kept a residual curvature: a horizontal exit
# then "met" the horizontal walls at infinity, the ladder climbed to its top
# tier and the pair was rejected (no line drawn).
func test_planned_exit_beam_exactly_straight():
	Surface.reset_id_counter()
	BeamTracer.clear_cache()
	var scene: Node = load("res://scenes/playground/projective.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	await get_tree().process_frame
	var plan := [PlanManager.PlanEntry.new(6, Side.Value.LEFT), PlanManager.PlanEntry.new(7, Side.Value.LEFT)]
	var pair := BeamTracer.trace_pair(scene.surfaces, Vector2(1243.33337402344, 390.0), Vector2(1810.0, 90.0), plan)
	assert_not_null(pair, "a trace pair (finite events)")
	if pair == null:
		return
	assert_eq(pair.result.planned_tier, 0, "decided at float64, no climb")
	var ev: Array = pair.result.planned
	for i in range(1, ev.size()):
		if ev[i - 1].kind == "hit" and ev[i - 1].applied and ev[i - 1].projective:
			assert_true(ev[i].beam.kappa.is_zero(), "the exit beam is exactly straight")


# User capture: a 7-entry projective plan whose exact path needs the on-arc
# candidate at a circle normal; the nearer candidate dead-ends further back.
# The backward pass searches the candidates (on-segment first) and backtracks.
func test_backtracking_finds_exact_path():
	for k in 2:
		for mode in ["planned", "physical"]:
			var n := "user_capture_%d_%s" % [k, mode]
			_check_fixture(n)
			var ev := _walk(n, NumCtx.f64())
			var cur := Vector2(_fx[n]["cursor"][0], _fx[n]["cursor"][1])
			var ce := ev.filter(func(e): return e.kind == "cursor")
			assert_eq(ce.size(), 1, n + ": the cursor is reached")
			if ce.size() == 1:
				assert_almost_eq(Vector2(ce[0].point.fx(), ce[0].point.fy()), cur, Vector2(1e-6, 1e-6), n + ": exactly at the cursor")
