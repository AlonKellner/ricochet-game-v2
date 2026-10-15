extends GutTest
## Stage 98: every invariant check detects the hand-corrupted trace built for
## it (tests/native/invariant_cases.gd) and accepts the clean one. Runs the
## native kernel (InvariantKernel) directly on the cases' data.

const Cases := preload("res://tests/native/invariant_cases.gd")


static func kinds(violations: Array) -> Array:
	return violations.map(func(v: String): return v.get_slice(":", 0))


func _check(c: Dictionary) -> Array:
	var surfaces: Array = c["surfaces"]
	return InvariantKernel.check_all(c["player"], c["cursor"], c["physical"], c["planned"], c["typed"], null,
		InvariantChecker.surface_infos(surfaces, {}), SceneDefs.defs_from_surfaces(surfaces), {}, [], true, false)


func test_clean_trace_passes() -> void:
	for c in Cases.cases():
		if c["name"] == "clean":
			assert_eq(_check(c), [], "clean trace")


func test_each_corruption_is_detected() -> void:
	var seen := {}
	for c in Cases.cases():
		var found := kinds(_check(c))
		for want in c["expect"]:
			assert_true(want in found, "%s: expected %s, got %s" % [c["name"], want, found])
			seen[want] = true
	# the data-only checks all have a detecting case
	for k in ["UX7", "NOGAPS", "S16", "GREEN-FROM-PLAYER", "ORIGIN-NOT-REHIT", "SINGLE-DIVERGENCE", "NO-POST-CURSOR-DIVERGENCE",
			"PHYSICAL-PREVIEW-MATCH", "PHYSICAL-CONTINUITY", "SOLID-PATH-TO-CURSOR", "TRACE-ENDS", "PHYSICS-COMPLIANCE", "VISUAL-ON-CARRIER"]:
		assert_true(seen.has(k), "%s has a detecting case" % k)


func _scene(ctx: NumCtx) -> BeamScene:
	var refl := {"type": "reflection", "interactive": true}
	var term := {"type": "terminal", "interactive": false}
	return BeamScene.from_defs(ctx, [
		{"id": 1, "line": true, "start": [300.0, -500.0], "end": [300.0, 500.0], "via": [300.0, 0.0], "full": false, "left": refl, "right": refl},
		{"id": 3, "line": true, "start": [-900.0, -900.0], "end": [-900.0, 900.0], "via": [-900.0, 0.0], "full": false, "left": term, "right": term},
		{"id": 4, "line": true, "start": [900.0, -900.0], "end": [900.0, 900.0], "via": [900.0, 0.0], "full": false, "left": term, "right": term},
	])


func test_event_checks_detect_corrupted_events() -> void:
	var ctx := NumCtx.f64()
	var sc := _scene(ctx)
	var ev: Array = BeamSolver.trace_direction(ctx, sc, ctx.cn(0, -40), ctx.cn(1, 0.0625), [], null, BeamSolver.PHYSICAL)
	assert_gte(ev.size(), 2)
	assert_eq(InvariantChecker.check_effect_law(ev, sc), [] as Array[String])
	assert_eq(InvariantChecker.check_event_on_beam(ev, sc), [] as Array[String])
	assert_eq(InvariantChecker.check_beam_order(ev), [] as Array[String])
	# a point off its beam and off the carrier
	ev[0].point = ev[0].point.add(ctx.cn(0.5, 0.5))
	var found := kinds(InvariantChecker.check_event_on_beam(ev, sc))
	assert_true("EVENT-ON-BEAM" in found and "HITPOINT-ON-CARRIER" in found, str(found))
	# the plan's effects: a plan the trace did not apply
	var plan := [BeamSolverPlanItem.make(1, true)]
	assert_eq(kinds(InvariantChecker.check_plan_effects_applied_events(ev, plan, true)), ["PLAN-EFFECTS"])
