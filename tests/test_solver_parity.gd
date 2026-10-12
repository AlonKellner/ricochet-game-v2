extends GutTest

# Stages 81-83: legacy Tracer vs beam solver on the real levels (scene corpus).
# Every shallow difference must be one where the beam solver agrees with the
# mpmath oracle (legacy precision loss or documented legacy quirk).

const CORPUS := "res://bench/scenes_corpus"

func _plan_entries(j: Array) -> Array:
	var out: Array = []
	for e in j:
		out.append(PlanManager.PlanEntry.new(int(e["surface_id"]), Side.Value.LEFT if e["side"] == "LEFT" else Side.Value.RIGHT))
	return out

func test_scene_defs_match_dump():
	# the game-side conversion the solver uses equals the dumped scene data
	# the oracle traced
	for fn in DirAccess.open("res://bench/scenes").get_files():
		var dump: Dictionary = JSON.parse_string(FileAccess.open("res://bench/scenes/" + fn, FileAccess.READ).get_as_text())
		Surface.reset_id_counter()
		MobiusTransform.reset_id_counter()
		var scene: Node = load(dump["scene"]).instantiate()
		add_child_autofree(scene)
		await get_tree().process_frame
		if not ("surfaces" in scene):
			continue
		var defs: Array = BeamScene.defs_from_surfaces(scene.surfaces)
		assert_true(JSON.parse_string(JSON.stringify(defs, "", false, true)) == dump["surfaces"], fn)

func test_parity_shallow_all_supported_scenes():
	var counts := {"same": 0, "legacy_wrong": 0, "unclassified": 0}
	var report := ""
	var files := DirAccess.open(CORPUS + "/scenarios").get_files()
	for fn in files:
		var scn: Dictionary = JSON.parse_string(FileAccess.open("%s/scenarios/%s" % [CORPUS, fn], FileAccess.READ).get_as_text())
		var ex: Dictionary = JSON.parse_string(FileAccess.open("%s/expected/%s" % [CORPUS, fn], FileAccess.READ).get_as_text())
		Surface.reset_id_counter()
		MobiusTransform.reset_id_counter()
		var scene: Node = load(scn["scene"]).instantiate()
		add_child_autofree(scene)
		await get_tree().process_frame
		var player := Vector2(scn["player"][0], scn["player"][1])
		var cursor := Vector2(scn["cursor"][0], scn["cursor"][1])
		var entries := _plan_entries(scn["plan"])
		if not BeamTracer.scene_supported(scene.surfaces):
			fail_test("%s is not supported" % fn)
			scene.queue_free()
			continue
		var defs := BeamScene.defs_from_surfaces(scene.surfaces)
		if defs.size() != scene.surfaces.size():
			fail_test("%s: scene defs" % fn)
			scene.queue_free()
			continue
		var legacy := SolverParity.legacy_events(scene.surfaces, player, cursor, entries)
		var res := PrecisionLadder.solve(defs, scn["player"], scn["cursor"],
			BeamTracer.plan_items(entries), 32, 32, BeamTracer.TOL)
		for mode in ["planned", "physical"]:
			var beam: Array = SolverParity.beam_events(res.planned if mode == "planned" else res.physical)
			var c := SolverParity.classify(legacy[mode], beam, SolverParity.oracle_events(ex[mode]))
			counts[c] += 1
			if c == "unclassified":
				report += "  %s %s\n" % [fn, mode]
		scene.queue_free()
	gut.p("parity: %s" % str(counts))
	assert_eq(counts["unclassified"], 0, "unclassified differences:\n" + report)
	assert_eq(counts["same"] + counts["legacy_wrong"], 2 * files.size())
