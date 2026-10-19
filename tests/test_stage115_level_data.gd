extends GutTest

# Stage 115.2: the levels are data (data/levels/*.json, rc-level/1). A level
# scene points at its data (level_path) and keeps only its nodes; until the
# nodes are made from the data too (the Godot host, a later stage), this pins
# that they agree with it: the player's start, the background, the camera,
# the colliders and which game nodes the level has. The surfaces themselves
# are pinned by the native parity test (rcg_tests: the data built natively
# equals the level dumps) and the session replay golden (bench/sessions).

func _level_scenes() -> Array[String]:
	var out: Array[String] = []
	for d in ["res://scenes/", "res://scenes/test_levels/", "res://scenes/playground/"]:
		for f in DirAccess.get_files_at(d):
			if f.ends_with(".tscn") and FileAccess.get_file_as_string(d + f).contains("level_settings.gd"):
				out.append(d + f)
	out.sort()
	return out

func _data_path(scene_path: String) -> String:
	return "res://data/levels/" + scene_path.trim_prefix("res://scenes/").trim_suffix(".tscn") + ".json"

func test_every_level_scene_points_at_its_data():
	var n := 0
	for path in _level_scenes():
		var scene: Node = load(path).instantiate()
		if path.ends_with("combo_base.tscn"):
			assert_eq(scene.level_path, "", "combo_base keeps its arrays (configured at run time)")
		else:
			assert_eq(scene.level_path, _data_path(path), path)
			n += 1
		scene.free()
	assert_eq(n, 36)

func test_the_scene_nodes_agree_with_the_data():
	for path in _level_scenes():
		if path.ends_with("combo_base.tscn"):
			continue
		var data := LevelData.load_data(_data_path(path))
		assert_false(data.is_empty(), path)
		var scene: Node = load(path).instantiate()
		var player: Node2D = scene.get_node_or_null("Player")
		assert_eq(player.position if player else null, LevelData.vec(data["player"]["start"]) if data["player"] != null else null, path + ": player")
		var bg: ColorRect = scene.get_node_or_null("Background")
		if data["background"] == null:
			assert_null(bg, path)
		else:
			var r := LevelData.rect(data["background"]["rect"])
			assert_eq(Rect2(bg.offset_left, bg.offset_top, bg.offset_right - bg.offset_left, bg.offset_bottom - bg.offset_top), r, path + ": background")
			var c: Array = data["background"]["color"]
			assert_eq(bg.color, Color(c[0], c[1], c[2], c[3]), path + ": background colour")
		var cam: Camera2D = scene.get_node_or_null("Camera")
		assert_eq(cam != null, data["camera"] != null, path + ": camera")
		if cam != null:
			assert_eq(cam.position, LevelData.vec(data["camera"]["start"]), path)
			assert_eq(cam.position_smoothing_enabled, data["camera"]["smoothing"], path)
			assert_eq(cam.position_smoothing_speed, data["camera"]["smoothing_speed"], path)
		var colliders := []
		for child in scene.get_children():
			if child is StaticBody2D:
				for c in child.get_children():
					colliders.append([child.position + c.position + c.shape.a, child.position + c.shape.b + c.position])
		assert_eq(colliders.size(), data["colliders"].size(), path + ": colliders")
		for i in mini(colliders.size(), data["colliders"].size()):
			assert_eq(colliders[i], [LevelData.vec(data["colliders"][i]["a"]), LevelData.vec(data["colliders"][i]["b"])], path)
		var f: Dictionary = data["features"]
		assert_eq([scene.has_node("GameManager"), scene.has_node("PathRenderer"), scene.has_node("ArrowAnimator"),
			scene.has_node("PlanHUD"), scene.has_node("Cursor")], [f["game"], f["preview"], f["arrow"], f["hud"], f["cursor"]], path + ": nodes")
		scene.free()

func test_a_level_is_read_natively_and_checked():
	var bad: Dictionary = RcgLevel.parse("{\"schema\": \"rc-level/1\", \"surfaces\": [{\"ids\": [1], \"start\": [0, 0]}]}")
	assert_true(bad.has("error"))
	assert_string_contains(bad["error"], "'end' required")
	var ok: Dictionary = RcgLevel.parse(FileAccess.get_file_as_string("res://data/levels/test_levels/portals.json"))
	assert_false(ok.has("error"))
	# numbers as the file spells them, read natively: ints stay ints, floats are float64
	var s0: Dictionary = ok["level"]["surfaces"][0]
	assert_eq(typeof(s0["ids"][0]), TYPE_INT)
	assert_eq(typeof(s0["start"][0]), TYPE_FLOAT)

func test_a_level_built_from_data_matches_its_dump():
	# the defs the game builds from the data are the level dump (bench/scenes)
	Surface.reset_id_counter()
	var scene: Node = load("res://scenes/test_levels/hit_effects.tscn").instantiate()
	add_child_autofree(scene)
	var dump = JSON.parse_string(FileAccess.get_file_as_string("res://bench/scenes/test_levels__hit_effects.surfaces.json"))
	var defs := SceneDefs.defs_from_surfaces(scene.surfaces)
	assert_eq(defs.size(), dump["surfaces"].size())
	for i in defs.size():
		assert_eq(defs[i]["id"], int(dump["surfaces"][i]["id"]))
		assert_eq(Vector2(defs[i]["start"][0], defs[i]["start"][1]), Vector2(dump["surfaces"][i]["start"][0], dump["surfaces"][i]["start"][1]))
