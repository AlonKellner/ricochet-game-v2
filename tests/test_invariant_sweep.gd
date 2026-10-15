extends GutTest

const TEST_LEVELS_DIR := "res://scenes/test_levels/"
var VIOLATIONS_PATH := SweepRunner.violations_path("sweep")
const MAX_VIOLATIONS_PER_GROUP := 5
const MAX_TOTAL_VIOLATIONS := 200

func test_sweep_all_scenes() -> void:
	var scene_paths := SweepPlans.discover_scenes(TEST_LEVELS_DIR)
	assert_gt(scene_paths.size(), 0, "Should find test scenes in %s" % TEST_LEVELS_DIR)

	var total_failures: Array[Dictionary] = []
	var total_combos := 0

	for scene_path in scene_paths:
		Surface.reset_id_counter()
		var scene: Node = load(scene_path).instantiate()
		scene.gravity = Vector2.ZERO
		add_child_autofree(scene)

		var runner := SweepRunner.new().configure(5, 10, 42)
		var result: Dictionary = runner.sweep(scene)
		total_combos += result.total_combos

		var failures: Array = result.failures
		for failure: Dictionary in failures:
			var violations: Array = failure.violations
			for violation: String in violations:
				total_failures.append({
					"scene": scene_path,
					"player_pos": failure.player_pos,
					"cursor_pos": failure.cursor_pos,
					"plan": [],
					"violation": violation,
				})

	_save_violations(total_failures)
	if total_failures.size() > 0:
		var report := "Invariant violations found (%d):\n" % total_failures.size()
		for f in total_failures.slice(0, 10):
			report += "  [%s] player=%s cursor=%s: %s\n" % [
				f.scene, f.player_pos, f.cursor_pos, f.violation]
		if total_failures.size() > 10:
			report += "  ... and %d more\n" % (total_failures.size() - 10)
		fail_test(report)
	else:
		pass_test("All invariants passed across %d combos in %d scenes" % [total_combos, scene_paths.size()])

func test_sweep_with_plans() -> void:
	var plan_scenes := SweepPlans.plan_scenes()

	var total_failures: Array[Dictionary] = []
	var total_combos := 0

	for scene_path in plan_scenes:
		if not FileAccess.file_exists(scene_path):
			continue
		Surface.reset_id_counter()
		var scene: Node = load(scene_path).instantiate()
		scene.gravity = Vector2.ZERO
		add_child_autofree(scene)
		await get_tree().process_frame
		await get_tree().process_frame

		var plans := SweepPlans.reflective_plans(scene)
		plans.append_array(SweepPlans.inversion_plans(scene))
		if plans.is_empty():
			continue
		var runner := SweepPlans.bounded_runner(scene, 5, 10, 42)
		var poi := runner._extract_points_of_interest(scene)
		var positions := runner.build_positions(poi)
		total_combos += plans.size() * SweepRunner.combo_count(positions, true)
		for f in SweepRunner.sweep_native(scene, positions, plans, true):
			for v: String in f.violations:
				total_failures.append({
					"scene": scene_path,
					"player_pos": f.player_pos,
					"cursor_pos": f.cursor_pos,
					"plan": SweepPlans.plan_to_data(plans[f.plan_index]),
					"violation": v,
				})
		# one combo through the live PathRenderer: the driver path the game runs
		var checker := InvariantChecker.new()
		checker.setup(scene)
		for v: String in checker.check_all(positions[0], positions[1], plans[0]):
			total_failures.append({"scene": scene_path, "player_pos": positions[0], "cursor_pos": positions[1],
				"plan": SweepPlans.plan_to_data(plans[0]), "violation": "LIVE " + v})

	_save_violations(total_failures)
	if total_failures.size() > 0:
		var report := "Plan sweep violations (%d):\n" % total_failures.size()
		for f in total_failures.slice(0, 10):
			report += "  [%s] plan=%s player=%s cursor=%s: %s\n" % [
				f.scene, _plan_to_str_from_data(f.plan), f.player_pos, f.cursor_pos, f.violation]
		if total_failures.size() > 10:
			report += "  ... and %d more\n" % (total_failures.size() - 10)
		fail_test(report)
	else:
		pass_test("Plan sweep passed: %d combos with plans" % total_combos)

func _plan_to_str_from_data(plan_data: Array) -> String:
	var parts: Array[String] = []
	for d in plan_data:
		parts.append("%d/%s" % [d.surface_id, "L" if d.side == Side.Value.LEFT else "R"])
	return "[%s]" % ",".join(parts)

func _save_violations(failures: Array) -> void:
	var existing: Array = []
	if FileAccess.file_exists(VIOLATIONS_PATH):
		var rf := FileAccess.open(VIOLATIONS_PATH, FileAccess.READ)
		if rf:
			var json := JSON.new()
			if json.parse(rf.get_as_text()) == OK and json.data is Array:
				existing = json.data
			rf.close()

	var new_entries: Array = []
	for f in failures:
		new_entries.append({
			"scene": f.scene,
			"player_pos": [f.player_pos.x, f.player_pos.y],
			"cursor_pos": [f.cursor_pos.x, f.cursor_pos.y],
			"plan": f.plan,
			"violation": f.violation,
		})

	var all_entries: Array = existing + new_entries

	var file := FileAccess.open(VIOLATIONS_PATH, FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(all_entries, "  "))
		file.close()
		print("[Sweep] Saved %d violations to %s" % [all_entries.size(), VIOLATIONS_PATH])

# Stage 87: plans through projective surfaces, on the projective playgrounds
# (each projective side as a one-entry plan, and the first two projectives
# as a two-entry plan).
# Stage 103: plans on the playgrounds no other sweep covers (portals, the
# plannable inversion, two-sided surfaces, the pre-transform), and infinity
# plans on every planned scene.
func test_sweep_playground_plans() -> void:
	await _sweep_scenes(SweepPlans.playground_plan_scenes(), func(scene): return [[]] + SweepPlans.scene_plans(scene), 103, "Playground plan")

func test_sweep_infinity_plans() -> void:
	var scenes: Array[String] = SweepPlans.plan_scenes()
	scenes.append_array(SweepPlans.playground_plan_scenes())
	await _sweep_scenes(scenes, func(scene): return SweepPlans.infinity_plans(scene), 88, "Infinity plan")

# Known findings (docs/TDD_10_NATIVE_MIGRATION.md, "Known findings"): a
# violation matching one is reported, not failed; a finding that no longer
# occurs fails the sweep so the list stays true.
# (K1, K2 and K3 were resolved by Stages 105 and 106: none are open.)
const KNOWN_FINDINGS := []

static func _known(f: Dictionary, plan_str: String) -> String:
	for k in KNOWN_FINDINGS:
		if f.scene == k.scene and plan_str == k.plan and String(f.violation).begins_with(k.prefix):
			return k.id
	return ""

# Sweep `scenes` with the plans `plans_of(scene)` returns (a small grid plus
# fuzz positions, natively) and one live combo per scene through the renderer.
func _sweep_scenes(scenes: Array[String], plans_of: Callable, seed: int, label: String) -> void:
	var total_failures: Array[Dictionary] = []
	var known_seen := {}
	var total_combos := 0
	for scene_path in scenes:
		Surface.reset_id_counter()
		var scene: Node = load(scene_path).instantiate()
		scene.gravity = Vector2.ZERO
		add_child_autofree(scene)
		await get_tree().process_frame
		var plans: Array = plans_of.call(scene)
		assert_gt(plans.size(), 0, "%s has plans" % scene_path)
		var runner := SweepPlans.bounded_runner(scene, 4, 6, seed)
		var positions := runner.build_positions()
		positions.append_array(SweepPlans.q0_positions(scene)) # Stage 105: exact grazes at Q0
		positions.append_array(SweepPlans.on_surface_positions(scene)) # Stage 106: pops at known points
		total_combos += plans.size() * SweepRunner.combo_count(positions, true)
		for f in SweepRunner.sweep_native(scene, positions, plans, true):
			for v: String in f.violations:
				total_failures.append({"scene": scene_path, "player_pos": f.player_pos, "cursor_pos": f.cursor_pos,
					"plan": SweepPlans.plan_to_data(plans[f.plan_index]), "violation": v})
		var checker := InvariantChecker.new()
		checker.setup(scene)
		for v: String in checker.check_all(positions[0], positions[1], plans[plans.size() - 1]):
			total_failures.append({"scene": scene_path, "player_pos": positions[0], "cursor_pos": positions[1],
				"plan": SweepPlans.plan_to_data(plans[plans.size() - 1]), "violation": "LIVE " + v})
	var unknown: Array[Dictionary] = []
	for f in total_failures:
		var k := _known(f, _plan_to_str_from_data(f.plan))
		if k == "":
			unknown.append(f)
		else:
			known_seen[k] = int(known_seen.get(k, 0)) + 1
	for k in known_seen:
		gut.p("%s sweep: known finding %s, %d combos" % [label, k, known_seen[k]])
	for k in KNOWN_FINDINGS:
		var scene_swept := scenes.has(k.scene)
		# (a shard sees only some player positions: only the full sweep can tell)
		if scene_swept and not known_seen.has(k.id) and label == "Playground plan" and SweepRunner.shard().y == 1:
			fail_test("known finding %s no longer occurs: remove it from KNOWN_FINDINGS and the doc" % k.id)
	total_failures = unknown
	_save_violations(total_failures)
	if total_failures.size() > 0:
		var report := "%s sweep violations (%d):\n" % [label, total_failures.size()]
		for f in total_failures.slice(0, 10):
			report += "  [%s] plan=%s player=%s cursor=%s: %s\n" % [f.scene, _plan_to_str_from_data(f.plan), f.player_pos, f.cursor_pos, f.violation]
		fail_test(report)
	else:
		pass_test("%s sweep passed: %d combos" % [label, total_combos])

func test_sweep_projective_plans() -> void:
	var total_failures: Array[Dictionary] = []
	var total_combos := 0
	for scene_path in SweepPlans.projective_scenes():
		var name := scene_path.get_file().get_basename()
		Surface.reset_id_counter()
		var scene: Node = load(scene_path).instantiate()
		scene.gravity = Vector2.ZERO
		add_child_autofree(scene)
		await get_tree().process_frame
		assert_gt(SweepPlans.projective_surfaces(scene).size(), 0, "%s has projective surfaces" % name)
		var plans := SweepPlans.projective_plans(scene)
		var runner := SweepPlans.bounded_runner(scene, 4, 6, 87)
		var positions := runner.build_positions()
		total_combos += plans.size() * SweepRunner.combo_count(positions, true)
		for f in SweepRunner.sweep_native(scene, positions, plans, true):
			for v: String in f.violations:
				total_failures.append({"scene": scene_path, "player_pos": f.player_pos, "cursor_pos": f.cursor_pos,
					"plan": SweepPlans.plan_to_data(plans[f.plan_index]), "violation": v})
		var checker := InvariantChecker.new()
		checker.setup(scene)
		for v: String in checker.check_all(positions[0], positions[1], plans[0]):
			total_failures.append({"scene": scene_path, "player_pos": positions[0], "cursor_pos": positions[1],
				"plan": SweepPlans.plan_to_data(plans[0]), "violation": "LIVE " + v})

	_save_violations(total_failures)
	if total_failures.size() > 0:
		var report := "Projective plan sweep violations (%d):\n" % total_failures.size()
		for f in total_failures.slice(0, 10):
			report += "  [%s] plan=%s player=%s cursor=%s: %s\n" % [f.scene, _plan_to_str_from_data(f.plan), f.player_pos, f.cursor_pos, f.violation]
		fail_test(report)
	else:
		pass_test("Projective plan sweep passed: %d combos" % total_combos)
