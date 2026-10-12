extends SceneTree

# Headless: dump the surfaces every scene builds, as JSON, for the oracle
# importer's round-trip test (docs/research/oracle/test_oracle_v2.py).
#   godot --headless --path . -s res://scripts/testing/dump_scene_surfaces.gd
# Writes bench/scenes/<dir>/<scene>.surfaces.json.

const SCENE_DIRS := ["res://scenes", "res://scenes/test_levels", "res://scenes/playground"]
const OUT_DIR := "res://bench/scenes"

func _init() -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(OUT_DIR))
	for dir_path in SCENE_DIRS:
		var dir := DirAccess.open(dir_path)
		if dir == null:
			continue
		for fn in dir.get_files():
			if not fn.ends_with(".tscn"):
				continue
			var path: String = dir_path + "/" + fn
			var surfaces = await _surfaces_of(path)
			if surfaces == null:
				continue
			var rel := path.trim_prefix("res://scenes/").trim_suffix(".tscn").replace("/", "__")
			var f := FileAccess.open("%s/%s.surfaces.json" % [OUT_DIR, rel], FileAccess.WRITE)
			f.store_string(JSON.stringify({"scene": path, "surfaces": surfaces}, " ", false, true))
			f.close()
	quit()

func _surfaces_of(path: String):
	Surface.reset_id_counter()
	MobiusTransform.reset_id_counter()
	var packed: PackedScene = load(path)
	var scene: Node = packed.instantiate()
	if not ("surfaces" in scene):
		scene.free()
		return null
	root.add_child(scene)
	await process_frame
	var out: Array = []
	for s in scene.surfaces:
		out.append(describe_surface(s))
	scene.queue_free()
	await process_frame
	return out

static func _v(p: Vector2):
	if is_inf(p.x) or is_inf(p.y):
		return null
	return [p.x, p.y]

static func describe_surface(s: Surface) -> Dictionary:
	var state := GameState.new()
	return {
		"id": s.id,
		"start": _v(s.segment.start.coords),
		"end": _v(s.segment.end.coords),
		"via": _v(s.segment.via.coords),
		"full": s.segment.full,
		"line": s.segment.get_carrier().is_line(),
		"left": describe_config(s.active_side_config(Side.Value.LEFT, state)),
		"right": describe_config(s.active_side_config(Side.Value.RIGHT, state)),
		"is_target": s.is_target,
		"player_solid": s.player_solid,
	}

static func describe_config(cfg: SideConfig):
	if cfg == null:
		return null
	var e: Effect = cfg.effect
	var d := {"interactive": cfg.interactive}
	if e == null:
		d["type"] = "pass"
	elif e is TerminalEffect:
		d["type"] = "terminal"
	elif e is CircleInversionEffect:
		d["type"] = "inversion"
	elif e is ReflectionEffect:
		d["type"] = "reflection"
	elif e is RigidMotionEffect:
		d["type"] = "portal"
		d["theta"] = e._theta
		d["d"] = [e._d.x, e._d.y]
	elif e is LineNormalProjection:
		d["type"] = "line_normal_projection"
		d["back"] = e._project_back
	elif e is CircleNormalProjection:
		d["type"] = "circle_normal_projection"
		d["back"] = e._project_back
	elif e is LineDirectionalProjection:
		d["type"] = "line_directional_projection"
		d["back"] = e._project_back
		d["normal"] = [e._normal.x, e._normal.y]
	elif e is CircleDirectionalProjection:
		d["type"] = "circle_directional_projection"
		d["back"] = e._project_back
		d["normal"] = [e._normal.x, e._normal.y]
	else:
		d["type"] = "unknown:" + str(e.get_script().get_global_name())
	return d
