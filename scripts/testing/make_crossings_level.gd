extends SceneTree

# Headless: writes the Stage 108 interactive level (crossing design surfaces
# and their pieces, a spawner whose spawns get trimmed) from surface specs.
#   godot --headless --path . -s res://scripts/testing/make_crossings_level.gd
# Writes res://scenes/stage108/crossings.tscn.

const OUT := "res://scenes/stage108/crossings.tscn"
const M := SpawnFrame.Mode

func _side(type := "pass", interactive := false, hits: Array = []) -> SideSpec:
	return SideSpec.make({"type": type}, interactive, hits)

func _mirror(a: Vector2, b: Vector2) -> SurfaceSpec:
	return SurfaceSpec.line(a, b, _side("reflection", true), _side("reflection", true))

func _block(a: Vector2, b: Vector2) -> SurfaceSpec:
	return SurfaceSpec.line(a, b, _side("terminal"), _side("terminal"))

func _specs() -> Array[SurfaceSpec]:
	var out: Array[SurfaceSpec] = []
	# 4 pieces: a mirror crossed by a block
	out.append(_mirror(Vector2(600, 200), Vector2(600, 450)))
	out.append(_block(Vector2(520, 330), Vector2(680, 330)))
	# 7 pieces: a mirror crossed by two blocks
	out.append(_mirror(Vector2(450, 600), Vector2(850, 600)))
	out.append(_block(Vector2(550, 540), Vector2(550, 660)))
	out.append(_block(Vector2(750, 540), Vector2(750, 660)))
	# 9 pieces: three mirrors crossing in a triangle
	out.append(_mirror(Vector2(880, 250), Vector2(1120, 250)))
	out.append(_mirror(Vector2(890, 230), Vector2(1010, 470)))
	out.append(_mirror(Vector2(1110, 230), Vector2(990, 470)))
	# line x circle: a mirror circle cut by a block (2 arcs, 3 line pieces)
	out.append(SurfaceSpec.circle(Vector2(1400, 300), 90.0, _side("reflection", true), _side("reflection", true)))
	out.append(_block(Vector2(1400, 180), Vector2(1400, 420)))
	# circle x circle: an inversion circle and a mirror circle (2 arcs each)
	out.append(SurfaceSpec.circle(Vector2(1350, 650), 80.0, _side("inversion", true), _side("inversion", true)))
	out.append(SurfaceSpec.circle(Vector2(1450, 650), 80.0, _side("reflection", true), _side("reflection", true)))
	# a spawner: a 300 px mirror across the shot, centred on the hit -- near
	# the floor it is trimmed to the piece holding its midpoint
	var t := SpawnTemplate.make(_mirror(Vector2(-150, 0), Vector2(150, 0)), M.BEAM_NORMAL)
	var host := _side("pass", true, [{"kind": "spawn", "channel": "t", "template": t, "deferred": false}])
	out.append(SurfaceSpec.line(Vector2(450, 700), Vector2(450, 860), host, host))
	var reset := _side("pass", true, [{"kind": "reset", "channels": ["t"], "deferred": false}])
	out.append(SurfaceSpec.line(Vector2(250, 880), Vector2(400, 880), reset, reset))
	return out

func _init() -> void:
	var root := Node2D.new()
	root.name = "Main"
	root.set_script(load("res://scripts/game/level_settings.gd"))
	root.gravity = Vector2.ZERO
	root.room_rect = Rect2(200, 100, 1500, 800)
	root.build_room = true
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
