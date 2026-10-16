extends GutTest

# Stage 107: the interactive level (scenes/test_levels/hit_effects.tscn, made by
# scripts/testing/make_hit_effects_level.gd) loads, builds every spec, plays
# its scripted interactions, and passes the invariant sweep -- as built and
# after spawns landed (presence-gated groups and spent hosts in the scene).

const LEVEL := "res://scenes/test_levels/hit_effects.tscn"

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

func _shoot(scene: Node, player: Vector2, cursor: Vector2) -> void:
	var gm = scene.get_node("GameManager")
	scene.get_node("Player").global_position = player
	scene.get_node("Cursor").global_position = cursor
	gm._try_fire()
	gm._on_flight_completed()   # the level has an arrow: land at once
	scene.get_node("ArrowAnimator").set_process(false)
	get_tree().paused = false

func test_stage107_level_builds():
	var scene := _level()
	await get_tree().process_frame
	assert_not_null(scene.world)
	assert_not_null(scene.get_node_or_null("SpawnPreview"), "a level that can spawn previews it")
	assert_eq(scene.surface_specs.size(), 11)
	for spec: SurfaceSpec in scene.surface_specs:
		assert_eq(spec.validate(), [] as Array[String])
	var portal := _at(scene, Vector2(1000, 150))
	assert_not_null(portal)
	assert_eq(scene.surfaces.filter(func(s): return s.segment.start.coords == Vector2(1300, 150)).size(), 1, "the portal's image")

func test_stage107_level_scripted_interactions():
	var scene := _level()
	await get_tree().process_frame
	var gm = scene.get_node("GameManager")
	var gate := _at(scene, Vector2(1350, 400))
	assert_eq(gate.active_side_config(Side.Value.LEFT, gm.game_state).effect.to_spec()["type"], "terminal", "closed")
	# through host A (beam tangent): a mirror along the shot
	_shoot(scene, Vector2(300, 220), Vector2(400, 220))
	assert_eq(gm.game_state.flags.get("chan:a"), "g1")
	var mirror: Array = scene.world.group_of("a")
	assert_eq(mirror.size(), 1)
	assert_almost_eq(mirror[0].segment.start.coords, Vector2(700, 220), Vector2(1e-3, 1e-3))
	# through the combined side G (target + toggle + spawn), then the gate opens
	_shoot(scene, Vector2(800, 690), Vector2(900, 690))
	assert_true(gm.targets_hit.has(_at(scene, Vector2(1100, 620)).id), "the target registered")
	assert_eq(gm.game_state.flags.get("open"), true, "the toggle")
	assert_eq(scene.world.group_of("g").size(), 1, "and the spawn (the shot passes under the deferred reset)")
	assert_eq(gate.active_side_config(Side.Value.LEFT, gm.game_state).hit_effects.size(), 1, "the gate is now a spawning pass")
	# the in-shot reset along the floor re-arms a: the mirror goes, host A is back
	_shoot(scene, Vector2(350, 800), Vector2(450, 900))
	assert_eq(gm.game_state.flags.get("chan:a"), "armed")
	assert_true(scene.world.group_of("a").is_empty())
	assert_true(_at(scene, Vector2(600, 150)).present(gm.game_state.flags))

# K5 (fixed) without any Stage 107 feature: the legacy portal_lines and a pass
# line whose carrier runs through the portal image's departure point.
func test_stage107_k5_pinned_portal_departure_on_a_carrier():
	Surface.reset_id_counter()
	var level := Node2D.new()
	level.set_script(load("res://scripts/game/level_settings.gd"))
	level.build_room = false
	level.portal_lines = PackedFloat64Array([1000, 150, 1000, 300, 0.0, 300.0, 0.0])
	level.passthrough_lines.append(Vector4(1300, 400, 1300, 600))   # the image's carrier, below it
	add_child_autofree(level)
	var pair: BeamTracerPair = BeamTracer.new().trace_pair(SceneDefs.defs_from_surfaces(level.surfaces),
		Vector2(700, 200), Vector2(800, 210), [], {}, {})
	var steps: Array = pair.physical.steps
	var after := -1
	for i in steps.size():
		if steps[i].start.x == 1300.0:
			after = i
			break
	assert_gt(after, 0, "the beam teleported to the image")
	if after > 0:
		assert_true(steps[after].after_portal, "K5 (fixed): the step after the teleport keeps its after_portal mark")
