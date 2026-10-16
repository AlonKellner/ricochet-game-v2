extends GutTest

# Stage 107: the spawn preview is the landing (GAME_SPEC 10.6; principle 2):
# over a grid of aims, what the preview shows is bit for bit what the shot
# creates when it lands, and the surfaces it marks are the ones that leave.
# The preview writes nothing (principle 25): not the world, not the flags,
# not even the surface id counter.

const M := SpawnFrame.Mode
const LEVEL := "res://scenes/test_levels/combo_base.tscn"

func _side(type := "pass", interactive := false, hits: Array = []) -> SideSpec:
	return SideSpec.make({"type": type}, interactive, hits)

func _level() -> Node:
	Surface.reset_id_counter()
	var scene: Node = load(LEVEL).instantiate()
	var mirror := SpawnTemplate.make(SurfaceSpec.line(Vector2(-40, 120), Vector2(40, 120), _side("reflection", true),
		_side("reflection", true)), M.CARRIER_NORMAL)
	var block := SpawnTemplate.make(SurfaceSpec.circle(Vector2(150, 0), 30.0, _side("terminal"), _side("terminal")), M.BEAM_REFLECTED)
	var h1 := _side("pass", true, [{"kind": "spawn", "channel": "a", "template": mirror}])
	var h2 := _side("reflection", true, [{"kind": "spawn", "channel": "b", "template": block}])
	var r := _side("pass", true, [{"kind": "reset", "channels": ["a", "b"]}])
	scene.surface_specs.append(SurfaceSpec.line(Vector2(900, 250), Vector2(900, 750), h1, h1))
	scene.surface_specs.append(SurfaceSpec.arc(Vector2(1300, 300), Vector2(1300, 700), Vector2(1450, 500), h2, h2))
	scene.surface_specs.append(SurfaceSpec.line(Vector2(600, 150), Vector2(1100, 150), r, r))
	add_child_autofree(scene)
	return scene

func _key(seg: Segment) -> Array:
	return [seg.start.coords, seg.end.coords, seg.via.coords, seg.full]

func test_stage107_preview_equals_landing():
	var scene := _level()
	await get_tree().process_frame
	var gm = scene.get_node("GameManager")
	var renderer = scene.get_node("PathRenderer")
	var preview: SpawnPreviewNode = scene.get_node("SpawnPreview")
	var compared := 0
	var spawned := 0
	for px in [400.0, 650.0]:
		for py in [300.0, 500.0, 700.0]:
			for dir: Vector2 in [Vector2(1, 0), Vector2(1, 0.25), Vector2(1, -0.375), Vector2(-0.5, -1), Vector2(1, 0.75)]:
				# a first shot (so the preview also sees a world with groups), then the probe
				for shot in 2:
					var player := Vector2(px, py)
					var cursor: Vector2 = player + dir * 64.0
					scene.get_node("Player").global_position = player
					scene.get_node("Cursor").global_position = cursor
					renderer._compute_trace()
					var ids_before: int = Surface._next_id
					var snap: Dictionary = scene.world.snapshot()
					var flags_before: Dictionary = gm.game_state.flags.duplicate(true)
					preview.recompute()
					assert_eq(Surface._next_id, ids_before, "the preview builds no surface")
					assert_eq(scene.world.snapshot(), snap, "the preview leaves the world alone")
					assert_eq(gm.game_state.flags, flags_before, "and the flags")
					var want: Array = preview.placed.map(func(p): return [p["channel"], p["generation"], _key(p["spec"].segment())])
					var vanish: Array = preview.vanishing.map(func(s): return s.id)
					var present_before: Array = scene.surfaces.filter(func(s): return s.present(gm.game_state.flags))
					var before: Array = scene.surfaces.duplicate()
					gm._try_fire()
					get_tree().paused = false
					var got := []
					for ch in ["a", "b"]:
						for s: Surface in scene.world.group_of(ch):
							if not before.has(s):
								got.append([ch, scene.world.generations[ch], _key(s.segment)])
					# a portal group has two surfaces; these templates have one each
					assert_eq(got, want, "player %s aim %s: the preview is the landing" % [player, dir])
					var left: Array = []
					for s: Surface in present_before:
						if not s.presence.is_empty() and not (scene.surfaces.has(s) and s.present(gm.game_state.flags)):
							left.append(s)
					assert_eq(left.map(func(s): return s.id), vanish, "the marked surfaces are the ones that leave")
					compared += 1
					spawned += want.size()
				gm._try_undo()
				gm._try_undo()
	assert_gt(spawned, 5, "the grid spawns (%d of %d shots)" % [spawned, compared])
