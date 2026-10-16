extends GutTest

# Stage 108: pieces in a level (GAME_SPEC 9.7) -- a node per piece, clicks
# and plans on pieces, the solver matching a planned piece, crossing-free
# levels exactly as before. combo_base: the room is (200, 100)-(1700, 900).

const LEVEL := "res://scenes/test_levels/combo_base.tscn"

func _side(type := "pass", interactive := false) -> SideSpec:
	return SideSpec.make({"type": type}, interactive)

# a two-sided mirror at x = 900 (y 300..700) crossed by a block at y = 500
func _level() -> Node:
	Surface.reset_id_counter()
	var scene: Node = load(LEVEL).instantiate()
	scene.surface_specs.append(SurfaceSpec.line(Vector2(900, 300), Vector2(900, 700), _side("reflection", true), _side("reflection", true)))
	scene.surface_specs.append(SurfaceSpec.line(Vector2(800, 500), Vector2(1000, 500), _side("terminal"), _side("terminal")))
	add_child_autofree(scene)
	return scene

func _mirror(scene: Node) -> Surface:
	for s in scene.surfaces:
		if s.segment.start.coords == Vector2(900, 300):
			return s
	return null

func _nodes_of(scene: Node, s: Surface) -> Array:
	return scene.get_children().filter(func(n): return "surface" in n and n.surface == s)

func test_stage108_a_node_per_piece():
	var scene := _level()
	await get_tree().process_frame
	var m := _mirror(scene)
	var nodes := _nodes_of(scene, m)
	assert_eq(nodes.size(), 2, "the crossed mirror is drawn as two pieces")
	assert_eq(nodes.map(func(n): return String(n.name)), ["Spec_%d_p0" % m.id, "Spec_%d_p1" % m.id])
	assert_eq(nodes[0].segment.end.coords, Vector2(900, 500))
	var walls: Array = scene.get_children().filter(func(n): return String(n.name).begins_with("Surface_"))
	assert_eq(walls.size(), 4, "the room's walls keep their nodes and names")

func test_stage108_click_and_plan_a_piece():
	var scene := _level()
	await get_tree().process_frame
	var gm = scene.get_node("GameManager")
	var m := _mirror(scene)
	scene.get_node("Cursor").global_position = Vector2(895, 400)
	var r: Dictionary = gm._detect_click()
	assert_eq(r.get("surface"), m)
	assert_eq(r["piece"].index, 0, "the upper piece")
	gm._handle_plan_click(false)
	assert_eq(gm.plan.entries.size(), 1)
	var e: PlanManager.PlanEntry = gm.plan.entries[0]
	assert_eq(e.piece, 0)
	assert_eq(e.span, PackedFloat64Array([900, 300, 900, 500, 900, 400]))

func test_stage108_the_solver_matches_the_planned_piece():
	var scene := _level()
	await get_tree().process_frame
	var m := _mirror(scene)
	var pieces: Pieces = scene.world.pieces()
	var defs := SceneDefs.defs_from_surfaces(scene.surfaces)
	# aim from (700, 350) at the cursor's mirror image: the shot hits the upper piece
	for case in [[0, true], [1, false]]:
		var entry := PlanManager.PlanEntry.new(m.id, Side.Value.RIGHT, Vector2(INF, INF), case[0], pieces.piece(m.id, case[0]).span)
		var pair: BeamTracerPair = BeamTracer.new().trace_pair(defs, Vector2(700, 350), Vector2(750, 400), [entry], {}, {})
		var first: TraceStep = pair.planned.steps[0]
		assert_eq(first.surface_id, m.id)
		assert_eq(first.hit_on_segment, case[1], "planned on piece %d: the hit is %s it" % [case[0], "on" if case[1] else "off"])
		assert_eq(pair.planned.cursor_index >= 0 and pair.physical.cursor_index >= 0, case[1],
			"the physical shot reaches the cursor only when the planned piece is hit")

func test_stage108_plan_labels_go_to_the_piece():
	var scene := _level()
	await get_tree().process_frame
	var gm = scene.get_node("GameManager")
	var m := _mirror(scene)
	gm.plan.add_entry(m.id, Side.Value.RIGHT, Vector2(900, 600), scene.world.pieces().piece(m.id, 1))
	gm._update_surface_overlays(true)
	var nodes := _nodes_of(scene, m)
	assert_eq(nodes[0]._plan_indices, [] as Array[int])
	assert_eq(nodes[1]._plan_indices, [1] as Array[int], "the label is on the lower piece")

func test_stage108_crossing_free_levels_keep_their_nodes():
	Surface.reset_id_counter()
	var scene: Node = load("res://scenes/test_levels/three_mirrors.tscn").instantiate()
	add_child_autofree(scene)
	await get_tree().process_frame
	var drawn: Array = scene.get_children().filter(func(n): return "surface" in n)
	for n in drawn:
		assert_eq(n.piece_index, -1, "%s: whole" % n.name)
		assert_false(String(n.name).contains("_p"), "names as before")
		assert_eq(n.segment, n.surface.segment)
