extends GutTest

# Stage 83: invariants on beam-solver events (no legacy frames). Each new
# check must accept a valid trace and detect a hand-corrupted one.

func _scene(ctx: NumCtx) -> BeamScene:
	var refl := {"type": "reflection", "interactive": true}
	var inv := {"type": "inversion", "interactive": true}
	var term := {"type": "terminal", "interactive": false}
	return BeamScene.from_defs(ctx, [
		{"id": 1, "line": true, "start": [300.0, -500.0], "end": [300.0, 500.0], "via": [300.0, 0.0], "full": false, "left": refl, "right": refl},
		{"id": 2, "line": false, "start": [700.0, 0.0], "end": [500.0, 0.0], "via": [600.0, -100.0], "full": false, "left": inv, "right": inv},
		{"id": 3, "line": true, "start": [-900.0, -900.0], "end": [-900.0, 900.0], "via": [-900.0, 0.0], "full": false, "left": term, "right": term},
		{"id": 4, "line": true, "start": [900.0, -900.0], "end": [900.0, 900.0], "via": [900.0, 0.0], "full": false, "left": term, "right": term},
		{"id": 5, "line": true, "start": [-900.0, -900.0], "end": [900.0, -900.0], "via": [0.0, -900.0], "full": false, "left": term, "right": term},
		{"id": 6, "line": true, "start": [-900.0, 900.0], "end": [900.0, 900.0], "via": [0.0, 900.0], "full": false, "left": term, "right": term},
	])

func _events(scene: BeamScene, ctx: NumCtx) -> Array:
	return BeamSolver.trace_direction(ctx, scene, ctx.cn(0, -40), ctx.cn(1, 0.0625), [], null, BeamSolver.Mode.PHYSICAL)

func test_valid_trace_has_no_violations():
	var ctx := NumCtx.f64()
	var sc := _scene(ctx)
	var ev := _events(sc, ctx)
	assert_gte(ev.size(), 2, "mirror, then the wall")
	assert_eq(InvariantChecker.check_effect_law(ev, sc), [] as Array[String])
	assert_eq(InvariantChecker.check_event_on_beam(ev, sc), [] as Array[String])
	assert_eq(InvariantChecker.check_beam_order(ev), [] as Array[String])

func test_EFFECT_LAW_detects_wrong_tangent():
	var ctx := NumCtx.f64()
	var sc := _scene(ctx)
	var ev := _events(sc, ctx)
	# rotate the beam after the first applied effect
	for i in ev.size() - 1:
		if ev[i].kind == "hit" and ev[i].applied and not ev[i].terminal:
			var b: HPBeam = ev[i + 1].beam
			ev[i + 1].beam = HPBeam.make(b.p, b.tau.add(b.tau.mul_i().scale(ctx.num(0.01))).unit(), b.kappa)
			break
	assert_gt(InvariantChecker.check_effect_law(ev, sc).size(), 0, "wrong outgoing tangent detected")

func test_EVENT_ON_BEAM_detects_offset_point():
	var ctx := NumCtx.f64()
	var sc := _scene(ctx)
	var ev := _events(sc, ctx)
	ev[0].point = ev[0].point.add(ctx.cn(0.5, 0.5))
	assert_gt(InvariantChecker.check_event_on_beam(ev, sc).size(), 0, "offset point detected")

func test_BEAM_ORDER_detects_reordered_events():
	var ctx := NumCtx.f64()
	var sc := _scene(ctx)
	# a beam with two passes before an effect: swap them
	var pass_cfg := {"type": "pass", "interactive": false}
	var term := {"type": "terminal", "interactive": false}
	var s2 := BeamScene.from_defs(ctx, [
		{"id": 1, "line": true, "start": [100.0, -500.0], "end": [100.0, 500.0], "via": [100.0, 0.0], "full": false, "left": pass_cfg, "right": pass_cfg},
		{"id": 2, "line": true, "start": [200.0, -500.0], "end": [200.0, 500.0], "via": [200.0, 0.0], "full": false, "left": pass_cfg, "right": pass_cfg},
		{"id": 3, "line": true, "start": [300.0, -500.0], "end": [300.0, 500.0], "via": [300.0, 0.0], "full": false, "left": term, "right": term},
	])
	var ev := BeamSolver.trace_direction(ctx, s2, ctx.cn(0, 0), ctx.cn(1, 0), [], null, BeamSolver.Mode.PHYSICAL)
	assert_eq(InvariantChecker.check_beam_order(ev), [] as Array[String])
	var tmp = ev[0]
	ev[0] = ev[1]
	ev[1] = tmp
	assert_gt(InvariantChecker.check_beam_order(ev).size(), 0, "reordered events detected")

func test_PLAN_EFFECTS_APPLIED_by_surface_ids():
	var ctx := NumCtx.f64()
	var sc := _scene(ctx)
	var ev := _events(sc, ctx)
	var applied: Array = []
	for e in ev:
		if e.kind == "hit" and e.applied and not e.terminal:
			applied.append(BeamSolver.PlanItem.make(e.surface_id, e.left))
	assert_eq(InvariantChecker.check_plan_effects_applied_events(ev, applied, true), [] as Array[String])
	var wrong: Array = [BeamSolver.PlanItem.make(99, true)]
	assert_gt(InvariantChecker.check_plan_effects_applied_events(ev, wrong, true).size(), 0, "plan / applied mismatch detected")

func test_DIRECTION_ONLY_without_legacy():
	var src := FileAccess.open("res://scripts/testing/invariant_checker.gd", FileAccess.READ).get_as_text()
	var a := src.find("func check_DIRECTION_ONLY")
	assert_gt(a, -1)
	# the body ends at the next top-level line (non-indented, non-comment)
	var m := RegEx.create_from_string("\\n[^\\s#]").search(src, a + 10)
	var b: int = m.get_start() if m != null else src.length()
	var body := src.substr(a, b - a)
	# whole identifiers only (BeamTracer is the beam solver's adapter)
	assert_null(RegEx.create_from_string("(?<![A-Za-z_])Tracer\\.").search(body), "DIRECTION-ONLY does not call the legacy Tracer")
	assert_null(RegEx.create_from_string("(?<![A-Za-z_])Planner\\.").search(body), "DIRECTION-ONLY does not call the legacy Planner")
