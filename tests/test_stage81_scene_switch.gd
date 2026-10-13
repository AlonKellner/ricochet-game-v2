extends GutTest

# Stage 81: every scene made of mirrors, inversions, blocks and pass-throughs
# is solved by the beam solver (Stage 82: portals; Stage 83: projectives --
# every scene, see test_stage83_projective::test_every_scene_uses_beam).

const BEAM_SCENES := [
	"res://scenes/main.tscn",
	"res://scenes/open_room_playable.tscn",
	"res://scenes/playground/inversion_grid.tscn",
	"res://scenes/playground/plannable_inversion.tscn",
	"res://scenes/playground/two_sided.tscn",
	"res://scenes/test_levels/circle_no_walls.tscn",
	"res://scenes/test_levels/combo_base.tscn",
	"res://scenes/test_levels/divergence_room.tscn",
	"res://scenes/test_levels/empty_room.tscn",
	"res://scenes/test_levels/full_circle_mirror.tscn",
	"res://scenes/test_levels/mirror_and_wall.tscn",
	"res://scenes/test_levels/open_room.tscn",
	"res://scenes/test_levels/parallel_mirrors.tscn",
	"res://scenes/test_levels/room_boundaries.tscn",
	"res://scenes/test_levels/room_with_inversion.tscn",
	"res://scenes/test_levels/room_with_mirror.tscn",
	"res://scenes/test_levels/room_with_passthroughs.tscn",
	"res://scenes/test_levels/semi_circle_mirror.tscn",
	"res://scenes/test_levels/three_mirrors.tscn",
	"res://scenes/test_levels/two_mirrors.tscn",
	"res://scenes/test_levels/two_sided_mirror.tscn",
	"res://scenes/playground/portals.tscn",       # Stage 82
	"res://scenes/test_levels/portals.tscn",      # Stage 82
	"res://scenes/playground/projective.tscn",    # Stage 83
	"res://scenes/playground/projective_arcs.tscn",
	"res://scenes/playground/projective_lines.tscn",
	"res://scenes/playground/directional_arcs.tscn",
	"res://scenes/playground/directional_lines.tscn",
]

func _solver_for(path: String) -> String:
	BeamTracer.clear_cache()
	Surface.reset_id_counter()
	var scene: Node = load(path).instantiate()
	add_child_autofree(scene)
	await get_tree().process_frame
	scene.get_node("Player").global_position = Vector2(700, 560)
	scene.get_node("Cursor").global_position = Vector2(1130, 470)
	var renderer := scene.get_node("PathRenderer")
	renderer._compute_trace()
	var supported := BeamTracer.scene_supported(scene.surfaces)
	var solver: String = renderer.last_solver
	return ("%s/%s" % [solver, "supported" if supported else "unsupported"])

func test_mirror_scenes_use_beam():
	for path in BEAM_SCENES:
		assert_eq(await _solver_for(path), "beam/supported", path)


# (moved from test_solver_parity.gd, deleted in Stage 84) the game-side
# conversion the solver uses equals the dumped scene data the oracle traced
func test_scene_defs_match_dump():
	for fn in DirAccess.open("res://bench/scenes").get_files():
		var dump: Dictionary = JSON.parse_string(FileAccess.open("res://bench/scenes/" + fn, FileAccess.READ).get_as_text())
		Surface.reset_id_counter()
		var scene: Node = load(dump["scene"]).instantiate()
		add_child_autofree(scene)
		await get_tree().process_frame
		if not ("surfaces" in scene):
			continue
		var defs: Array = BeamScene.defs_from_surfaces(scene.surfaces)
		assert_true(JSON.parse_string(JSON.stringify(defs, "", false, true)) == dump["surfaces"], fn)
