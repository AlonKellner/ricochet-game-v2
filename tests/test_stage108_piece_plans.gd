extends GutTest

# Stage 108: plan entries naming a piece (GAME_SPEC 9.7) through the
# bindings. Expected values: the mpmath oracle (trace2) --
# bench/fixtures/stage108_piece_plans.json (gen_fixtures_stage108.py),
# compared at f64 and 4 limbs as Stage 85 does.

const FIXTURE := "res://bench/fixtures/stage108_piece_plans.json"
const Rules := preload("res://tests/test_stage81_trace_rules.gd")
const POINT_TOL := 1e-9

var _fx: Dictionary = {}

func before_all():
	var f := FileAccess.open(FIXTURE, FileAccess.READ)
	for x in JSON.parse_string(f.get_as_text())["fixtures"]:
		_fx[x["name"]] = x

func _walk(name: String, ctx: NumCtx) -> Array:
	var fx: Dictionary = _fx[name]
	var scene := Rules.scene_of(ctx, fx["surfaces"])
	var player := ctx.cn(fx["player"][0], fx["player"][1])
	var cursor = null if fx["cursor"] == null else ctx.cn(fx["cursor"][0], fx["cursor"][1])
	var mode: int = BeamSolver.PHYSICAL if fx["mode"] == "physical" else BeamSolver.PLANNED
	var plan := Rules.plan_of(fx["plan"])
	var flags: Dictionary = fx["flags"]
	if fx["direction"] != null:
		return BeamSolver.trace_direction(ctx, scene, player, ctx.cn(fx["direction"][0], fx["direction"][1]), plan, cursor, mode,
			BeamSolver.MAX_HITS, flags)
	if mode == BeamSolver.PLANNED:
		return BeamSolver.planned(ctx, scene, player, cursor, plan, BeamSolver.MAX_HITS, flags)
	return BeamSolver.physical(ctx, scene, player, cursor, plan, BeamSolver.MAX_HITS, flags)

func _check(events: Array, expected: Array, label: String) -> bool:
	if events.size() != expected.size():
		fail_test("%s: %d events, oracle has %d (%s)" % [label, events.size(), expected.size(),
			", ".join(events.map(func(e): return e.describe()))])
		return false
	for i in expected.size():
		var e: BeamSolverEvent = events[i]
		var x: Dictionary = expected[i]
		var got := [e.kind, e.surface_id, ("LEFT" if e.left else "RIGHT") if e.kind == "hit" else "",
			e.applied, e.terminal, e.on_segment, e.rehit, e.state]
		var want := [x["kind"], int(x["surface_id"]), x["side"], x["applied"], x["terminal"], x["on_segment"], x["rehit"],
			x["state"]]
		if got != want:
			fail_test("%s event %d: got %s, oracle %s" % [label, i, str(got), str(want)])
			return false
		if x["flags"] != null and e.flags != x["flags"]:
			fail_test("%s event %d: flags %s, oracle %s" % [label, i, str(e.flags), str(x["flags"])])
			return false
		if x.has("x") and e.point != null:
			var d := Vector2(e.point.fx(), e.point.fy()).distance_to(Vector2(String(x["x"]).to_float(), String(x["y"]).to_float()))
			if d > POINT_TOL:
				fail_test("%s event %d: point off by %s" % [label, i, d])
				return false
	return true

func _check_fixture(name: String) -> void:
	assert_true(_fx.has(name), "fixture %s" % name)
	if not _fx.has(name):
		return
	for ctx: NumCtx in [NumCtx.f64(), NumCtx.with_limbs(4)]:
		if _check(_walk(name, ctx), _fx[name]["expected"], "%s [%s]" % [name, ctx.name()]):
			pass_test("%s [%s]" % [name, ctx.name()])

func test_stage108_fixture_count():
	assert_eq(_fx.size(), 9, "every Stage 108 piece-plan fixture is checked below")

func test_stage108_piece_reached():
	_check_fixture("piece_planned_reached_planned")
	_check_fixture("piece_planned_reached_physical")

func test_stage108_other_piece_diverges():
	_check_fixture("piece_planned_other_piece_planned")
	_check_fixture("piece_planned_other_piece_physical")

func test_stage108_no_piece_matches_the_whole_surface():
	_check_fixture("piece_unspecified_matches_whole")

func test_stage108_circle_pieces():
	for name in ["circle_piece_reached_planned", "circle_piece_reached_physical", "circle_piece_other_planned", "circle_piece_other_physical"]:
		_check_fixture(name)
