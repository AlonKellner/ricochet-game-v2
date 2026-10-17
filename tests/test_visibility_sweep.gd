extends GutTest

# Stage 110: the visibility region (GAME_SPEC 15) swept against the preview.
# For each player position and plan the native sweep computes the region,
# checks its structure (VIS-*: valid, non-overlapping polygons whose edges are
# members -- beams through the player's image -- or carrier pieces; reported at
# the player) and pins it at every cursor (VISIBILITY-EQUALS-NON-DIVERGENCE:
# inside iff the merged steps before the cursor are ALIGNED and it is reached).
# A sweep file: run_tests_parallel.sh shards it (SWEEP_SHARD).

var VIOLATIONS_PATH := SweepRunner.violations_path("visibility")

# Known findings (docs/TDD_14_VISIBILITY.md, "Known findings"): a violation
# matching one is reported, not failed; one that no longer occurs fails the
# unsharded sweep so the list stays true. An entry without a scene matches
# every scene (and is not required to occur).
const KNOWN_FINDINGS := [
	# K7: a cursor exactly on a planned surface's carrier, where a walk departs or
	# ends its lap: whether the solver reaches it at once or only after the loop
	# varies along the carrier with no change in the casts (a measure-zero set).
	{"id": "K7", "prefix": "VISIBILITY-EQUALS-NON-DIVERGENCE (on a planned carrier)"},
	# K8: a cursor exactly on a critical member (the member tangent at Q to a
	# carrier through Q, the other way round): the member's own cast is built from
	# a rounded direction, so it crosses where the exact cursor touches.
	{"id": "K8", "prefix": "VISIBILITY-EQUALS-NON-DIVERGENCE (on a critical member)"},
]

func test_visibility_empty_plan_on_every_level() -> void:
	var scenes := SweepPlans.discover_scenes(SweepPlans.TEST_LEVELS_DIR)
	for s in SweepPlans.playground_plan_scenes():
		scenes.append(s)
	await _sweep(scenes, func(_scene): return [[]], 110, "Visibility, no plan")

func test_visibility_plans() -> void:
	var scenes: Array[String] = SweepPlans.plan_scenes()
	await _sweep(scenes, func(scene):
		var plans := SweepPlans.reflective_plans(scene)
		plans.append_array(SweepPlans.inversion_plans(scene))
		return plans, 111, "Visibility, plans")

func test_visibility_playground_plans() -> void:
	await _sweep(SweepPlans.playground_plan_scenes(), func(scene): return SweepPlans.scene_plans(scene).filter(_supported), 112,
		"Visibility, playground plans")

func test_visibility_infinity_plans() -> void:
	var scenes: Array[String] = SweepPlans.plan_scenes()
	scenes.append_array(SweepPlans.playground_plan_scenes())
	await _sweep(scenes, func(scene): return SweepPlans.infinity_plans(scene).filter(_supported), 113, "Visibility, infinity plans")

# Plans through projective surfaces: the region is not computed yet (Stage 110 scope).
func _supported(plan: Array) -> bool:
	for e in plan:
		if e.surface_id == BeamSolver.INFINITY_ID:
			continue
		var s: Surface = _scene_surfaces.get(e.surface_id)
		if s != null:
			var cfg: SideConfig = s.active_side_config(e.side, GameState.new({}))
			if cfg != null and cfg.effect != null and cfg.effect.kind() == Effect.Kind.PROJECTIVE:
				return false
	return true

var _scene_surfaces := {}

func _known(f: Dictionary) -> String:
	for k in KNOWN_FINDINGS:
		if (not k.has("scene") or f.scene == k.scene) and String(f.violation).begins_with(k.prefix):
			return k.id
	return ""

func _sweep(scenes: Array[String], plans_of: Callable, seed: int, label: String) -> void:
	var failures: Array[Dictionary] = []
	var combos := 0
	var known_seen := {}
	for scene_path in scenes:
		Surface.reset_id_counter()
		var scene: Node = load(scene_path).instantiate()
		scene.gravity = Vector2.ZERO
		add_child_autofree(scene)
		await get_tree().process_frame
		_scene_surfaces = {}
		for s in scene.surfaces:
			_scene_surfaces[s.id] = s
		var plans: Array = plans_of.call(scene)
		if plans.is_empty():
			continue
		var runner := SweepPlans.bounded_runner(scene, 4, 6, seed)
		var positions := runner.build_positions()
		positions.append_array(SweepPlans.q0_positions(scene))
		positions.append_array(SweepPlans.on_surface_positions(scene))
		combos += plans.size() * SweepRunner.combo_count(positions, true)
		for f in SweepRunner.sweep_native(scene, positions, plans, true, null, true, true):
			for v: String in f.violations:
				var entry := {"scene": scene_path, "player_pos": f.player_pos, "cursor_pos": f.cursor_pos,
					"plan": SweepPlans.plan_to_data(plans[f.plan_index]), "violation": v}
				var k := _known(entry)
				if k == "":
					failures.append(entry)
				else:
					known_seen[k] = int(known_seen.get(k, 0)) + 1
	for k in known_seen:
		gut.p("%s sweep: known finding %s, %d combos" % [label, k, known_seen[k]])
	if SweepRunner.shard().y == 1:
		for k in KNOWN_FINDINGS:
			if k.has("scene") and scenes.has(k.scene) and not known_seen.has(k.id):
				fail_test("known finding %s no longer occurs: remove it from KNOWN_FINDINGS and the doc" % k.id)
	_save_violations(failures)
	if failures.size() > 0:
		var report := "%s sweep violations (%d):\n" % [label, failures.size()]
		for f in failures.slice(0, 12):
			report += "  [%s] plan=%s player=%s cursor=%s: %s\n" % [f.scene.get_file(), f.plan, f.player_pos, f.cursor_pos, f.violation]
		fail_test(report)
	else:
		pass_test("%s sweep passed: %d combos" % [label, combos])

func _save_violations(failures: Array) -> void:
	var existing: Array = []
	if FileAccess.file_exists(VIOLATIONS_PATH):
		var rf := FileAccess.open(VIOLATIONS_PATH, FileAccess.READ)
		if rf:
			var json := JSON.new()
			if json.parse(rf.get_as_text()) == OK and json.data is Array:
				existing = json.data
			rf.close()
	for f in failures:
		existing.append({"scene": f.scene, "player_pos": [f.player_pos.x, f.player_pos.y], "cursor_pos": [f.cursor_pos.x, f.cursor_pos.y],
			"plan": f.plan, "violation": f.violation})
	var file := FileAccess.open(VIOLATIONS_PATH, FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(existing, "  "))
		file.close()
