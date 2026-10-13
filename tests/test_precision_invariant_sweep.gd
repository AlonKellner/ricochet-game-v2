extends GutTest

# Invariant sweep of the inversion grid through PathRenderer (beam solver).
# Runs where the renderer reports a solver other than "beam" (no trace pair,
# e.g. degenerate inputs) are counted but not asserted here.

const SCENE := "res://scenes/playground/inversion_grid.tscn"

var _scene: Node

func before_each():
	BeamTracer.clear_cache()
	Surface.reset_id_counter()
	_scene = load(SCENE).instantiate()
	_scene.gravity = Vector2.ZERO
	add_child_autofree(_scene)
	await get_tree().process_frame

func _report(failures: Array, label: String) -> void:
	if failures.is_empty():
		pass_test("%s: no violations" % label)
		return
	var counts := {}
	for f in failures:
		var k: String = String(f.violation).get_slice(":", 0)
		counts[k] = counts.get(k, 0) + 1
	var report := "%s: %d violations %s\\n" % [label, failures.size(), str(counts)]
	for f in failures.slice(0, 12):
		report += "  plan=%s player=%s cursor=%s: %s\\n" % [f.plan, f.player, f.cursor, f.violation]
	fail_test(report)

# User report: no plan, beam misses everything; the post-cursor continuation
# must stay on the straight line (it bent towards the screen corner).
func test_no_hit_continuation_is_collinear():
	var player := Vector2(620, 540)
	var cursor := Vector2(579.6038, 831.54)
	_scene.get_node("Player").global_position = player
	_scene.get_node("Cursor").global_position = cursor
	var renderer := _scene.get_node("PathRenderer")
	renderer._compute_trace()
	assert_eq(renderer.last_solver, "beam")
	var dir := (cursor - player).normalized()
	for path in [renderer.get_traced_path(), renderer.get_planned_path()]:
		for s in path.steps:
			if is_inf(s.start.x) or is_inf(s.end.x):
				continue
			var off: float = absf((s.end - player).normalized().cross(dir))
			assert_lt(off, 1e-5, "step %s -> %s stays on the aim line" % [s.start, s.end])

# User report 2: a cancelling re-hit (circle 7 twice) must restore the
# straight beam exactly; the beam then continues forward along the aim line,
# through infinity, and returns from behind -- never "back" from h2 to h1.
func test_cancelling_rehit_restores_straight_beam():
	var player := Vector2(620.0, 676.6658)
	var cursor := Vector2(665.6934, 698.2932)
	var renderer := _scene.get_node("PathRenderer")
	var dir := (cursor - player).normalized()
	for dy in [0.0, 0.0005, -0.0005, 0.01, -0.01]:
		BeamTracer.clear_cache()
		_scene.get_node("Player").global_position = player
		_scene.get_node("Cursor").global_position = cursor + Vector2(0, dy)
		renderer._compute_trace()
		assert_eq(renderer.last_solver, "beam")
		var d := (cursor + Vector2(0, dy) - player).normalized()
		for path in [renderer.get_traced_path(), renderer.get_planned_path()]:
			for s in path.steps:
				if s.frame_id != TraceStep.IDENTITY_FRAME or is_inf(s.start.x) or is_inf(s.end.x):
					continue
				if s.start == s.end:
					continue
				assert_gt((s.end - s.start).dot(d), 0.0,
					"dy=%s: straight step %s -> %s travels forward along the aim" % [dy, s.start, s.end])

# A step the renderer draws as a straight chord (arc too large to draw) must be
# the path's own arc, i.e. run between its endpoints: via strictly inside the
# chord's arc <=> the inscribed angle at via is not acute.
func _chord_violations(path: TracePath) -> Array:
	var bad: Array = []
	for s in path.steps:
		if not s.is_arc_step or is_inf(s.start.x) or is_inf(s.end.x) or is_inf(s.via.x):
			continue
		if VisualConverter.is_arc(s.start, s.via, s.end):
			continue
		if (s.start - s.via).dot(s.end - s.via) > 0.0:
			bad.append("chord drawn for a long-way arc %s -> %s (via %s)" % [s.start, s.end, s.via])
	return bad

# User report 3: plan 7,4,1,3; the arc from circle 1 to circle 3 goes the long
# way round (out of screen). With radius > MAX_ARC_RADIUS it was drawn as the
# chord, i.e. through circles 2 and 3 into the wrong side.
func test_huge_long_way_arc_not_drawn_as_chord():
	var renderer := _scene.get_node("PathRenderer")
	var gm := _scene.get_node("GameManager")
	gm.plan.clear()
	for sid in [7, 4, 1, 3]:
		gm.plan.add_entry(sid, Side.Value.LEFT)
	for cur in [Vector2(1246.298, 440.8163), Vector2(1245.297, 440.8163)]:
		BeamTracer.clear_cache()
		_scene.get_node("Player").global_position = Vector2(620.0, 679.9991)
		_scene.get_node("Cursor").global_position = cur
		renderer._compute_trace()
		assert_eq(renderer.last_solver, "beam")
		for path in [renderer.get_planned_path(), renderer.get_traced_path()]:
			assert_eq(_chord_violations(path), [], "cursor %s" % cur)
			# the step leaving the circle-1 hit heads up and out of screen
			for i in path.steps.size():
				var st: TraceStep = path.steps[i]
				if st.surface_id == 1 and i + 1 < path.steps.size():
					var nxt: TraceStep = path.steps[i + 1]
					assert_lt(nxt.end.y, nxt.start.y, "cursor %s: leaves circle 1 upwards (%s -> %s)" % [cur, nxt.start, nxt.end])
					break

func _sweep(plans: Array, positions: Array) -> Dictionary:
	var checker := InvariantChecker.new()
	checker.setup(_scene)
	var renderer := _scene.get_node("PathRenderer")
	var failures: Array = []
	var fallbacks := 0
	var runs := 0
	for ids in plans:
		var plan: Array = ids.map(func(i): return PlanManager.PlanEntry.new(i, Side.Value.LEFT))
		for pi in positions.size():
			if not SweepRunner.in_shard(pi):
				continue
			var p: Vector2 = positions[pi]
			for c in positions:
				if p == c:
					continue
				BeamTracer.clear_cache()
				var vs := checker.check_all(p, c, plan)
				runs += 1
				if renderer.last_solver != "beam":
					fallbacks += 1
					continue
				for v in vs:
					failures.append({"plan": str(ids), "player": p, "cursor": c, "violation": v})
				for path in [renderer.get_planned_path(), renderer.get_traced_path()]:
					for v in _chord_violations(path):
						failures.append({"plan": str(ids), "player": p, "cursor": c, "violation": "LONG-ARC-CHORD: " + v})
	return {"failures": failures, "fallbacks": fallbacks, "runs": runs}

func test_sweep_no_plan():
	var runner := SweepRunner.new().configure(5, 10, 42)
	var positions := runner.build_positions(runner._extract_points_of_interest(_scene))
	var r := _sweep([[]], positions)
	gut.p("inversion_grid no plan: %d runs, %d without a beam pair (degenerate inputs)" % [r.runs, r.fallbacks])
	_report(r.failures, "inversion_grid no plan")

func test_sweep_with_plans():
	var plans := [[7], [4, 5], [7, 4, 1], [5, 5], [1, 2, 3, 6], [7, 4, 1, 3, 6, 6, 6]]
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	var positions: Array[Vector2] = []
	while positions.size() < 10:
		var p := Vector2(rng.randf_range(40, 1880), rng.randf_range(40, 1040))
		var inside := false
		for cx in [760, 960, 1160]:
			for cy in [380, 540, 700]:
				if p.distance_to(Vector2(cx, cy)) < 62:
					inside = true
		if not inside:
			positions.append(p)
	var r := _sweep(plans, positions)
	gut.p("inversion_grid with plans: %d runs, %d without a beam pair" % [r.runs, r.fallbacks])
	_report(r.failures, "inversion_grid with plans")
