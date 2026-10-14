extends GutTest

# FastSolver (raw floats) must be a bit-identical twin of BeamSolver over
# F64Num, and its inline tangent-linear gains must match BeamSolver over DualNum.

const SAMPLE_EVERY := 5

func _scenarios() -> Array:
	PrecisionBench.use_corpus("")
	var names := PrecisionBench.list_scenarios()
	var out: Array = []
	for i in names.size():
		if i % SAMPLE_EVERY == 0 or names[i].begins_with("dump"):
			out.append(names[i])
	return out

func _inputs(name: String) -> Dictionary:
	var scn: Dictionary = PrecisionBench.load_json("%s/%s.json" % [PrecisionBench.SCN_DIR, name])
	return {
		"defs": PrecisionBench.scene_defs(scn),
		"player": PackedFloat64Array([float(scn["player"][0]), float(scn["player"][1])]),
		"cursor": PackedFloat64Array([float(scn["cursor"][0]), float(scn["cursor"][1])]),
		"plan": PrecisionBench.plan_items(scn),
	}

func _same_events(a: Array, b: Array, label: String) -> void:
	assert_eq(a.size(), b.size(), "%s: event count" % label)
	for i in mini(a.size(), b.size()):
		var x: BeamSolverEvent = a[i]
		var y: BeamSolverEvent = b[i]
		var same: bool = x.kind == y.kind and x.surface_id == y.surface_id and x.left == y.left and x.applied == y.applied \
			and x.on_segment == y.on_segment and x.rehit == y.rehit
		assert_true(same, "%s: event %d structure (%s vs %s)" % [label, i, x.describe(), y.describe()])
		if not same:
			return
		if x.point != null and y.point != null:
			assert_eq(x.point.fx(), y.point.fx(), "%s: event %d x bit-identical" % [label, i])
			assert_eq(x.point.fy(), y.point.fy(), "%s: event %d y bit-identical" % [label, i])

func _scene(ctx: NumCtx, inp: Dictionary) -> BeamScene:
	return BeamScene.from_defs(ctx, inp["defs"])

func test_planned_bit_identical_to_generic_f64():
	for name in _scenarios():
		var inp := _inputs(name)
		var ctx := NumCtx.f64()
		var gen := BeamSolver.planned(ctx, _scene(ctx, inp),
			ctx.cn(inp["player"][0], inp["player"][1]), ctx.cn(inp["cursor"][0], inp["cursor"][1]), inp["plan"])
		var fast := FastSolver.planned(FastSolverScene.from_defs(inp["defs"]), inp["player"], inp["cursor"], inp["plan"])
		assert_gt(gen.size(), 0, name + ": planned events")
		_same_events(fast.events, gen, name + " planned")

func test_physical_bit_identical_to_generic_f64():
	for name in _scenarios():
		var inp := _inputs(name)
		var ctx := NumCtx.f64()
		var gen := BeamSolver.physical(ctx, _scene(ctx, inp),
			ctx.cn(inp["player"][0], inp["player"][1]), ctx.cn(inp["cursor"][0], inp["cursor"][1]), inp["plan"], 16)
		var fast := FastSolver.physical(FastSolverScene.from_defs(inp["defs"]), inp["player"], inp["cursor"], inp["plan"], 16)
		assert_gt(gen.size(), 0, name + ": physical events")
		_same_events(fast.events, gen, name + " physical")

func test_gains_match_dual_numbers():
	var compared := 0
	for name in _scenarios():
		var inp := _inputs(name)
		var ctx := NumCtx.dual()
		var dual := BeamSolver.physical(ctx, _scene(ctx, inp),
			ctx.cn(inp["player"][0], inp["player"][1]), ctx.cn(inp["cursor"][0], inp["cursor"][1]), inp["plan"], 16)
		var fast := FastSolver.physical(FastSolverScene.from_defs(inp["defs"]), inp["player"], inp["cursor"], inp["plan"], 16)
		for i in mini(dual.size(), fast.gain.size()):
			if dual[i].point == null or not dual[i].seeded:
				continue
			var g := Vector2(dual[i].point.re.d, dual[i].point.im.d).length()
			var f: float = fast.gain[i]
			assert_almost_eq(f, g, 1e-3 * maxf(1.0, g), "%s: gain %d" % [name, i])
			compared += 1
	assert_gt(compared, 100)
