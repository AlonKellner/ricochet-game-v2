extends GutTest
## Stage 95 differential test: NPrecisionLadder / NErrorEstimate against
## PrecisionLadder / ErrorEstimate on the oracle corpus (bench/scenes): the
## accepted tiers, events, error estimates, sensitivities and climb records
## must be identical. Every SAMPLE_EVERY-th scenario by default; DIFF_ALL=1
## runs them all.

const Diff := preload("res://tests/native/diff_helpers.gd")
const SAMPLE_EVERY := 5
const TOL := 0.01


func _scenarios() -> Array:
	PrecisionBench.use_corpus("")
	var names := PrecisionBench.list_scenarios()
	if OS.get_environment("DIFF_ALL") == "1":
		return names
	var out: Array = []
	for i in names.size():
		if i % SAMPLE_EVERY == 0 or names[i].begins_with("dump"):
			out.append(names[i])
	return out


func _inputs(name: String) -> Dictionary:
	var scn: Dictionary = PrecisionBench.load_json("%s/%s.json" % [PrecisionBench.SCN_DIR, name])
	return {
		"defs": PrecisionBench.scene_defs(scn),
		"player": [float(scn["player"][0]), float(scn["player"][1])],
		"cursor": [float(scn["cursor"][0]), float(scn["cursor"][1])],
		"plan": PrecisionBench.plan_items(scn),
	}


func test_corpus_available() -> void:
	assert_gt(_scenarios().size(), 20, "corpus scenarios")


func test_ladder_solve_identical() -> void:
	var mismatches: Array = []
	var n := 0
	for name in _scenarios():
		var inp := _inputs(name)
		var fsc := FastSolver.Scene.from_defs(inp["defs"])
		var a := PrecisionLadder.solve(inp["defs"], inp["player"], inp["cursor"], inp["plan"], 32, 32, TOL, false, fsc)
		var b := NPrecisionLadder.solve(inp["defs"], inp["player"], inp["cursor"], inp["plan"], 32, 32, TOL, false, null)
		var r := Diff.result_diff(a, b, name)
		if r != "":
			mismatches.append(r)
		n += 1
	assert_eq(mismatches.size(), 0, "ladder: %d of %d scenarios differ\n%s" % [mismatches.size(), n, "\n".join(mismatches)])


func test_ladder_verify_identical() -> void:
	# verify = true climbs every tier until convergence: the long form of the ladder
	var mismatches: Array = []
	var names := _scenarios()
	for i in range(0, names.size(), 3):
		var inp := _inputs(names[i])
		var a := PrecisionLadder.solve(inp["defs"], inp["player"], inp["cursor"], inp["plan"], 16, 16, TOL, true)
		var b := NPrecisionLadder.solve(inp["defs"], inp["player"], inp["cursor"], inp["plan"], 16, 16, TOL, true)
		var r := Diff.result_diff(a, b, names[i])
		if r != "":
			mismatches.append(r)
	assert_eq(mismatches.size(), 0, "ladder verify: %d scenarios differ\n%s" % [mismatches.size(), "\n".join(mismatches)])


func test_solve_physical_identical() -> void:
	var mismatches: Array = []
	for name in _scenarios():
		var inp := _inputs(name)
		var a := PrecisionLadder.solve_physical(inp["defs"], inp["player"], inp["cursor"], inp["plan"], 32, TOL)
		var b := NPrecisionLadder.solve_physical(inp["defs"], inp["player"], inp["cursor"], inp["plan"], 32, TOL)
		var r := Diff.result_diff(a, b, name)
		if r != "":
			mismatches.append(r)
	assert_eq(mismatches.size(), 0, "solve_physical: %d scenarios differ\n%s" % [mismatches.size(), "\n".join(mismatches)])


func test_sensitivities_identical() -> void:
	var mismatches: Array = []
	for name in _scenarios():
		var inp := _inputs(name)
		var fsc := FastSolver.Scene.from_defs(inp["defs"])
		var sp := ErrorEstimate.physical_sensitivity(inp["defs"], inp["player"], inp["cursor"], inp["plan"], 32, fsc)
		var np := NErrorEstimate.physical_sensitivity(inp["defs"], inp["player"], inp["cursor"], inp["plan"], 32)
		var r := Diff.sens_diff(sp, np, name + ".physical")
		if r == "":
			var sq := ErrorEstimate.planned_sensitivity(inp["defs"], inp["player"], inp["cursor"], inp["plan"], 32, fsc)
			var nq := NErrorEstimate.planned_sensitivity(inp["defs"], inp["player"], inp["cursor"], inp["plan"], 32)
			r = Diff.sens_diff(sq, nq, name + ".planned")
			if r == "":
				for eps in [NumCtx.f64().eps(), NumCtx.with_limbs(4).eps()]:
					r = Diff.floats_diff(ErrorEstimate.estimate(sp, eps), NErrorEstimate.estimate(np, eps), name + ".estimate")
					if r == "" and ErrorEstimate.unreliable_index(sp, eps) != NErrorEstimate.unreliable_index(np, eps):
						r = "%s: unreliable_index" % name
					if r != "":
						break
			if r == "" and ErrorEstimate.required_limbs(sp, TOL, PrecisionLadder.TIERS) != NErrorEstimate.required_limbs(np, TOL, NPrecisionLadder.tiers()):
				r = "%s: required_limbs" % name
		if r != "":
			mismatches.append(r)
	assert_eq(mismatches.size(), 0, "sensitivities: %d scenarios differ\n%s" % [mismatches.size(), "\n".join(mismatches)])


func test_replay_identical() -> void:
	# gains replayed along the 4-limb trajectory, in float64
	var mismatches: Array = []
	var names := _scenarios()
	for i in range(0, names.size(), 2):
		var inp := _inputs(names[i])
		var big := NumCtx.with_limbs(4)
		var nbig := NNumCtx.with_limbs(4)
		var ev_gd := BeamSolver.physical(big, BeamScene.from_defs(big, inp["defs"]), big.cn(inp["player"][0], inp["player"][1]),
			big.cn(inp["cursor"][0], inp["cursor"][1]), inp["plan"], 32)
		var ev_n := NBeamSolver.physical(nbig, NBeamScene.from_defs(nbig, inp["defs"]), nbig.cn(inp["player"][0], inp["player"][1]),
			nbig.cn(inp["cursor"][0], inp["cursor"][1]), inp["plan"], 32)
		var r := Diff.events_diff(ev_gd, ev_n, names[i] + ".big4")
		if r == "":
			var a := ErrorEstimate.replay(ev_gd, inp["defs"])
			var b := NErrorEstimate.replay(ev_n, inp["defs"])
			r = Diff.floats_diff(a.gain, b.gain, names[i] + ".replay.gain")
			if r == "" and not Diff.same_float(a.max_gain, b.max_gain):
				r = "%s.replay.max_gain" % names[i]
		if r != "":
			mismatches.append(r)
	assert_eq(mismatches.size(), 0, "replay: %d scenarios differ\n%s" % [mismatches.size(), "\n".join(mismatches)])


func test_event_distance_and_residuals_identical() -> void:
	var mismatches: Array = []
	var names := _scenarios()
	for i in range(0, names.size(), 2):
		var inp := _inputs(names[i])
		var fsc := FastSolver.Scene.from_defs(inp["defs"])
		var nsc := NFastSolverScene.from_defs(inp["defs"])
		var p := PackedFloat64Array(inp["player"])
		var c := PackedFloat64Array(inp["cursor"])
		var fq := FastSolver.planned(fsc, p, c, inp["plan"])
		var nq := NFastSolver.planned(nsc, p, c, inp["plan"])
		var r := Diff.floats_diff(PrecisionLadder.planned_residuals(fq.events, fsc.scene), NPrecisionLadder.planned_residuals(nq.events, nsc.scene),
			names[i] + ".residuals")
		if r == "":
			var big := NumCtx.with_limbs(4)
			var nbig := NNumCtx.with_limbs(4)
			var bq := BeamSolver.planned(big, BeamScene.from_defs(big, inp["defs"]), big.cn(inp["player"][0], inp["player"][1]),
				big.cn(inp["cursor"][0], inp["cursor"][1]), inp["plan"])
			var nbq := NBeamSolver.planned(nbig, NBeamScene.from_defs(nbig, inp["defs"]), nbig.cn(inp["player"][0], inp["player"][1]),
				nbig.cn(inp["cursor"][0], inp["cursor"][1]), inp["plan"])
			r = Diff.floats_diff(PrecisionLadder.event_distance(fq.events, bq), NPrecisionLadder.event_distance(nq.events, nbq), names[i] + ".distance")
		if r != "":
			mismatches.append(r)
	assert_eq(mismatches.size(), 0, "event_distance/residuals: %d scenarios differ\n%s" % [mismatches.size(), "\n".join(mismatches)])
