extends GutTest
## Stage 98 differential test: the GDScript InvariantChecker (GdInvariantChecker,
## every check in GDScript over a live PathRenderer) against the native kernel
## through its two paths: the thin InvariantChecker driver (live renderer) and
## SweepKernel (no scene tree). Violation strings must be identical on the
## sweep scenes' combos and on the hand-corrupted cases.

const Cases := preload("res://tests/native/invariant_cases.gd")
const H := preload("res://tests/test_helpers.gd")

var _mismatches: Array = []
var _checks := 0


func _note(label: String, a: Array, b: Array) -> void:
	_checks += 1
	if Array(a) != Array(b):
		_mismatches.append("%s: %s vs %s" % [label, a, b])


func _report(what: String) -> void:
	assert_gt(_checks, 0, "%s: checks" % what)
	assert_eq(_mismatches.size(), 0, "%s: %d of %d differ\n%s" % [what, _mismatches.size(), _checks, "\n".join(_mismatches.slice(0, 20))])
	_mismatches.clear()
	_checks = 0


# ---------------------------------------------------------------- hand-built cases

func test_cases_identical_and_detected() -> void:
	for c in Cases.cases():
		var parent := Cases.FakeParent.new()
		var fake := Cases.FakeRenderer.new()
		parent.add_child(fake)
		add_child_autofree(parent)
		parent.surfaces = c["surfaces"]
		fake.physical = c["physical"]
		fake.planned = c["planned"]
		fake.typed = c["typed"]
		var gd := GdInvariantChecker.new()
		gd._renderer = fake
		var drv := InvariantChecker.new()
		drv._renderer = fake
		var a: Array = gd.check_all(c["player"], c["cursor"])
		var b: Array = drv.check_all(c["player"], c["cursor"])
		_note(c["name"], a, b)
		if c["name"] == "nan":
			# the fake renderer's get_line_direction() normalizes the NaN step for the GDScript checker: Godot warns
			assert_engine_error("cannot be normalized")
		for want in c["expect"]:
			var found: bool = a.any(func(v: String): return v.begins_with(want))
			if not found:
				_mismatches.append("%s: GDScript checker misses %s (%s)" % [c["name"], want, a])
			_checks += 1
	_report("hand-built cases")


func test_event_checks_identical() -> void:
	var ctx := NumCtx.f64()
	var refl := {"type": "reflection", "interactive": true}
	var inv := {"type": "inversion", "interactive": true}
	var term := {"type": "terminal", "interactive": false}
	var sc := BeamScene.from_defs(ctx, [
		{"id": 1, "line": true, "start": [300.0, -500.0], "end": [300.0, 500.0], "via": [300.0, 0.0], "full": false, "left": refl, "right": refl},
		{"id": 2, "line": false, "start": [700.0, 0.0], "end": [500.0, 0.0], "via": [600.0, -100.0], "full": false, "left": inv, "right": inv},
		{"id": 3, "line": true, "start": [-900.0, -900.0], "end": [-900.0, 900.0], "via": [-900.0, 0.0], "full": false, "left": term, "right": term},
		{"id": 4, "line": true, "start": [900.0, -900.0], "end": [900.0, 900.0], "via": [900.0, 0.0], "full": false, "left": term, "right": term},
		{"id": 5, "line": true, "start": [-900.0, -900.0], "end": [900.0, -900.0], "via": [0.0, -900.0], "full": false, "left": term, "right": term},
		{"id": 6, "line": true, "start": [-900.0, 900.0], "end": [900.0, 900.0], "via": [0.0, 900.0], "full": false, "left": term, "right": term},
	])
	var rng := RandomNumberGenerator.new()
	rng.seed = 98
	for i in 40:
		var ang := rng.randf_range(-PI, PI)
		var ev: Array = BeamSolver.trace_direction(ctx, sc, ctx.cn(rng.randf_range(-400, 400), rng.randf_range(-400, 400)), ctx.cn(cos(ang), sin(ang)), [], null, BeamSolver.PHYSICAL)
		var label := "events %d" % i
		_note(label + " law", GdInvariantChecker.check_effect_law(ev, sc), InvariantChecker.check_effect_law(ev, sc))
		_note(label + " on_beam", GdInvariantChecker.check_event_on_beam(ev, sc), InvariantChecker.check_event_on_beam(ev, sc))
		_note(label + " order", GdInvariantChecker.check_beam_order(ev), InvariantChecker.check_beam_order(ev))
		var plan := [BeamSolverPlanItem.make(1, i % 2 == 0)]
		_note(label + " plan", GdInvariantChecker.check_plan_effects_applied_events(ev, plan, true), InvariantChecker.check_plan_effects_applied_events(ev, plan, true))
		# corruptions: an offset point, a rotated beam
		if ev.size() > 1 and ev[0].point != null:
			ev[0].point = ev[0].point.add(ctx.cn(0.3, -0.2))
			_note(label + " offset", GdInvariantChecker.check_event_on_beam(ev, sc), InvariantChecker.check_event_on_beam(ev, sc))
			for k in ev.size() - 1:
				if ev[k].kind == "hit" and ev[k].applied and not ev[k].terminal:
					var b: HPBeam = ev[k + 1].beam
					ev[k + 1].beam = HPBeam.make(b.p, b.tau.add(b.tau.mul_i().scale(ctx.num(0.01))).unit(), b.kappa)
					break
			_note(label + " rotated", GdInvariantChecker.check_effect_law(ev, sc), InvariantChecker.check_effect_law(ev, sc))
			_note(label + " order2", GdInvariantChecker.check_beam_order(ev), InvariantChecker.check_beam_order(ev))
	_report("event checks")


# ---------------------------------------------------------------- the sweep scenes

func _instantiate(path: String) -> Node:
	Surface.reset_id_counter()
	var scene: Node = load(path).instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	return scene


func _kernel_combo(scene: Node, p: Vector2, c: Vector2, plan: Array) -> Array:
	var renderer: Node = scene.get_node_or_null("PathRenderer")
	var flags: Dictionary = renderer.get_flags() if renderer else {}
	var defs := SceneDefs.defs_from_surfaces(scene.surfaces)
	var infos := InvariantChecker.surface_infos(scene.surfaces, flags)
	for f in SweepKernel.run(defs, infos, PackedVector2Array([p, c]), [plan], flags, 0, 1, false):
		if f.player == p and f.cursor == c:
			return f.violations
	return []


func test_sweep_scenes_identical() -> void:
	var stride := 41
	var s := OS.get_environment("DIFF_STRIDE")
	if s.is_valid_int() and int(s) > 0:
		stride = int(s)
	var scenes := SweepPlans.discover_scenes(SweepPlans.TEST_LEVELS_DIR)
	for path in scenes:
		var scene := _instantiate(path)
		await get_tree().process_frame
		var runner := SweepRunner.new().configure(5, 10, 42)
		var has_room: bool = "room_rect" in scene and "build_room" in scene and scene.build_room
		var has_blocks: bool = "block_lines" in scene and scene.block_lines.size() > 0
		if has_room or has_blocks:
			var rect: Rect2 = scene.room_rect if "room_rect" in scene else Rect2(560, 240, 800, 600)
			runner.set_bounds(rect.position + Vector2(10, 10), rect.position + rect.size - Vector2(10, 10))
		var positions := runner.build_positions(runner._extract_points_of_interest(scene))
		var plans: Array = [[]]
		if path in SweepPlans.plan_scenes():
			plans.append_array(SweepPlans.reflective_plans(scene).slice(0, 2))
			plans.append_array(SweepPlans.inversion_plans(scene).slice(0, 1))
		var gd := GdInvariantChecker.new()
		gd.setup(scene)
		var drv := InvariantChecker.new()
		drv.setup(scene)
		# scenes without a GameManager: setup() installs the checker's plan on the renderer; share one
		gd._plan = drv._plan
		var idx := 0
		for plan in plans:
			for p in positions:
				for c in positions:
					var i := idx
					idx += 1
					if i % stride != 0:
						continue
					var label := "%s p=%s c=%s plan=%d" % [path.get_file().get_basename(), p, c, plan.size()]
					var a: Array = gd.check_all(p, c, plan)
					var b: Array = drv.check_all(p, c, plan)
					_note(label + " live", a, b)
					if p != c:
						_note(label + " kernel", a, _kernel_combo(scene, p, c, plan))
	_report("sweep scenes")
