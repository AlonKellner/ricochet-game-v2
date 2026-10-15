extends GutTest

# The invariant sweeps' harness: plans reach the solver in every scene, and
# the checker's cached scene description matches the scene.

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

func _scene(scene_name: String) -> Node:
	var scene: Node = load("res://scenes/test_levels/%s.tscn" % scene_name).instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	return scene

# room_with_inversion has no GameManager: the checker must still apply the plan
func test_checker_applies_plan_without_game_manager():
	var scene := _scene("room_with_inversion")
	await get_tree().process_frame
	assert_null(scene.get_node_or_null("GameManager"), "precondition: no GameManager")
	var inv: Surface = null
	for s: Surface in scene.surfaces:
		var cfg: SideConfig = s.active_side_config(Side.Value.LEFT, GameState.new())
		if cfg != null and cfg.effect is CircleInversionEffect:
			inv = s
	assert_not_null(inv)
	var checker := InvariantChecker.new()
	checker.setup(scene)
	var plan := [PlanManager.PlanEntry.new(inv.id, Side.Value.LEFT)]
	var player := Vector2(700, 400)
	var cursor := Vector2(1200, 700)
	checker.check_all(player, cursor, plan)
	var r: PrecisionLadderResult = scene.get_node("PathRenderer").last_beam_result
	assert_not_null(r)
	# the planned trace consumed the plan entry: an applied hit on the inversion
	var applied := false
	for e: BeamSolverEvent in r.planned:
		if e.kind == "hit" and e.surface_id == inv.id and e.applied:
			applied = true
	assert_true(applied, "the plan reached the solver")
	# and it is the same solve the game runs with that plan
	var pair := TestTrace.trace_pair(scene.surfaces, player, cursor, plan)
	assert_eq(r.planned.size(), pair.result.planned.size())
	# a later combo without a plan clears it
	checker.check_all(player, cursor, [])
	r = scene.get_node("PathRenderer").last_beam_result
	var bare := TestTrace.trace_pair(scene.surfaces, player, cursor, [])
	assert_eq(r.planned.size(), bare.result.planned.size(), "no stale plan")
	assert_eq(r.planned.map(func(e): return e.applied), bare.result.planned.map(func(e): return e.applied), "no stale plan")

func test_checker_scene_cache_matches_scene():
	var scene := _scene("three_mirrors")
	await get_tree().process_frame
	var checker := InvariantChecker.new()
	checker.setup(scene)
	checker.check_all(Vector2(700, 400), Vector2(1200, 700))
	assert_eq(str(checker._defs()), str(SceneDefs.defs_from_surfaces(scene.surfaces)))
	assert_eq(checker._beam_scene().surfaces.size(), scene.surfaces.size())

# DIRECTION-ONLY's reference uses the physical-only ladder: identical to the
# physical half of solve(), with the same sensitivities
func test_solve_physical_equals_solve():
	var scene := _scene("three_mirrors")
	await get_tree().process_frame
	var defs := SceneDefs.defs_from_surfaces(scene.surfaces)
	var fsc := FastSolverScene.from_defs(defs)
	var emit := FeatureFlags.infinity_event()
	fsc.scene.emit_infinity = emit
	for pc in [[Vector2(700, 400), Vector2(1200, 700)], [Vector2(600, 300), Vector2(900, 800)], [Vector2(1300, 500), Vector2(650, 350)]]:
		var p: Vector2 = pc[0]
		var c: Vector2 = pc[1]
		var full := PrecisionLadder.solve(defs, [p.x, p.y], [c.x, c.y], [], BeamSolver.MAX_HITS, BeamSolver.MAX_HITS, BeamTracer.TOL(), false, fsc, {}, emit)
		var phys := PrecisionLadder.solve_physical(defs, [p.x, p.y], [c.x, c.y], [], BeamSolver.MAX_HITS, BeamTracer.TOL(), fsc, {}, emit)
		assert_eq(phys.physical.map(func(e): return e.describe()), full.physical.map(func(e): return e.describe()))
		assert_eq(phys.physical_err, full.physical_err)
		assert_eq(phys.physical_tier, full.physical_tier)
		assert_eq(phys.physical_sens.gain, ErrorEstimate.physical_sensitivity(defs, [p.x, p.y], [c.x, c.y], [], BeamSolver.MAX_HITS, fsc).gain)

# DIRECTION-ONLY compares the planned shot with a reference aimed at the
# float64 rounding of the image. Where the planned trace decides an exact tie
# (here: the cursor at the room corner, two walls through it; plan = the
# inversion twice, whose exact image is the cursor itself), the rounding of
# the reference's aim decides the order instead: not a violation.
func test_direction_only_stops_at_rounding_ties():
	var scene := _scene("room_with_inversion")
	await get_tree().process_frame
	var checker := InvariantChecker.new()
	checker.setup(scene)
	var plan := [PlanManager.PlanEntry.new(5, Side.Value.LEFT), PlanManager.PlanEntry.new(5, Side.Value.LEFT)]
	assert_eq(checker.check_all(Vector2(560, 240), Vector2(0, 0), plan), [] as Array[String])
	assert_eq(checker.check_all(Vector2(960, 1080), Vector2(560, 840), plan), [] as Array[String])

# Found by the plan sweep once plans reached every scene. The cursor exactly
# on the planned surface's carrier (a fixed point of the effect), or player,
# circle center and cursor exactly collinear (the beam hits the mirror head
# on): exact incidences that rounding cannot decide. The initial aim line is
# exact for inversions too (a rational map of exact data), so the existing
# exactness rules decide them: no phantom hits, no unconverged climbs.
func test_plan_through_fixed_points_and_centers_is_exact():
	var cases := [
		["room_with_inversion", Side.Value.LEFT, Vector2(1155, 685), Vector2(1130, 540)],
		["room_with_inversion", Side.Value.LEFT, Vector2(1350, 685), Vector2(1000, 740)],
		["full_circle_mirror", Side.Value.LEFT, Vector2(1355, 760), Vector2(1160, 540)],
		["full_circle_mirror", Side.Value.RIGHT, Vector2(1355, 760), Vector2(1160, 540)],
		["full_circle_mirror", Side.Value.LEFT, Vector2(1760, 90), Vector2(1920, 0)],
		["full_circle_mirror", Side.Value.LEFT, Vector2(0, 1080), Vector2(160, 990)],
	]
	for c in cases:
		H.reset_counters()
		var scene := _scene(c[0])
		await get_tree().process_frame
		var checker := InvariantChecker.new()
		checker.setup(scene)
		var plan := [PlanManager.PlanEntry.new(5, c[1])]
		assert_eq(checker.check_all(c[2], c[3], plan), [] as Array[String], "%s %s %s" % c.slice(1))
		var r: PrecisionLadderResult = scene.get_node("PathRenderer").last_beam_result
		assert_not_null(r, "a trace pair: %s" % [c])
		if r != null:
			assert_true(r.converged, "converged: %s" % [c])
		scene.free()
