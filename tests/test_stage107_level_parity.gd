extends GutTest

# Stage 107 safety net: every shipped level's scene defs (what the beam solver
# sees) are pinned bit for bit, so the two-axis refactor (hit effects, surface
# specs, level desugaring) provably changes no level. Expected canons:
# tests/data/stage107_level_defs.json. Re-bless (only for a reviewed,
# deliberate format change) with RC_STAGE107_BLESS=1.

const FIXTURE := "res://tests/data/stage107_level_defs.json"
const DIRS := ["res://scenes/test_levels/", "res://scenes/playground/"]

func _scenes() -> Array[String]:
	var out: Array[String] = []
	for d in DIRS:
		out.append_array(SweepPlans.discover_scenes(d, []))
	out.sort()
	return out

func _canon_of(scene_path: String) -> String:
	Surface.reset_id_counter()
	var scene: Node = load(scene_path).instantiate()
	scene.gravity = Vector2.ZERO
	add_child(scene)
	var c := DefsCanon.canon(SceneDefs.defs_from_surfaces(scene.surfaces))
	remove_child(scene)
	scene.free()
	return c

func test_stage107_level_defs_unchanged() -> void:
	var got := {}
	for path in _scenes():
		got[path] = _canon_of(path)
	assert_gt(got.size(), 20, "found the level scenes")
	if OS.get_environment("RC_STAGE107_BLESS") == "1":
		var f := FileAccess.open(FIXTURE, FileAccess.WRITE)
		f.store_string(JSON.stringify(got, " ", true))
		f.close()
		pass_test("blessed %d scenes" % got.size())
		return
	var f := FileAccess.open(FIXTURE, FileAccess.READ)
	assert_not_null(f, "fixture exists")
	if f == null:
		return
	var want: Dictionary = JSON.parse_string(f.get_as_text())
	assert_eq(got.keys(), want.keys(), "the same scenes")
	for path in want:
		if got.get(path, "") != want[path]:
			fail_test("%s: scene defs changed" % path)
