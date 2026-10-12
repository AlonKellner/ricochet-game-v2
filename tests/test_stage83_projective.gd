extends GutTest

# Stage 83: projective effects on the beam solver (PHYSICAL), and the
# switch-over of every scene. Expected values: the mpmath oracle (trace2):
# bench/fixtures/stage83_projective.json (gen_fixtures_stage83.py).

const FIXTURE := "res://bench/fixtures/stage83_projective.json"
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

func _check_fixture(name: String, tol: float = POINT_TOL) -> void:
	assert_true(_fx.has(name), "fixture %s" % name)
	for ctx: NumCtx in [NumCtx.f64(), NumCtx.with_limbs(4)]:
		if _check(_walk(name, ctx), _fx[name]["expected"], "%s [%s]" % [name, ctx.name()], tol):
			pass_test("%s [%s]" % [name, ctx.name()])

func test_projective_exit_directions():
	for kind in ["line_normal", "circle_normal_inside", "circle_normal_outside", "line_directional", "circle_directional"]:
		for variant in ["through", "back"]:
			_check_fixture("%s_%s" % [kind, variant])

func test_projective_resets_stack_and_epoch():
	_check_fixture("projective_clears_stack")
	var ev := _walk("projective_clears_stack", NumCtx.f64())
	# event 1 is the projective; the mirror is pushed again afterwards (a pop
	# would mean the stack survived the projective)
	assert_gt(ev.size(), 3)
	if ev.size() > 3:
		assert_false(ev[2].popped, "the mirror after the projective is a push")
		assert_eq(ev[0].epoch, 0)
		assert_eq(ev[2].epoch, 1, "a projective starts a new epoch")
	# frame ids: steps after the projective are a new frame, even with an
	# empty stack
	var path := BeamTracer.to_path(ev, [], 0, {}, {}, null)
	var ids := {}
	for s in path.steps:
		ids[s.frame_id] = true
	assert_gt(ids.size(), 2, "identity before, new epoch frames after")

func test_projective_straight_beam_exact():
	for name in _fx:
		if not (name.contains("normal") or name.contains("directional")):
			continue
		var ev := _walk(name, NumCtx.f64())
		for i in range(1, ev.size()):
			if ev[i - 1].kind == "hit" and ev[i - 1].applied and ev[i - 1].projective:
				assert_true(ev[i].beam.kappa.is_zero(), "%s: the beam after a projective is exactly straight" % name)

func test_planned_passes_projectives_until_87():
	_check_fixture("planned_passes_projective")

func test_degenerate_inputs_native():
	for name in ["player_at_inversion_center", "player_at_inversion_center_planned",
			"cursor_at_inversion_center", "cursor_at_inversion_center_no_plan"]:
		_check_fixture(name, 0.01)
	# the game path is the beam solver, no fallback
	BeamTracer.clear_cache()
	Surface.reset_id_counter()
	MobiusTransform.reset_id_counter()
	var scene: Node = load("res://scenes/playground/inversion_grid.tscn").instantiate()
	add_child_autofree(scene)
	await get_tree().process_frame
	var pair := BeamTracer.trace_pair(scene.surfaces, Vector2(760, 380), Vector2(1000, 700),
		[PlanManager.PlanEntry.new(1, Side.Value.RIGHT)])
	assert_not_null(pair, "player at an inversion center: beam solver, no fallback")

func test_fast_twin_projective():
	var ctx := NumCtx.f64()
	for name in _fx:
		var fx: Dictionary = _fx[name]
		if fx["direction"] == null:
			continue
		var gen := _walk(name, ctx)
		var out := FastSolver.trace_direction(FastSolver.Scene.from_defs(fx["surfaces"]), PackedFloat64Array(fx["player"]),
			PackedFloat64Array(fx["direction"]), Rules.plan_of(fx["plan"]), null,
			BeamSolver.Mode.PHYSICAL if fx["mode"] == "physical" else BeamSolver.Mode.PLANNED)
		assert_eq(out.events.size(), gen.size(), "%s: event count" % name)
		for i in mini(out.events.size(), gen.size()):
			var a: BeamSolver.Event = out.events[i]
			var b: BeamSolver.Event = gen[i]
			assert_eq([a.kind, a.surface_id, a.left, a.applied], [b.kind, b.surface_id, b.left, b.applied], "%s event %d" % [name, i])
			if a.point != null and b.point != null:
				assert_eq([a.point.fx(), a.point.fy()], [b.point.fx(), b.point.fy()], "%s event %d: bit-identical" % [name, i])

# ---------------------------------------------------------------- switch-over

func test_every_scene_uses_beam():
	var dirs := ["res://scenes", "res://scenes/test_levels", "res://scenes/playground"]
	var n := 0
	for dir_path in dirs:
		for fn in DirAccess.open(dir_path).get_files():
			if not fn.ends_with(".tscn"):
				continue
			BeamTracer.clear_cache()
			Surface.reset_id_counter()
			MobiusTransform.reset_id_counter()
			var scene: Node = load(dir_path + "/" + fn).instantiate()
			add_child_autofree(scene)
			await get_tree().process_frame
			var renderer := scene.get_node_or_null("PathRenderer")
			if renderer == null or not ("surfaces" in scene):
				continue
			scene.get_node("Player").global_position = Vector2(700, 560)
			scene.get_node("Cursor").global_position = Vector2(1130, 470)
			renderer._compute_trace()
			assert_eq(renderer.last_solver, "beam", fn)
			assert_true(BeamTracer.scene_supported(scene.surfaces), fn + " supported")
			n += 1
	assert_gt(n, 25)
