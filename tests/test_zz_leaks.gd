extends GutTest

# Stage 103: a leak budget. Loading, playing and freeing a level must give
# back every object it made: side links refer to their surfaces and closing
# partners weakly (no reference cycles), and the overlay links are cached per
# surface instead of rebuilt every frame. Runs last (file name).

const BUDGET := 8   # objects: engine-internal caches may grow by a few

func _levels() -> Array[String]:
	var out: Array[String] = []
	for dir in ["res://scenes/test_levels", "res://scenes/playground"]:
		for fn in DirAccess.open(dir).get_files():
			if fn.ends_with(".tscn"):
				out.append(dir + "/" + fn)
	out.append("res://scenes/main.tscn")
	return out

# Load the level, let it run a few frames with the cursor resting near a
# surface (hover overlays, plan overlays, the preview), then free it.
func _play(path: String) -> void:
	Surface.reset_id_counter()
	var scene: Node = load(path).instantiate()
	add_child(scene)
	var cursor: Node2D = scene.get_node_or_null("Cursor")
	var player: Node2D = scene.get_node_or_null("Player")
	await get_tree().process_frame
	if cursor:
		cursor.set_process(false)
	if player:
		player.set_physics_process(false)
	var gm: Node = scene.get_node_or_null("GameManager")
	if "surfaces" in scene and cursor:
		for s in scene.surfaces:
			if not s.pre_transform and s.active_side_config(Side.Value.LEFT, GameState.new()) != null \
					and s.active_side_config(Side.Value.LEFT, GameState.new()).interactive:
				cursor.global_position = s.segment.via.coords + Vector2(5, 5)
				if gm:
					gm.plan.add_entry(s.id, Side.Value.LEFT)
				break
	for i in 3:
		await get_tree().process_frame
	scene.queue_free()
	await get_tree().process_frame
	await get_tree().process_frame

func test_levels_give_back_their_objects():
	var levels := _levels()
	assert_true(levels.size() >= 30, "every level: %d" % levels.size())
	# warm-up: scripts, scenes and the extension's classes load once
	for path in levels:
		await _play(path)
	var before := int(Performance.get_monitor(Performance.OBJECT_COUNT))
	for path in levels:
		await _play(path)
	var after := int(Performance.get_monitor(Performance.OBJECT_COUNT))
	assert_lt(after - before, BUDGET, "objects before %d, after %d: a level leaked" % [before, after])
	assert_eq(int(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT)), 0, "no orphan nodes")

func test_overlay_links_are_reused():
	H_reset()
	var seg := Segment.from_coords(Vector2(400, 0), Vector2(400, 600), Vector2(400, 300))
	var s := Surface.new(seg, SideConfig.new(null, true), SideConfig.new(null, true), false, false)
	var a := s.overlay_link(Side.Value.LEFT)
	var b := s.overlay_link(Side.Value.LEFT)
	assert_true(is_same(a, b), "the derived link of a side is built once")
	assert_not_null(a.outgoing, "a pass leaves by the opposite side")
	assert_true(is_same(a.outgoing.outgoing, a), "and its partner leads back")

func test_links_hold_no_cycles():
	H_reset()
	var seg := Segment.from_coords(Vector2(400, 0), Vector2(400, 600), Vector2(400, 300))
	var refl := ReflectionEffect.new(seg.get_carrier())
	var s := Surface.new(seg, SideConfig.new(refl, true), SideConfig.new(null, false))
	var link := s.get_side_link(Side.Value.LEFT)
	var watch: WeakRef = weakref(s)
	assert_true(is_same(link.outgoing, link), "a self-link")
	assert_true(is_same(link.surface, s))
	s = null
	assert_null(watch.get_ref(), "a mirror surface is freed with its last reference (no cycle)")
	assert_null(link.surface, "its link then has no surface")
	# a pair: the first link keeps the second alive, the second leads back
	H_reset()
	var a_seg := Segment.from_coords(Vector2(100, 0), Vector2(100, 600), Vector2(100, 300))
	var b_seg := Segment.from_coords(Vector2(500, 0), Vector2(500, 600), Vector2(500, 300))
	var sa := Surface.new(a_seg, SideConfig.new(null, false), SideConfig.new(null, false))
	var sb := Surface.new(b_seg, SideConfig.new(null, false), SideConfig.new(null, false))
	var la := SideLink.from_pair(sa, Side.Value.LEFT, sb, Side.Value.RIGHT)
	var wb: WeakRef = weakref(la.outgoing)
	assert_not_null(wb.get_ref(), "the partner lives while the first link does")
	assert_true(is_same(la.outgoing.outgoing, la))
	la = null
	assert_null(wb.get_ref(), "and the pair is freed with it (no cycle)")

func H_reset() -> void:
	Surface.reset_id_counter()
