extends GutTest

# End-to-end: the real inversion_grid scene, driven through PathRenderer
# (BeamTracer + PrecisionLadder), compared with the mpmath oracle corpus.
# Includes the user's F12 failure dump (plan 7,4,1,3,6,6,6) and deep plans.

const SCENE := "res://scenes/playground/inversion_grid.tscn"
const TOL := 0.01
const DEEP := ["grid60_chain_d10_007", "grid60_chain_d13_069", "grid60_chain_d15_014", "grid60_chain_d15_133", "grid60_mixed_d11_013"]

var _scene: Node

func before_each():
	Surface.reset_id_counter()
	_scene = load(SCENE).instantiate()
	add_child_autofree(_scene)
	await get_tree().process_frame

func _drive(name: String) -> Dictionary:
	PrecisionBench.use_corpus("")
	var scn: Dictionary = PrecisionBench.load_json("%s/%s.json" % [PrecisionBench.SCN_DIR, name])
	var exp: Dictionary = PrecisionBench.load_json("%s/%s.json" % [PrecisionBench.EXP_DIR, name])
	var gm := _scene.get_node("GameManager")
	gm.plan.clear()
	for e in scn["plan"]:
		gm.plan.add_entry(int(e["surface_id"]), Side.Value.LEFT if e["side"] == "LEFT" else Side.Value.RIGHT)
	_scene.get_node("Player").global_position = Vector2(scn["player"][0], scn["player"][1])
	_scene.get_node("Cursor").global_position = Vector2(scn["cursor"][0], scn["cursor"][1])
	var renderer := _scene.get_node("PathRenderer")
	renderer._compute_trace()
	return {"scn": scn, "exp": exp, "renderer": renderer}

# Hit/cursor steps of a displayed path vs the oracle event list (up to the
# first origin/escape). Returns the max point error, or INF on a mismatch.
func _cmp_events(path: TracePath, expected: Array, label: String) -> float:
	var events: Array = []
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		if s.surface_id >= 0 and s.hit != null:
			events.append({"kind": "hit", "id": s.surface_id, "left": s.hit_side == Side.Value.LEFT, "p": s.end})
		elif s.surface_id == BeamSolver.INFINITY_ID:
			# the passage of the frame's infinity (Stage 88/101): a point when finite
			events.append({"kind": "infinity", "id": s.surface_id, "left": false, "p": s.end})
		if path.cursor_index == i + 1:
			events.append({"kind": "cursor", "id": -1, "left": false, "p": s.end})
	var worst := 0.0
	for i in expected.size():
		var ex: Dictionary = expected[i]
		if ex["kind"] in ["origin", "escape", "max"]:
			break
		if i >= events.size():
			fail_test("%s: missing event %d" % [label, i])
			return INF
		var e: Dictionary = events[i]
		var same: bool = e["kind"] == ex["kind"]
		if same and ex["kind"] == "hit":
			same = e["id"] == int(ex["surface_id"]) and e["left"] == (ex["side"] == "LEFT")
		if not same:
			fail_test("%s: event %d is %s%d, expected %s%d" % [label, i, e["kind"], e["id"], ex["kind"], int(ex["surface_id"])])
			return INF
		if ex.has("x"):
			var d: float = e["p"].distance_to(Vector2(String(ex["x"]).to_float(), String(ex["y"]).to_float()))
			worst = maxf(worst, d)
	return worst

func test_user_dump_7_4_1_3_6_6_6_planned_and_physical():
	var r := _drive("dump_7_4_1_3_6_6_6_f32")
	assert_eq(r["renderer"].last_solver, "beam", "inversion grid uses the beam solver")
	var e_pl := _cmp_events(r["renderer"].get_planned_path(), r["exp"]["planned"], "planned")
	var e_ph := _cmp_events(r["renderer"].get_traced_path(), r["exp"]["physical"], "physical")
	assert_lt(e_pl, TOL, "planned trace within %.2f px of the oracle (got %f)" % [TOL, e_pl])
	assert_lt(e_ph, TOL, "physical trace within %.2f px of the oracle (got %f)" % [TOL, e_ph])

func test_planned_path_reaches_cursor_exactly():
	var r := _drive("dump_7_4_1_3_6_6_6_f32")
	var path: TracePath = r["renderer"].get_planned_path()
	assert_gt(path.cursor_index, 0, "cursor reached")
	var end: Vector2 = path.steps[path.cursor_index - 1].end
	var cursor := Vector2(r["scn"]["cursor"][0], r["scn"]["cursor"][1])
	assert_lt(end.distance_to(cursor), 1e-3, "planned trace ends on the cursor")

func test_deep_plans_match_oracle():
	for name in DEEP:
		var r := _drive(name)
		var e_pl := _cmp_events(r["renderer"].get_planned_path(), r["exp"]["planned"], name + " planned")
		var e_ph := _cmp_events(r["renderer"].get_traced_path(), r["exp"]["physical"], name + " physical")
		assert_lt(e_pl, TOL, "%s planned err %f" % [name, e_pl])
		assert_lt(e_ph, TOL, "%s physical err %f" % [name, e_ph])

func test_steps_carry_error_estimates():
	var r := _drive("grid60_chain_d15_014")
	var path: TracePath = r["renderer"].get_traced_path()
	for s in path.steps:
		if s.hit != null:
			assert_true(s.precision_bits >= 53, "precision recorded")
			assert_true(s.est_error >= 0.0 and s.est_error <= TOL, "estimated error %f within tol" % s.est_error)

func test_merged_view_aligned_until_first_divergence():
	var r := _drive("dump_7_4_1_3_6_6_6_f32")
	var typed: Array = r["renderer"].get_typed_steps()
	# the first five plan hits (7,4,1,3,6) are physical too: aligned (solid)
	var aligned := 0
	for s in typed:
		if s.type == StepTypes.Type.ALIGNED:
			aligned += 1
		else:
			break
	assert_gte(aligned, 5, "first planned steps aligned with physical")

func test_every_effect_type_is_supported():
	var supported: Array = [RoomBuilder.create_block_surface(Vector2(0, 0), Vector2(100, 0), Vector2(50, 0))]
	assert_true(BeamTracer.scene_supported(SceneDefs.defs_from_surfaces(supported)), "terminal blocks are supported (Stage 81)")
	var seg := Segment.from_coords(Vector2(0, 0), Vector2(0, 100), Vector2(0, 50))
	var proj := LineNormalProjection.new(false)
	var cfg := SideConfig.new(proj, true)
	var surf := Surface.new(seg, cfg, cfg, false, true)
	assert_true(BeamTracer.scene_supported(SceneDefs.defs_from_surfaces([surf])), "projections are supported (Stage 83)")
