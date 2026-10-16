extends SceneTree

# Headless: writes the Stage 107 interactive level (spawn and reset, every
# placement mode, a portal host, a shared channel, a combined side, a
# categorical host, an in-shot and a deferred reset) from surface specs.
#   godot --headless --path . -s res://scripts/testing/make_hit_effects_level.gd
# Writes res://scenes/stage107/hit_effects.tscn.

const OUT := "res://scenes/stage107/hit_effects.tscn"
const M := SpawnFrame.Mode

func _side(type := "pass", interactive := false, hits: Array = [], params := {}) -> SideSpec:
	var e := {"type": type}
	e.merge(params)
	return SideSpec.make(e, interactive, hits)

func _spawn(channel: String, template: SpawnTemplate) -> Dictionary:
	return {"kind": "spawn", "channel": channel, "template": template, "deferred": false}

func _mirror(a: Vector2, b: Vector2) -> SurfaceSpec:
	return SurfaceSpec.line(a, b, _side("reflection", true), _side("reflection", true))

func _two(s: SideSpec, a: Vector2, b: Vector2) -> SurfaceSpec:
	return SurfaceSpec.line(a, b, s, s)

func _specs() -> Array[SurfaceSpec]:
	var out: Array[SurfaceSpec] = []
	# the four required modes, on the column x = 600 (each its own channel)
	out.append(_two(_side("pass", true, [_spawn("a", SpawnTemplate.make(_mirror(Vector2(100, 0), Vector2(220, 0)), M.BEAM_TANGENT))]),
		Vector2(600, 150), Vector2(600, 290)))
	out.append(_two(_side("pass", true, [_spawn("b", SpawnTemplate.make(_mirror(Vector2(-60, -150), Vector2(60, -150)), M.BEAM_NORMAL))]),
		Vector2(600, 320), Vector2(600, 460)))
	out.append(_two(_side("terminal", true, [_spawn("c", SpawnTemplate.make(_mirror(Vector2(-50, -120), Vector2(50, -120)), M.CARRIER_TANGENT))]),
		Vector2(600, 490), Vector2(600, 630)))
	var inv := SurfaceSpec.circle(Vector2(120, 0), 30.0, _side("inversion", true), _side("inversion", true))
	var d := _side("reflection", true, [_spawn("d", SpawnTemplate.make(inv, M.CARRIER_NORMAL))])
	out.append(SurfaceSpec.arc(Vector2(370, 750), Vector2(530, 750), Vector2(450, 670), d, d))
	# a portal host (to x = 1300) spawning a one-sided mirror, mirrored by the side hit
	var one_sided := SurfaceSpec.line(Vector2(80, -40), Vector2(80, 40), _side("reflection", true), _side())
	var e := _side("portal", true, [_spawn("e", SpawnTemplate.make(one_sided, M.BEAM_TANGENT, true))], {"theta": 0.0, "d": [300.0, 0.0]})
	out.append(_two(e, Vector2(1000, 150), Vector2(1000, 300)))
	# two hosts sharing the channel f: a block circle 150 to the right of the hit
	var block := SurfaceSpec.circle(Vector2(150, 0), 25.0, _side("terminal"), _side("terminal"))
	var f := _side("pass", true, [_spawn("f", SpawnTemplate.make(block, M.WORLD))])
	out.append(_two(f, Vector2(1100, 350), Vector2(1100, 450)))
	out.append(_two(f, Vector2(1100, 480), Vector2(1100, 580)))
	# one side, three hit effects: a target, a toggle of "open", a spawn
	var g := _side("pass", true, [{"kind": "target"}, {"kind": "state", "key": "open", "op": "toggle", "value": true},
		_spawn("g", SpawnTemplate.make(_mirror(Vector2(60, -50), Vector2(60, 50)), M.CARRIER_NORMAL))])
	out.append(_two(g, Vector2(1100, 620), Vector2(1100, 760)))
	# a categorical host: while "open" it is a pass that spawns, else a block
	var h := SurfaceSpec.line(Vector2(1350, 400), Vector2(1350, 600), null, null)
	h.resolver_key = "open"
	var hh := _side("pass", true, [_spawn("h", SpawnTemplate.make(_mirror(Vector2(100, -40), Vector2(100, 40)), M.BEAM_REFLECTED))])
	h.resolver_table = {"true": [hh, hh], "DEFAULT": [_side("terminal"), _side("terminal")]}
	out.append(h)
	# resets: in the shot (a, b, c, d) and deferred to landing (e, f, g, h)
	var r1 := _side("pass", true, [{"kind": "reset", "channels": ["a", "b", "c", "d"], "deferred": false}])
	out.append(_two(r1, Vector2(300, 850), Vector2(900, 850)))
	var r2 := _side("pass", true, [{"kind": "reset", "channels": ["e", "f", "g", "h"], "deferred": true}])
	out.append(_two(r2, Vector2(1600, 200), Vector2(1600, 800)))
	return out

func _init() -> void:
	var root := Node2D.new()
	root.name = "Main"
	root.set_script(load("res://scripts/game/level_settings.gd"))
	root.gravity = Vector2.ZERO
	root.room_rect = Rect2(200, 100, 1500, 800)
	root.build_room = true
	root.state_key = "open"
	for s in _specs():
		var errors := s.validate()
		assert(errors.is_empty(), ", ".join(errors))
		root.surface_specs.append(s)
	var bg := ColorRect.new()
	bg.name = "Background"
	bg.size = Vector2(1920, 1080)
	bg.color = Color(0.1, 0.1, 0.1, 1)
	_child(root, bg)
	var player: Node2D = load("res://scenes/player.tscn").instantiate()
	player.name = "Player"
	player.position = Vector2(300, 500)
	_child(root, player)
	for pair in [["Cursor", "res://scripts/game/cursor.gd", Node2D], ["PathRenderer", "res://scripts/visual/path_renderer.gd", Node2D],
			["ArrowAnimator", "res://scripts/game/arrow_animator.gd", Node2D], ["GameManager", "res://scripts/game/game_manager.gd", Node],
			["PlanHUD", "res://scripts/game/plan_hud.gd", Control]]:
		var n: Node = pair[2].new()
		n.name = pair[0]
		n.set_script(load(pair[1]))
		if pair[0] == "ArrowAnimator":
			n.z_index = 30
		_child(root, n)
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(OUT.get_base_dir()))
	var packed := PackedScene.new()
	var err := packed.pack(root)
	assert(err == OK, "pack: %d" % err)
	err = ResourceSaver.save(packed, OUT)
	assert(err == OK, "save: %d" % err)
	print("wrote ", OUT)
	root.free()
	quit()

func _child(root: Node, n: Node) -> void:
	root.add_child(n)
	n.owner = root
