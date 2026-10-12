extends GutTest

# Stage 82: portals (rigid motion) in the beam solver. Expected values: closed
# forms, and the mpmath oracle (trace2): bench/fixtures/stage82_portals.json
# (docs/research/oracle/gen_fixtures_stage82.py) and the scene corpus.

const FIXTURE := "res://bench/fixtures/stage82_portals.json"
const Rules := preload("res://tests/test_stage81_trace_rules.gd")
const POINT_TOL := 1e-9
const CORPUS_TOL := 0.01

var _fx: Dictionary = {}

func before_all():
	var f := FileAccess.open(FIXTURE, FileAccess.READ)
	for x in JSON.parse_string(f.get_as_text())["fixtures"]:
		_fx[x["name"]] = x

func _walk(name: String, ctx: NumCtx) -> Array:
	var fx: Dictionary = _fx[name]
	var scene := BeamScene.from_defs(ctx, fx["surfaces"])
	var player := ctx.cn(fx["player"][0], fx["player"][1])
	var cursor = null if fx["cursor"] == null else ctx.cn(fx["cursor"][0], fx["cursor"][1])
	var mode: int = BeamSolver.Mode.PHYSICAL if fx["mode"] == "physical" else BeamSolver.Mode.PLANNED
	var plan := Rules.plan_of(fx["plan"])
	if fx["direction"] != null:
		return BeamSolver.trace_direction(ctx, scene, player, ctx.cn(fx["direction"][0], fx["direction"][1]), plan, cursor, mode)
	if mode == BeamSolver.Mode.PLANNED:
		return BeamSolver.planned(ctx, scene, player, cursor, plan)
	return BeamSolver.physical(ctx, scene, player, cursor, plan)

func _check(events: Array, expected: Array, label: String, tol: float) -> bool:
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
			if d > tol:
				fail_test("%s event %d: point off by %s" % [label, i, d])
				return false
	return true

func _check_fixture(name: String) -> void:
	assert_true(_fx.has(name), "fixture %s" % name)
	for ctx: NumCtx in [NumCtx.f64(), NumCtx.with_limbs(4)]:
		if _check(_walk(name, ctx), _fx[name]["expected"], "%s [%s]" % [name, ctx.name()], POINT_TOL):
			pass_test("%s [%s]" % [name, ctx.name()])

# ---------------------------------------------------------------- beam map

func test_portal_beam_map():
	# closed form: the outgoing beam is the rigid image of the incoming one:
	# it contains T(z) for points z of the incoming continuation, ahead of the
	# exit point, with the same curvature
	var ctx := NumCtx.f64()
	var rng := RandomNumberGenerator.new()
	rng.seed = 82
	var checked := 0
	for i in 100:
		var theta: float = 0.0 if i % 3 == 0 else rng.randf_range(-PI, PI)
		# float64 throughout (Vector2 is float32 in this build)
		var dxv: float = rng.randf_range(-500, 500)
		var dyv: float = rng.randf_range(-500, 500)
		var c := cos(theta)
		var sn := sin(theta)
		var inv_d := [-(c * dxv + sn * dyv), -(c * dyv - sn * dxv)]
		var src := {"id": 1, "line": true, "start": [100.0, -400.0], "end": [100.0, 400.0], "via": [100.0, 0.0], "full": false,
			"left": {"type": "portal", "theta": theta, "d": [dxv, dyv], "pair": 2},
			"right": {"type": "portal", "theta": theta, "d": [dxv, dyv], "pair": 2}}
		var tgt := {"id": 2, "line": true, "start": [c * 100.0 + sn * 400.0 + dxv, sn * 100.0 - c * 400.0 + dyv],
			"end": [c * 100.0 - sn * 400.0 + dxv, sn * 100.0 + c * 400.0 + dyv], "via": [c * 100.0 + dxv, sn * 100.0 + dyv], "full": false,
			"left": {"type": "portal", "theta": -theta, "d": inv_d, "pair": 1},
			"right": {"type": "portal", "theta": -theta, "d": inv_d, "pair": 1}}
		var scene := BeamScene.from_defs(ctx, [src, tgt])
		assert_not_null(scene, "scene with portals")
		if scene == null:
			return
		var ang := rng.randf_range(-0.5, 0.5)
		var k: float = 0.0 if i % 2 == 0 else rng.randf_range(-0.004, 0.004)
		var b := HPBeam.make(ctx.cn(0, rng.randf_range(-100, 100)), ctx.cn(cos(ang), sin(ang)), ctx.num(k))
		var roots := BeamGeom.hits(b, scene.by_id[1].seg, false)
		if roots.is_empty():
			continue
		var t = roots[0]
		var out: HPBeam = BeamEffects.apply(b, t, scene.by_id[1], "portal")
		assert_not_null(out, "portal beam map")
		if out == null:
			return
		assert_eq(out.kappa.to_float(), k, "beam %d: curvature preserved" % i)
		for dt in [5.0, 23.0]:
			var z := b.at(t.add(ctx.num(dt)))
			var tzx: float = c * z.fx() - sn * z.fy() + dxv
			var tzy: float = sn * z.fx() + c * z.fy() + dyv
			var u = out.param_of(ctx.cn(tzx, tzy))
			var back := out.at(u)
			var err := sqrt((back.fx() - tzx) * (back.fx() - tzx) + (back.fy() - tzy) * (back.fy() - tzy))
			assert_lt(err, 1e-9, "beam %d: T(z) on the outgoing beam" % i)
			assert_gt(u.to_float(), 0.0, "beam %d: ahead of the exit point" % i)
		checked += 1
	assert_gt(checked, 60)

# ---------------------------------------------------------------- rules

func test_portal_translate_and_departure():
	_check_fixture("portal_translate")
	# the beam leaving the pair does not re-hit it at the exit point
	var ev := _walk("portal_translate", NumCtx.f64())
	if ev.size() > 1:
		assert_ne(ev[1].surface_id, 2, "no immediate re-hit of the paired surface")

func test_pop_across_pair_resumes_exactly():
	_check_fixture("portal_pop_departure_rehit")
	# the pop restores the earlier beam object exactly: a straight beam stays
	# exactly straight
	var ev := _walk("portal_pop_departure_rehit", NumCtx.f64())
	if ev.size() > 3:
		assert_true(ev[3].beam == ev[0].beam, "restored beam is the original object")
		assert_true(ev[3].beam.kappa.is_zero(), "exactly straight")

func test_portal_stack_rules():
	_check_fixture("portal_mirror_then_pair_pushes")

func test_portal_rotation_and_arcs():
	_check_fixture("portal_rotated_quarter")
	_check_fixture("portal_arc")

func test_plan_through_portal_level():
	_check_fixture("planned_through_portal_level")
	_check_fixture("physical_through_portal_level")

func test_plan_through_portal_reaches_cursor():
	var n := 0
	for prefix in ["playground__portals", "test_levels__portals"]:
		for name in Rules.corpus_names(prefix):
			var se := Rules.load_corpus(name)
			var scn: Dictionary = se[0]
			var res := PrecisionLadder.solve(scn["surfaces"], scn["player"], scn["cursor"], Rules.plan_of(scn["plan"]), 32, 32, CORPUS_TOL)
			_check(res.planned, se[1]["planned"], name + " planned", CORPUS_TOL)
			_check(res.physical, se[1]["physical"], name + " physical", CORPUS_TOL)
			for e in res.planned:
				if e.kind == "cursor":
					assert_lt(Vector2(e.point.fx(), e.point.fy()).distance_to(Vector2(scn["cursor"][0], scn["cursor"][1])), 1e-6,
						"%s: the planned path passes through the cursor" % name)
			n += 1
	assert_eq(n, 24)

# ---------------------------------------------------------------- display

func test_after_portal_gap_flag():
	BeamTracer.clear_cache()
	Surface.reset_id_counter()
	MobiusTransform.reset_id_counter()
	var scene: Node = load("res://scenes/test_levels/portals.tscn").instantiate()
	add_child_autofree(scene)
	await get_tree().process_frame
	scene.get_node("Player").global_position = Vector2(760, 500)
	scene.get_node("Cursor").global_position = Vector2(1250, 560)
	var renderer := scene.get_node("PathRenderer")
	renderer._compute_trace()
	assert_eq(renderer.last_solver, "beam", "portal scenes use the beam solver")
	var path: Tracer.TracedPath = renderer.get_traced_path()
	var found := false
	for i in range(1, path.steps.size()):
		var s: Tracer.Step = path.steps[i]
		if s.after_portal:
			found = true
			# the step starts on the paired surface (x = 1130), the previous
			# one ended on the source (x = 830): the visible gap
			assert_almost_eq(s.start.x, 1130.0, 1e-3, "exit on the pair")
			assert_almost_eq(path.steps[i - 1].end.x, 830.0, 1e-3, "entry on the source")
			break
	assert_true(found, "a step after the portal carries after_portal")

# ---------------------------------------------------------------- twin

func test_fast_twin_portal():
	var ctx := NumCtx.f64()
	for name in _fx:
		var fx: Dictionary = _fx[name]
		if fx["direction"] == null:
			continue
		var gen := _walk(name, ctx)
		assert_eq(gen.size(), fx["expected"].size(), "%s: generic walk matches the oracle length" % name)
		var out := FastSolver.trace_direction(FastSolver.Scene.from_defs(fx["surfaces"]), PackedFloat64Array(fx["player"]),
			PackedFloat64Array(fx["direction"]), Rules.plan_of(fx["plan"]), null,
			BeamSolver.Mode.PHYSICAL if fx["mode"] == "physical" else BeamSolver.Mode.PLANNED)
		assert_eq(out.events.size(), gen.size(), "%s: event count" % name)
		for i in mini(out.events.size(), gen.size()):
			var a: BeamSolver.Event = out.events[i]
			var b: BeamSolver.Event = gen[i]
			assert_eq([a.kind, a.surface_id, a.left, a.applied, a.on_segment, a.rehit],
				[b.kind, b.surface_id, b.left, b.applied, b.on_segment, b.rehit], "%s event %d" % [name, i])
			if a.point != null and b.point != null:
				assert_eq([a.point.fx(), a.point.fy()], [b.point.fx(), b.point.fy()], "%s event %d: bit-identical" % [name, i])
