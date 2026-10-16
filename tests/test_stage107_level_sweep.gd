extends GutTest

# Stage 107: the invariant sweep of the interactive level -- as built and after
# spawns landed (presence-gated groups and spent hosts in the scene). A sweep
# file: run_tests_parallel.sh shards it (SWEEP_SHARD); test-fast skips it.

const LEVEL := "res://scenes/stage107/hit_effects.tscn"

func _level() -> Node:
	Surface.reset_id_counter()
	var scene: Node = load(LEVEL).instantiate()
	add_child_autofree(scene)
	return scene

func _shoot(scene: Node, player: Vector2, cursor: Vector2) -> void:
	var gm = scene.get_node("GameManager")
	scene.get_node("Player").global_position = player
	scene.get_node("Cursor").global_position = cursor
	gm._try_fire()
	gm._on_flight_completed()
	scene.get_node("ArrowAnimator").set_process(false)
	get_tree().paused = false

func test_stage107_level_invariant_sweep():
	var scene := _level()
	await get_tree().process_frame
	var runner := SweepRunner.new().configure(3, 4, 107)
	var result: Dictionary = runner.sweep(scene)
	_report(result, "as built")
	# spawn into a, b, c and f, then sweep the changed world
	_shoot(scene, Vector2(300, 220), Vector2(400, 220))
	_shoot(scene, Vector2(300, 390), Vector2(400, 390))
	_shoot(scene, Vector2(300, 560), Vector2(400, 560))
	_shoot(scene, Vector2(800, 400), Vector2(900, 400))
	var spawned := 0
	for ch in ["a", "b", "c", "f"]:
		spawned += scene.world.group_of(ch).size()
	assert_gt(spawned, 2, "groups landed before the second sweep")
	_report(SweepRunner.new().configure(3, 4, 108).sweep(scene), "after spawns")

# K5 (TDD_11, pre-existing, pending the user's decision): a carrier through a
# portal end's departure point -- here a spawned mirror's -- makes the first
# event after the teleport zero-length, and the tracer drops the step's
# after_portal mark: a "gap" from one portal end to the other at the same y.
# Only that exact signature is tolerated (and counted); anything else fails.
static func is_k5(v: String) -> bool:
	var m := RegEx.create_from_string("(NOGAPS: Gap|PHYSICAL-CONTINUITY: gap) between step \\d+ end=\\((1000|1300)\\.0, ([-0-9.]+)\\) and step \\d+ start=\\((1000|1300)\\.0, ([-0-9.]+)\\)").search(v)
	return m != null and m.get_string(2) != m.get_string(4) and m.get_string(3) == m.get_string(5)

func _report(result: Dictionary, label: String) -> void:
	var lines: Array[String] = []
	var k5 := 0
	for f: Dictionary in result.failures:
		for v: String in f.violations:
			if is_k5(v):
				k5 += 1
			else:
				lines.append("player=%s cursor=%s: %s" % [f.player_pos, f.cursor_pos, v])
	if lines.is_empty():
		pass_test("%s: %d combos (K5: %d)" % [label, result.total_combos, k5])
	else:
		fail_test("%s: %d violations:\n  %s" % [label, lines.size(), "\n  ".join(lines.slice(0, 10))])
