extends GutTest

# Stage 81: trace rules of the generalized beam solver (terminals, passes,
# MAX_HITS, one-sided and partial surfaces, endpoints/junctions, departure
# re-hit, targets). Expected events: the mpmath oracle (trace2 semantics),
# bench/fixtures/stage81_rules.json (docs/research/oracle/gen_fixtures_stage81.py)
# and the scene corpus bench/scenes_corpus (gen_scene_corpus.py).

const FIXTURE := "res://bench/fixtures/stage81_rules.json"
const CORPUS := "res://bench/scenes_corpus"
const POINT_TOL := 1e-9      # hand scenes, exact inputs: float64 vs oracle
const CORPUS_TOL := 0.01     # px (PHYSICAL-PREVIEW-MATCH)

var _fx: Dictionary = {}

func before_all():
	var f := FileAccess.open(FIXTURE, FileAccess.READ)
	for x in JSON.parse_string(f.get_as_text())["fixtures"]:
		_fx[x["name"]] = x

static func plan_of(j: Array) -> Array:
	var out: Array = []
	for e in j:
		out.append(BeamSolver.PlanItem.make(int(e["surface_id"]), e["side"] == "LEFT"))
	return out

# Walk a fixture with the generic solver at `ctx`.
func _walk(name: String, ctx: NumCtx) -> Array:
	var fx: Dictionary = _fx[name]
	var scene := BeamScene.from_defs(ctx, fx["surfaces"])
	var player := ctx.cn(fx["player"][0], fx["player"][1])
	var cursor = null if fx["cursor"] == null else ctx.cn(fx["cursor"][0], fx["cursor"][1])
	var mode: int = BeamSolver.Mode.PHYSICAL if fx["mode"] == "physical" else BeamSolver.Mode.PLANNED
	if fx["direction"] != null:
		var d := ctx.cn(fx["direction"][0], fx["direction"][1])
		return BeamSolver.trace_direction(ctx, scene, player, d, plan_of(fx["plan"]), cursor, mode)
	if mode == BeamSolver.Mode.PLANNED:
		return BeamSolver.planned(ctx, scene, player, cursor, plan_of(fx["plan"]))
	return BeamSolver.physical(ctx, scene, player, cursor, plan_of(fx["plan"]))

# Compare solver events with oracle events: kind, surface, side, applied,
# terminal, on-segment, re-hit flag and point.
func _check(events: Array, expected: Array, label: String, tol: float) -> bool:
	if events.size() != expected.size():
		fail_test("%s: %d events, oracle has %d (%s)" % [label, events.size(), expected.size(), _describe(events)])
		return false
	for i in expected.size():
		var e: BeamSolver.Event = events[i]
		var x: Dictionary = expected[i]
		var got := [e.kind, e.surface_id, ("LEFT" if e.left else "RIGHT") if e.kind == "hit" else "",
			e.applied, e.terminal, e.on_segment, e.rehit]
		var want := [x["kind"], int(x["surface_id"]), x["side"], x["applied"], x["terminal"], x["on_segment"], x["rehit"]]
		if got != want:
			fail_test("%s event %d: got %s, oracle %s" % [label, i, str(got), str(want)])
			return false
		if x.has("x") and e.point != null:
			var d := Vector2(e.point.fx(), e.point.fy()).distance_to(Vector2(String(x["x"]).to_float(), String(x["y"]).to_float()))
			if d > tol:
				fail_test("%s event %d: point off by %g" % [label, i, d])
				return false
	return true

static func _describe(events: Array) -> String:
	return ", ".join(events.map(func(e): return e.describe()))

func _check_fixture(name: String) -> void:
	assert_true(_fx.has(name), "fixture %s" % name)
	for ctx: NumCtx in [NumCtx.f64(), NumCtx.with_limbs(4)]:
		var ok := _check(_walk(name, ctx), _fx[name]["expected"], "%s [%s]" % [name, ctx.name()], POINT_TOL)
		if ok:
			pass_test("%s [%s] matches the oracle" % [name, ctx.name()])

# ---------------------------------------------------------------- rules

func test_terminal_ends_trace_at_hit():
	_check_fixture("terminal_ends_trace")
	var last: Dictionary = _fx["terminal_ends_trace"]["expected"].back()
	assert_true(last["terminal"], "oracle: the block hit is the last event")

func test_pass_surfaces_emit_events():
	_check_fixture("pass_surfaces_emit_events")

func test_max_hits_32_counts_all_events():
	_check_fixture("max_hits_parallel_mirrors")
	var ev := _walk("max_hits_parallel_mirrors", NumCtx.f64())
	assert_eq(ev.filter(func(e): return e.kind == "hit").size(), 32, "32 hits")
	assert_eq("" if ev.is_empty() else ev.back().kind, "max", "then the MAX_HITS stop")

func test_one_sided_mirror_inert_side_passes():
	_check_fixture("one_sided_mirror_inert_side")
	_check_fixture("one_sided_mirror_active_side")

func test_partial_inversion_arc_off_segment_passes():
	_check_fixture("partial_inversion_off_segment_physical")
	_check_fixture("partial_inversion_off_segment_planned")
	_check_fixture("partial_inversion_on_segment_physical")

func test_y_junction_blocks_both_sides():
	_check_fixture("y_junction_fires_lower_id")
	_check_fixture("endpoint_graze_passes")

func test_partial_blockage_does_not_leak():
	_check_fixture("partial_blockage_does_not_leak")

func test_zero_length_rule_same_in_planned():
	_check_fixture("zero_length_planned_entry")
	_check_fixture("zero_length_planned_entry_right")

func test_departure_rehit_is_ordinary_hit():
	_check_fixture("departure_rehit_straight")
	_check_fixture("departure_rehit_off_segment_planned")
	var ev := _walk("departure_rehit_straight", NumCtx.f64())
	if ev.size() > 1:
		assert_true(ev[1].rehit, "second event is the departure re-hit")

func test_provenance_points_snap_to_endpoints():
	# the beam is constructed through the cursor / player: a segment endpoint
	# exactly there is hit exactly there (junction full block -> reflection),
	# whatever the computed root's rounding
	_check_fixture("cursor_at_junction_physical")
	_check_fixture("cursor_at_junction_planned")
	_check_fixture("player_at_arc_endpoint")
	_check_fixture("player_at_arc_endpoint_planned")
	# known points propagate through effects (an inversion fixes its circle)
	_check_fixture("propagated_endpoint_physical")
	# exact incidence on an exact initial line (collinear data)
	_check_fixture("collinear_endpoint_physical")
	_check_fixture("collinear_endpoint_direction")
	# incidence of a known point inside a mirror, then the exact reflected line
	_check_fixture("reflected_exact_line_endpoint")
	# mirror-only plan: exact aim image -> exact initial line
	_check_fixture("planned_mirror_exact_image")
	_check_fixture("planned_mirror_exact_image_physical")
	# an exact line through an inversion center stays exactly straight
	_check_fixture("line_through_inversion_center")
	# an exact line tangent to a circle touches it without crossing
	_check_fixture("tangent_line_no_crossing")
	_check_fixture("tangent_line_no_crossing_planned")

func test_targets_recorded_on_segment():
	_check_fixture("target_recorded_on_segment")
	var fx: Dictionary = _fx["target_recorded_on_segment"]
	var ctx := NumCtx.f64()
	var scene := BeamScene.from_defs(ctx, fx["surfaces"])
	var ev := BeamSolver.trace_direction(ctx, scene, ctx.cn(fx["player"][0], fx["player"][1]),
		ctx.cn(fx["direction"][0], fx["direction"][1]), [], null, BeamSolver.Mode.PHYSICAL)
	assert_eq(BeamSolver.targets_of(scene, ev), fx["targets"].map(func(x): return int(x)), "on-segment target hits")

# ---------------------------------------------------------------- FastSolver twin

func test_fast_twin_trace_rules():
	# every fixture: the raw-float walk is bit-identical to the generic float64 walk
	var ctx := NumCtx.f64()
	for name in _fx:
		var fx: Dictionary = _fx[name]
		if fx["direction"] == null:
			continue
		var gen := _walk(name, ctx)
		assert_eq(gen.size(), fx["expected"].size(), "%s: generic walk matches the oracle length" % name)
		var fsc := FastSolver.Scene.from_defs(fx["surfaces"])
		var mode: int = BeamSolver.Mode.PHYSICAL if fx["mode"] == "physical" else BeamSolver.Mode.PLANNED
		var cur = null if fx["cursor"] == null else PackedFloat64Array(fx["cursor"])
		var out := FastSolver.trace_direction(fsc, PackedFloat64Array(fx["player"]), PackedFloat64Array(fx["direction"]),
			plan_of(fx["plan"]), cur, mode)
		assert_eq(out.events.size(), gen.size(), "%s: event count" % name)
		for i in mini(out.events.size(), gen.size()):
			var a: BeamSolver.Event = out.events[i]
			var b: BeamSolver.Event = gen[i]
			assert_eq([a.kind, a.surface_id, a.left, a.applied, a.on_segment, a.rehit],
				[b.kind, b.surface_id, b.left, b.applied, b.on_segment, b.rehit], "%s event %d" % [name, i])
			if a.point != null and b.point != null:
				assert_eq([a.point.fx(), a.point.fy()], [b.point.fx(), b.point.fy()], "%s event %d: bit-identical point" % [name, i])

# ---------------------------------------------------------------- scene corpus

static func corpus_names(prefix: String = "") -> Array:
	var out: Array = []
	var d := DirAccess.open(CORPUS + "/scenarios")
	for fn in d.get_files():
		if fn.ends_with(".json") and fn.begins_with(prefix):
			out.append(fn.get_basename())
	out.sort()
	return out

static func load_corpus(name: String) -> Array:
	var scn: Dictionary = JSON.parse_string(FileAccess.open("%s/scenarios/%s.json" % [CORPUS, name], FileAccess.READ).get_as_text())
	var ex: Dictionary = JSON.parse_string(FileAccess.open("%s/expected/%s.json" % [CORPUS, name], FileAccess.READ).get_as_text())
	return [scn, ex]

func test_planned_reflection_chain_reaches_cursor():
	var n := 0
	for prefix in ["test_levels__two_mirrors", "test_levels__three_mirrors", "test_levels__parallel_mirrors"]:
		for name in corpus_names(prefix):
			var se := load_corpus(name)
			var scn: Dictionary = se[0]
			var res := PrecisionLadder.solve(scn["surfaces"], scn["player"], scn["cursor"], plan_of(scn["plan"]),
				32, 32, CORPUS_TOL)
			_check(res.planned, se[1]["planned"], name + " planned", CORPUS_TOL)
			for e in res.planned:
				if e.kind == "cursor":
					assert_lt(Vector2(e.point.fx(), e.point.fy()).distance_to(Vector2(scn["cursor"][0], scn["cursor"][1])), 1e-6,
						"%s: the planned path passes through the cursor" % name)
			n += 1
	assert_eq(n, 36)

func test_scene_corpus_matches_oracle():
	# every supported real level: planned and physical event structure and
	# points (≤ 0.01 px) for the whole trace
	var bad := 0
	var names := corpus_names()
	for name in names:
		var se := load_corpus(name)
		var scn: Dictionary = se[0]
		var res := PrecisionLadder.solve(scn["surfaces"], scn["player"], scn["cursor"], plan_of(scn["plan"]),
			32, 32, CORPUS_TOL)
		if not _check(res.planned, se[1]["planned"], name + " planned", CORPUS_TOL):
			bad += 1
		if not _check(res.physical, se[1]["physical"], name + " physical", CORPUS_TOL):
			bad += 1
	assert_eq(bad, 0, "%d scenarios: planned and physical match the oracle" % names.size())
	assert_eq(names.size(), 264)
