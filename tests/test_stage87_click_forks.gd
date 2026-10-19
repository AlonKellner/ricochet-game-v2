extends GutTest

# Stage 87: forks of projective back-propagation, settled at planning time by
# the player's click. A circle projective has two candidate hit points (both
# exit lines pass through the target; one may reach it only through
# infinity): the candidate nearest the click is taken -- by default the
# segment's midpoint (its via point). No search: one deterministic path.
# Expected values: the mpmath oracle (trace2) --
# bench/fixtures/stage87_click_forks.json (gen_fixtures_stage87_clicks.py),
# including the oracle's back-propagated hit points ("hits").

const FIXTURE := "res://bench/fixtures/stage87_click_forks.json"
const Rules := preload("res://tests/test_stage81_trace_rules.gd")
const POINT_TOL := 1e-9

var _fx: Dictionary = {}

func before_all():
	var f := FileAccess.open(FIXTURE, FileAccess.READ)
	for x in JSON.parse_string(f.get_as_text())["fixtures"]:
		_fx[x["name"]] = x

func _scene(ctx: NumCtx, name: String) -> BeamScene:
	return Rules.scene_of(ctx, _fx[name]["surfaces"])

func _walk(name: String, ctx: NumCtx) -> Array:
	var fx: Dictionary = _fx[name]
	var scene := _scene(ctx, name)
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

# Every fixture: both tiers match the oracle's events.
func test_all_click_fixtures_match_oracle():
	assert_gt(_fx.size(), 20)
	for name in _fx:
		for ctx: NumCtx in [NumCtx.f64(), NumCtx.with_limbs(4)]:
			if _check(_walk(name, ctx), _fx[name]["expected"], "%s [%s]" % [name, ctx.name()]):
				pass_test("%s [%s]" % [name, ctx.name()])

# The solver's back-propagated hit points are the oracle's.
func test_hits_match_oracle():
	var ctx := NumCtx.f64()
	for name in _fx:
		var fx: Dictionary = _fx[name]
		var chain := BeamSolver.anchor_chain(_scene(ctx, name), ctx.cn(fx["player"][0], fx["player"][1]),
			ctx.cn(fx["cursor"][0], fx["cursor"][1]), Rules.plan_of(fx["plan"]))
		assert_not_null(chain, name)
		if chain == null:
			continue
		assert_eq(chain.hits.size(), fx["hits"].size(), name + ": projective hit count")
		for k in fx["hits"]:
			var want := Vector2(String(fx["hits"][k][0]).to_float(), String(fx["hits"][k][1]).to_float())
			var h = chain.hits.get(int(k))
			assert_not_null(h, "%s: hit %s" % [name, k])
			if h != null:
				assert_almost_eq(Vector2(h.fx(), h.fy()), want, Vector2(1e-9, 1e-9), "%s: hit %s" % [name, k])

func test_fast_twin_click_forks():
	var ctx := NumCtx.f64()
	for name in _fx:
		var fx: Dictionary = _fx[name]
		var gen := _walk(name, ctx)
		var sc := Rules.fast_scene_of(fx["surfaces"])
		var plan := Rules.plan_of(fx["plan"])
		var out = FastSolver.planned(sc, PackedFloat64Array(fx["player"]), PackedFloat64Array(fx["cursor"]), plan) \
			if fx["mode"] == "planned" else FastSolver.physical(sc, PackedFloat64Array(fx["player"]), PackedFloat64Array(fx["cursor"]), plan)
		assert_eq(out.events.size(), gen.size(), "%s: event count" % name)
		for i in mini(out.events.size(), gen.size()):
			var a: BeamSolverEvent = out.events[i]
			var b: BeamSolverEvent = gen[i]
			assert_eq([a.kind, a.surface_id, a.left, a.applied], [b.kind, b.surface_id, b.left, b.applied], "%s event %d" % [name, i])
			if a.point != null and b.point != null:
				assert_eq([a.point.fx(), a.point.fy()], [b.point.fx(), b.point.fy()], "%s event %d: bit-identical" % [name, i])

func _hit(name: String, j: int) -> Vector2:
	var h: Array = _fx[name]["hits"][str(j)]
	return Vector2(String(h[0]).to_float(), String(h[1]).to_float())

# The click picks the hit point. The near branch is realized at once: the
# projective fires there and the cursor is reached. Aimed straight at the far
# one, the beam would meet the circle first on the planned side and fire
# there; Stage 111 realizes the plan the other way round instead (GAME_SPEC
# 13.1, R2: the orientation whose planned walk reaches the cursor): after an
# unplanned wrap the clicked point is the first crossing met, the projective
# fires there and the cursor is reached -- while the physical shot, aimed away,
# ends at the room's wall.
func test_click_picks_each_branch_circle_normal():
	var near := _hit("cn_inside_click_near_planned", 0)
	var far := _hit("cn_inside_click_far_planned", 0)
	assert_gt(near.distance_to(far), 100.0, "two branches")
	for mode in ["planned", "physical"]:
		var ev := _walk("cn_inside_click_near_%s" % mode, NumCtx.f64())
		var proj := ev.filter(func(e): return e.kind == "hit" and e.projective and e.applied)
		assert_gt(proj.size(), 0)
		if proj.size() > 0:
			assert_almost_eq(Vector2(proj[0].point.fx(), proj[0].point.fy()), near, Vector2(1e-6, 1e-6),
				"near %s: the projective fires at the clicked candidate" % mode)
		assert_true(ev.any(func(e): return e.kind == "cursor"), "near %s: the cursor is reached" % mode)
	var evf := _walk("cn_inside_click_far_planned", NumCtx.f64())
	var kinds := evf.map(func(e): return e.kind)
	var projf := evf.filter(func(e): return e.kind == "hit" and e.projective and e.applied)
	assert_gt(projf.size(), 0, "far planned: the projective fires")
	if projf.size() > 0:
		assert_almost_eq(Vector2(projf[0].point.fx(), projf[0].point.fy()), far, Vector2(1e-6, 1e-6),
			"far planned: the other way round, the projective fires at the clicked candidate")
		assert_lt(kinds.find("infinity"), evf.find(projf[0]), "far planned: after the wrap")
	assert_true(kinds.has("cursor"), "far planned: the cursor is reached")
	var evp := _walk("cn_inside_click_far_physical", NumCtx.f64())
	assert_true(evp.size() > 0 and evp[0].kind == "hit" and evp[0].terminal, "far physical: aimed away, the shot ends at the wall")
	assert_false(evp.any(func(e): return e.kind == "cursor"), "far physical: the cursor is not reached")

func test_click_picks_each_branch_circle_directional():
	assert_gt(_hit("cd_inside_click_left_planned", 0).distance_to(_hit("cd_inside_click_right_planned", 0)), 100.0)

# No click: the segment's midpoint is the default click.
func test_default_click_is_midpoint():
	var none := _hit("cn_inside_click_none_planned", 0)
	var near := _hit("cn_inside_click_near_planned", 0)
	var far := _hit("cn_inside_click_far_planned", 0)
	var via := Vector2(400.0, 150.0)
	var expect := near if near.distance_to(via) < far.distance_to(via) else far
	assert_eq(none, expect, "the candidate nearest the midpoint")

# Outside the circle the click still decides (the wrap-through-infinity
# candidate is a real path); back projectives too.
func test_click_decides_outside_and_back():
	assert_gt(_hit("cn_outside_click_near_direct_planned", 0).distance_to(_hit("cn_outside_click_near_wrap_planned", 0)), 100.0)
	assert_gt(_hit("cn_back_inside_click_near_planned", 0).distance_to(_hit("cn_back_inside_click_far_planned", 0)), 100.0)

# A line projective has one candidate: any click gives the same hit.
func test_line_projective_ignores_click():
	assert_eq(_hit("ln_click_a_planned", 0), _hit("ln_click_b_planned", 0))

# Provenance beats the click: the target on the carrier is never the hit.
# Stage 114: the target on the carrier is its own candidate, and the fork rule
# chooses -- clicked at the second entry's point, the first hit is that point
# (the beam meets the entry again after its loop); clicked at the antipode, the other.
func test_provenance_makes_the_target_its_own_candidate():
	assert_lt(_hit("cn_twice_clicked_target_planned", 0).distance_to(_hit("cn_twice_clicked_target_planned", 1)), 1e-6,
		"clicked at the second's point")
	assert_gt(_hit("cn_twice_clicked_other_planned", 0).distance_to(_hit("cn_twice_clicked_other_planned", 1)), 1.0,
		"clicked at the antipode")

# A fork after a mirror: the clicked branch is taken, realizable or not.
func test_fork_after_mirror_honors_click():
	var reached := func(n: String) -> bool:
		return _walk(n, NumCtx.f64()).any(func(e): return e.kind == "cursor")
	assert_true(reached.call("mirror_fork_click_near_planned"))
	assert_false(reached.call("mirror_fork_click_far_planned"), "the far branch does not reach -- shown, not hidden")

# Two forks, four click combinations: four hit pairs.
func test_two_forks_four_combinations():
	var pairs := {}
	for name in _fx:
		if name.begins_with("two_forks_"):
			pairs[str([_hit(name, 0), _hit(name, 1)])] = true
	assert_eq(pairs.size(), 4)

# Property sweep (no oracle): for random targets and clicks, the hit point is
# a candidate (its exit line passes through the target), it is the candidate
# nearest the click, and the provenance-excluded point is never picked.
func test_property_nearest_candidate():
	var ctx := NumCtx.f64()
	var defs := [{"id": 1, "line": false, "start": [700.0, 0.0], "end": [400.0, -150.0], "via": [400.0, 150.0], "full": true,
		"circle": {"center": [500.0, 0.0], "radius": 200.0},
		"left": {"type": "circle_normal_projection", "interactive": true, "back": false},
		"right": {"type": "circle_normal_projection", "interactive": true, "back": true}}]
	var sc := BeamScene.from_defs(ctx, defs)
	var surf: BeamSceneSurf = sc.by_id[1]
	var rng := RandomNumberGenerator.new()
	rng.seed = 87
	var c := Vector2(500, 0)
	for i in 300:
		var X := Vector2(rng.randf_range(100, 900), rng.randf_range(-400, 400))
		if X.distance_to(c) < 1.0:
			continue
		var click := Vector2(rng.randf_range(250, 750), rng.randf_range(-250, 250))
		var left := rng.randf() < 0.5
		var cands := BeamEffects.back_propagate_candidates(surf, left, ctx.cn(X.x, X.y))
		assert_eq(cands.size(), 2, "two candidates on the line through the center and X")
		var H = BeamEffects.back_propagate(surf, left, ctx.cn(X.x, X.y), false, click)
		var hv := Vector2(H.fx(), H.fy())
		var hx: float = H.fx() - 500.0
		var hy: float = H.fy()
		assert_almost_eq(sqrt(hx * hx + hy * hy), 200.0, 1e-9, "on the circle")
		assert_almost_eq(hx * (X.y) - hy * (X.x - 500.0), 0.0, 1e-6 * (X - c).length() * 200.0, "on the line through c and X")
		var others: Array = cands.map(func(k): return Vector2(k[0].fx(), k[0].fy()))
		for o: Vector2 in others:
			assert_true(hv.distance_to(click) <= o.distance_to(click) + 1e-9, "nearest the click")
		# no click: nearest the midpoint (via)
		var Hd = BeamEffects.back_propagate(surf, left, ctx.cn(X.x, X.y))
		var hd := Vector2(Hd.fx(), Hd.fy())
		for o: Vector2 in others:
			assert_true(hd.distance_to(Vector2(400, 150)) <= o.distance_to(Vector2(400, 150)) + 1e-9, "default: nearest the midpoint")
	# provenance (Stage 114): X on the carrier is its own candidate, exactly; the
	# click picks it or the antipode (a full circle: both on the segment)
	for i in 50:
		var a := rng.randf_range(0, TAU)
		var X := c + Vector2(cos(a), sin(a)) * 200.0
		var H = BeamEffects.back_propagate(surf, true, ctx.cn(X.x, X.y), true, X)
		assert_eq(Vector2(H.fx(), H.fy()), X, "clicked at X: X itself")
		var Ho = BeamEffects.back_propagate(surf, true, ctx.cn(X.x, X.y), true, c - (X - c))
		assert_gt(Vector2(Ho.fx(), Ho.fy()).distance_to(X), 300.0, "clicked at the antipode: the antipode")

# The game: a plan click is stored, survives undo, reaches the solver, and is
# part of the trace cache key.
func test_click_flows_through_the_game():
	var pm := PlanManager.new()
	pm.add_entry(9, Side.Value.LEFT, Vector2(945, 425))
	assert_eq(pm.entries[0].click, Vector2(945, 425), "stored")
	var copy := PlanManager.new()
	copy.restore_from(pm.entries)
	assert_eq(copy.entries[0].click, Vector2(945, 425), "kept by restore (checkpoint undo)")
	var cp := CheckpointData.new(Vector2.ZERO, Vector2.ZERO, GameState.new(), pm.entries, {})
	assert_eq(cp.plan_entries[0].click, Vector2(945, 425), "kept by checkpoints")
	var items := BeamTracer.plan_items(pm.entries)
	assert_eq(items[0].hint, Vector2(945, 425), "passed to the solver")
	pm.add_entry(5, Side.Value.LEFT)
	assert_null(BeamTracer.plan_items(pm.entries)[1].hint, "no click: no hint (the midpoint default)")
	# the cache key: the same plan clicked on the other half is another trace
	Surface.reset_id_counter()
	var scene: Node = load("res://scenes/playground/projective.tscn").instantiate()
	add_child_autofree(scene)
	await get_tree().process_frame
	var plan_ids := [[7, 1], [10, 1], [8, 1], [10, 0], [7, 0], [9, 0], [13, 0]]
	var mk := func(click9: Vector2) -> Array:
		return plan_ids.map(func(x): return PlanManager.PlanEntry.new(x[0], x[1], click9 if x[0] == 9 else Vector2(INF, INF)))
	var a := TestTrace.trace_pair(scene.surfaces, Vector2(842.1705, 878.0266), Vector2(146.1522, 559.0353), mk.call(Vector2(945, 425)))
	var b := TestTrace.trace_pair(scene.surfaces, Vector2(842.1705, 878.0266), Vector2(146.1522, 559.0353), mk.call(Vector2(460, 570)))
	assert_not_null(a)
	assert_not_null(b)
	if a != null and b != null:
		# Stage 114 (the fork rule): arc 9 is a semicircle, so both clicks take its
		# on-arc candidate and the traces are one (before, the other half's click
		# took the off-arc candidate: another trace, cached apart by the click)
		assert_eq(str(a.result.planned.map(func(e): return e.describe())), str(b.result.planned.map(func(e): return e.describe())),
			"the same walk for both clicks (the on-arc candidate)")
		for pair in [a, b]:
			var ce: Array = pair.result.planned.filter(func(e): return e.kind == "cursor")
			assert_eq(ce.size(), 1, "the click reaches the cursor")
			if ce.size() == 1:
				assert_almost_eq(Vector2(ce[0].point.fx(), ce[0].point.fy()), Vector2(146.1522, 559.0353), Vector2(1e-4, 1e-4), "exactly")

# Off the planned path the cursor is never "reached" at its projection: a
# cursor event lies on the cursor.
func test_cursor_event_only_at_the_cursor():
	for name in _fx:
		var fx: Dictionary = _fx[name]
		var cur := Vector2(fx["cursor"][0], fx["cursor"][1])
		for e in _walk(name, NumCtx.f64()):
			if e.kind == "cursor":
				assert_almost_eq(Vector2(e.point.fx(), e.point.fy()), cur, Vector2(1e-6, 1e-6), name + ": the cursor event is at the cursor")
