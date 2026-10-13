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
	var packed: PackedScene = load(path)
	var scene: Node = packed.instantiate()
	if not ("surfaces" in scene):
		scene.free()
		return null
	root.add_child(scene)
	await process_frame
	# the solver's own conversion (BeamTracer feeds the same defs to the solver)
	var out: Array = BeamScene.defs_from_surfaces(scene.surfaces)
	scene.queue_free()
	await process_frame
	return out
