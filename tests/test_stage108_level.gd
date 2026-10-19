extends GutTest

# Stage 108: the interactive crossing level (scenes/test_levels/crossings.tscn,
# its data data/levels/test_levels/crossings.json) -- the pieces it builds,
# a planned piece, a trimmed spawn (its sweep: test_stage108_level_sweep.gd).

const LEVEL := "res://scenes/test_levels/crossings.tscn"

func _level() -> Node:
	Surface.reset_id_counter()
	var scene: Node = load(LEVEL).instantiate()
	add_child_autofree(scene)
	return scene

func _at(scene: Node, a: Vector2) -> Surface:
	for s in scene.surfaces:
		if s.segment.start.coords == a:
			return s
	return null

func _circle_at(scene: Node, c: Vector2) -> Surface:
	for s in scene.surfaces:
		if s.segment.full and not s.segment.is_line() and s.segment.get_carrier().center().distance_to(c) < 1e-3:
			return s
	return null

func test_stage108_level_pieces():
	var scene := _level()
	await get_tree().process_frame
	var p: Pieces = scene.world.pieces()
	var count := func(list: Array) -> int:
		var n := 0
		for s in list:
			n += p.of(s).size()
		return n
	assert_eq(count.call([_at(scene, Vector2(600, 200)), _at(scene, Vector2(520, 330))]), 4)
	assert_eq(count.call([_at(scene, Vector2(450, 600)), _at(scene, Vector2(550, 540)), _at(scene, Vector2(750, 540))]), 7)
	assert_eq(count.call([_at(scene, Vector2(880, 250)), _at(scene, Vector2(890, 230)), _at(scene, Vector2(1110, 230))]), 9)
	assert_eq(count.call([_circle_at(scene, Vector2(1400, 300)), _at(scene, Vector2(1400, 180))]), 5)
	assert_eq(count.call([_circle_at(scene, Vector2(1350, 650)), _circle_at(scene, Vector2(1450, 650))]), 4)
	assert_eq(p.portal_crossings, [] as Array[String])

func test_stage108_level_spawn_is_trimmed_by_the_floor():
	var scene := _level()
	await get_tree().process_frame
	var gm = scene.get_node("GameManager")
	scene.get_node("Player").global_position = Vector2(300, 800)
	scene.get_node("Cursor").global_position = Vector2(400, 800)
	scene.get_node("Cursor").set_process(false)
	gm._try_fire()
	gm._on_flight_completed()
	scene.get_node("ArrowAnimator").set_process(false)
	get_tree().paused = false
	var group: Array = scene.world.group_of("t")
	assert_eq(group.size(), 1, "spawned")
	var seg: Segment = group[0].segment
	var ys := [seg.start.coords.y, seg.end.coords.y]
	ys.sort()
	assert_eq(ys, [650.0, 900.0], "300 px centred on (450, 800), trimmed at the floor (y = 900)")
	assert_true(seg.is_line())
