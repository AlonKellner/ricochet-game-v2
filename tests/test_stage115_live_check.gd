extends GutTest

# TDD_20 F1: the debug live check (VisibilityRenderer._live_check) compares the
# region with the preview at the cursor only when both are fresh. After a
# landing the region (never paused) recomputes with the landed flags a frame
# before the preview (paused during the flight) does; comparing them then
# reported VISIBILITY-EQUALS-NON-DIVERGENCE mismatches that were not.

func test_no_live_mismatch_after_a_landing_that_changes_the_flags():
	Surface.reset_id_counter()
	var scene: Node = load("res://scenes/test_levels/flag_switches.tscn").instantiate()
	add_child_autofree(scene)
	var player: Node2D = scene.get_node("Player")
	var cursor: Node2D = scene.get_node("Cursor")
	cursor.set_process(false)
	player.set_physics_process(false)
	var vis: VisibilityRenderer = scene.get_node("VisibilityRenderer")
	vis.async_compute = false
	var gm = scene.get_node("GameManager")
	player.global_position = Vector2(700, 520)
	# the toggler's right side planned; the cursor beyond the gate it opens
	gm.plan.add_entry(9, Side.Value.RIGHT, Vector2(794, 444))
	cursor.global_position = Vector2(1180, 520)
	for k in 3:
		await get_tree().process_frame
	assert_eq(vis.region.contains(cursor.global_position), 1, "visible: the toggler opens the gate")
	# the real flight: the tree is paused while the arrow flies and the landing
	# happens inside a paused frame (open = true: crossing the toggler again closes the gate)
	var arrow: Node2D = scene.get_node("ArrowAnimator")
	gm._try_fire()
	var frames := 0
	while arrow.is_flying() and frames < 2000:
		await get_tree().process_frame
		frames += 1
	assert_false(arrow.is_flying(), "landed")
	assert_eq(gm.game_state.flags.get("open"), true)
	for k in 4:
		await get_tree().process_frame
	assert_eq(vis.region.contains(cursor.global_position), 0, "not visible after the landing")
	assert_false(VisibilityRenderer.preview_visible(scene.get_node("PathRenderer")))
	assert_eq(vis.live_mismatches, 0, "no mismatch reported in the frame between them")
