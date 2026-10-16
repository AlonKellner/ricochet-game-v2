extends GutTest

# Stage 108: the invariant sweep of the crossing level -- crossing design
# surfaces as built, and after a trimmed spawn landed. A sweep file:
# run_tests_parallel.sh shards it (SWEEP_SHARD); test-fast skips it.

const LEVEL := "res://scenes/stage108/crossings.tscn"

func test_stage108_level_invariant_sweep():
	Surface.reset_id_counter()
	var scene: Node = load(LEVEL).instantiate()
	add_child_autofree(scene)
	await get_tree().process_frame
	_report(SweepRunner.new().configure(3, 4, 208).sweep(scene), "as built")
	var gm = scene.get_node("GameManager")
	scene.get_node("Player").global_position = Vector2(300, 800)
	scene.get_node("Cursor").global_position = Vector2(400, 800)
	gm._try_fire()
	gm._on_flight_completed()
	scene.get_node("ArrowAnimator").set_process(false)
	get_tree().paused = false
	assert_eq(scene.world.group_of("t").size(), 1, "a trimmed spawn landed")
	_report(SweepRunner.new().configure(3, 4, 209).sweep(scene), "after the spawn")

func _report(result: Dictionary, label: String) -> void:
	var lines: Array[String] = []
	for f: Dictionary in result.failures:
		for v: String in f.violations:
			lines.append("player=%s cursor=%s: %s" % [f.player_pos, f.cursor_pos, v])
	if lines.is_empty():
		pass_test("%s: %d combos" % [label, result.total_combos])
	else:
		fail_test("%s: %d violations:\n  %s" % [label, lines.size(), "\n  ".join(lines.slice(0, 10))])
