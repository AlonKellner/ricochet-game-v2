extends GutTest

# Stage 107: spawn and reset in a real level (GAME_SPEC 10.6), through the
# game manager: the shot that spawns never sees its spawn; the next one does;
# a reset acts in the shot; undo and reset restore the world (the same
# surfaces); plans on surfaces that left are dropped.
#
# combo_base: the room is (200, 100)-(1700, 900); no arrow, a shot lands at once.
# The host H stands at x = 900 (y 300..780); a reset R at x = 1100 (y 150..420)
# is off the first shot's line. The block template lands 400 along the beam.

const M := SpawnFrame.Mode
const LEVEL := "res://scenes/test_levels/combo_base.tscn"

func _side(type := "pass", interactive := false, hits: Array = [], params := {}) -> SideSpec:
	var e := {"type": type}
	e.merge(params)
	return SideSpec.make(e, interactive, hits)

func _block_template() -> SpawnTemplate:
	return SpawnTemplate.make(SurfaceSpec.line(Vector2(400, -60), Vector2(400, 60), _side("terminal"), _side("terminal")), M.BEAM_TANGENT)

func _mirror_template() -> SpawnTemplate:
	return SpawnTemplate.make(SurfaceSpec.line(Vector2(400, -60), Vector2(400, 60), _side("reflection", true), _side("reflection", true)),
		M.BEAM_TANGENT)

func _host(type := "pass", template: SpawnTemplate = null, params := {}) -> SurfaceSpec:
	var hits := [{"kind": "spawn", "channel": "c", "template": template if template != null else _block_template()}]
	var s := _side(type, true, hits, params)
	return SurfaceSpec.line(Vector2(900, 300), Vector2(900, 780), s, s)

func _reset_line() -> SurfaceSpec:
	var s := _side("pass", true, [{"kind": "reset", "channels": ["c"]}])
	return SurfaceSpec.line(Vector2(1100, 150), Vector2(1100, 420), s, s)

func _level(specs: Array) -> Node:
	Surface.reset_id_counter()
	var scene: Node = load(LEVEL).instantiate()
	for s in specs:
		scene.surface_specs.append(s)
	add_child_autofree(scene)
	return scene

# The shot's physical trace (the one the game manager fires), then the shot.
func _shoot(scene: Node, player: Vector2, cursor: Vector2) -> TracePath:
	var gm = scene.get_node("GameManager")
	scene.get_node("Player").global_position = player
	scene.get_node("Cursor").global_position = cursor
	var pair: BeamTracerPair = BeamTracer.new().trace_pair(SceneDefs.defs_from_surfaces(scene.surfaces), player, cursor,
		gm.plan.entries, gm.game_state.flags, {})
	gm._try_fire()
	get_tree().paused = false
	return pair.physical

func _ids(path: TracePath) -> Array:
	return path.hits.map(func(h): return h["surface_id"])

func _at_x(scene: Node, x: float) -> Surface:
	for s in scene.surfaces:
		if not s.segment.full and s.segment.start.coords.x == x and s.segment.end.coords.x == x:
			return s
	return null

func test_stage107_the_spawning_shot_ignores_its_spawn_the_next_does_not():
	var scene := _level([_host()])
	await get_tree().process_frame
	var host := _at_x(scene, 900)
	var first := _shoot(scene, Vector2(700, 540), Vector2(800, 540))
	assert_eq(first.hits[0]["surface_id"], host.id, "the host fires")
	var gm = scene.get_node("GameManager")
	assert_eq(gm.game_state.flags.get("chan:c"), "g1", "generation 1 landed")
	var block := _at_x(scene, 1300)
	assert_not_null(block, "a block was placed 400 along the beam, across its line")
	assert_false(_ids(first).has(block.id), "the shot that spawned it did not meet it")
	assert_false(host.present(gm.game_state.flags), "the host is spent")
	var second := _shoot(scene, Vector2(700, 540), Vector2(800, 540))
	assert_eq(_ids(second), [block.id], "the next shot passes the spent host and stops at the block")

func test_stage107_a_reset_acts_in_the_shot():
	var scene := _level([_host(), _reset_line()])
	await get_tree().process_frame
	_shoot(scene, Vector2(700, 540), Vector2(800, 540))
	var block := _at_x(scene, 1300)
	var reset := _at_x(scene, 1100)
	assert_not_null(block)
	# through the reset at x = 1100 (y ~ 347), then where the block stands (1300, 540)
	var shot := _shoot(scene, Vector2(1000, 250), Vector2(1150, 395))
	assert_eq(shot.hits[0]["surface_id"], reset.id, "the reset fires")
	assert_false(_ids(shot).has(block.id), "the block vanished in the same shot")
	var gm = scene.get_node("GameManager")
	assert_eq(gm.game_state.flags.get("chan:c"), "armed")
	assert_eq(scene.world.group_of("c"), [], "the group left the world")
	assert_null(_at_x(scene, 1300))
	assert_true(_at_x(scene, 900).present(gm.game_state.flags), "the host is back")

func test_stage107_undo_and_reset_restore_the_world():
	var scene := _level([_host()])
	await get_tree().process_frame
	var gm = scene.get_node("GameManager")
	var before: Array = scene.surfaces.map(func(s): return s.id)
	_shoot(scene, Vector2(700, 540), Vector2(800, 540))
	var block := _at_x(scene, 1300)
	assert_not_null(block)
	_shoot(scene, Vector2(700, 540), Vector2(800, 540))   # stops at the block; the world is unchanged
	gm._try_undo()
	assert_eq(_at_x(scene, 1300), block, "one undo: still the same spawned surface")
	gm._try_undo()
	assert_null(_at_x(scene, 1300), "two undos: before the spawn")
	assert_eq(scene.surfaces.map(func(s): return s.id), before, "the very same surfaces")
	assert_false(gm.game_state.flags.has("chan:c"))
	_shoot(scene, Vector2(700, 540), Vector2(800, 540))
	assert_not_null(_at_x(scene, 1300))
	gm._try_reset()
	assert_null(_at_x(scene, 1300), "reset: the world as built")
	assert_eq(scene.surfaces.map(func(s): return s.id), before)
	assert_eq(scene.get_children().filter(func(n): return String(n.name).begins_with("Spawned")), [], "no stray nodes")

func test_stage107_plans_on_surfaces_that_left_are_dropped():
	var scene := _level([_host()])
	await get_tree().process_frame
	var gm = scene.get_node("GameManager")
	var host := _at_x(scene, 900)
	gm.plan.add_entry(host.id, Side.Value.LEFT, Vector2(900, 540))
	_shoot(scene, Vector2(700, 540), Vector2(850, 540))
	assert_true(gm.plan.is_empty(), "the spent host's entry is dropped")

func test_stage107_a_spawned_mirror_can_be_planned():
	var scene := _level([_host("pass", _mirror_template())])
	await get_tree().process_frame
	var gm = scene.get_node("GameManager")
	_shoot(scene, Vector2(700, 540), Vector2(800, 540))
	var mirror := _at_x(scene, 1300)
	assert_not_null(mirror)
	scene.get_node("Cursor").global_position = Vector2(1290, 540)
	var r: Dictionary = gm._detect_click()
	assert_eq(r.get("surface"), mirror, "the spawned mirror is clickable")
	var next := _shoot(scene, Vector2(700, 540), Vector2(800, 540))
	assert_eq(next.hits[0]["surface_id"], mirror.id)
	assert_true(next.hits.size() > 1, "it reflects")

func test_stage107_the_host_path_effect_applies_and_it_spawns():
	for type in ["reflection", "terminal"]:
		var scene := _level([_host(type)])
		await get_tree().process_frame
		var host := _at_x(scene, 900)
		var shot := _shoot(scene, Vector2(700, 540), Vector2(800, 540))
		assert_eq(shot.hits[0]["surface_id"], host.id)
		if type == "terminal":
			assert_eq(shot.hits.size(), 1, "the terminal host stops the shot")
		else:
			assert_lt(shot.hits[1]["point"].x, 900.0, "the reflective host sends it back (to the left wall)")
		assert_not_null(_at_x(scene, 1300), "%s host: spawned too" % type)
		scene.queue_free()
		await get_tree().process_frame

func test_stage107_a_portal_host_teleports_and_spawns():
	var scene := _level([_host("portal", null, {"theta": 0.0, "d": [0.0, -200.0]})])
	await get_tree().process_frame
	var src := _at_x(scene, 900)
	var shot := _shoot(scene, Vector2(700, 540), Vector2(800, 540))
	assert_eq(shot.hits[0]["surface_id"], src.id)
	assert_not_null(_at_x(scene, 1300), "spawned at the source hit")
	var gm = scene.get_node("GameManager")
	var pair: Array = scene.surfaces.filter(func(s): return not s.presence.is_empty() and s.segment.start.coords.x == 900)
	assert_eq(pair.size(), 2, "the portal pair shares the host presence")
	for s in pair:
		assert_false(s.present(gm.game_state.flags), "both ends are spent")

func test_stage107_structural_changes_invalidate_the_defs_cache():
	var scene := _level([_host()])
	await get_tree().process_frame
	var gm = scene.get_node("GameManager")
	var v0: int = gm._defs_cache.version_of(scene.surfaces)
	_shoot(scene, Vector2(700, 540), Vector2(800, 540))
	assert_gt(gm._defs_cache.version_of(scene.surfaces), v0, "a new world, new defs")
	assert_true(SceneDefs.defs_from_surfaces(scene.surfaces).any(func(d): return d.has("presence") and d["presence"][0]["values"] == ["g1"]))

func test_stage107_targets_register_by_flag():
	var t := _side("pass", false, [{"kind": "target"}])
	var scene := _level([SurfaceSpec.line(Vector2(900, 300), Vector2(900, 780), t, t)])
	await get_tree().process_frame
	var target := _at_x(scene, 900)
	var shot := _shoot(scene, Vector2(700, 540), Vector2(800, 540))
	var gm = scene.get_node("GameManager")
	assert_eq(gm.targets_hit, {target.id: true})
	assert_true(shot.targets_hit.has(target.id), "the same target the solver's on-segment rule counts")
	gm._try_undo()
	assert_eq(gm.targets_hit, {}, "undone")
