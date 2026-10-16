extends GutTest

# Stage 107: a side's list of state changes (GAME_SPEC 10.6) and surface
# presence (9.6) through the bindings. Expected values: the mpmath oracle
# (trace2) -- bench/fixtures/stage107_hit_effects.json
# (gen_fixtures_stage107.py), compared at f64 and 4 limbs as Stage 85 does.

const FIXTURE := "res://bench/fixtures/stage107_hit_effects.json"
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
			e.applied, e.terminal, e.on_segment, e.rehit, e.state, e.fired]
		var want := [x["kind"], int(x["surface_id"]), x["side"], x["applied"], x["terminal"], x["on_segment"], x["rehit"],
			x["state"], x["fired"]]
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

func test_stage107_fixture_count():
	assert_eq(_fx.size(), 11, "every Stage 107 fixture is checked below")

func test_stage107_state_lists():
	_check_fixture("multi_state_two_keys")
	_check_fixture("multi_state_order")
	_check_fixture("multi_state_ghost_in_list")
	_check_fixture("multi_state_planned_planned")
	_check_fixture("multi_state_planned_physical")

func test_stage107_presence_mid_shot():
	_check_fixture("presence_vanish_and_appear")
	_check_fixture("presence_absent_from_start")
	_check_fixture("presence_reset_restores")

func test_stage107_absent_surface_blocks_no_junction():
	_check_fixture("presence_junction_both")
	_check_fixture("presence_junction_absent")

func test_stage107_plan_entry_on_absent_surface():
	_check_fixture("presence_plan_entry_absent")

func test_stage107_scene_surface_bindings():
	var fx: Dictionary = _fx["presence_vanish_and_appear"]
	var scene := Rules.scene_of(NumCtx.f64(), fx["surfaces"])
	var host: BeamSceneSurf = scene.by_id[1]
	assert_false(host.present({"chan:c": "pending"}), "the host is absent while pending")
	assert_true(host.present({}), "present while the channel is missing (DEFAULT)")
	assert_eq(host.state_changes(true, {}), [{"key": "chan:c", "op": "set", "value": "pending"}])
	var two: Dictionary = _fx["multi_state_two_keys"]
	var s2: BeamSceneSurf = Rules.scene_of(NumCtx.f64(), two["surfaces"]).by_id[1]
	assert_eq(s2.state_changes(false, {}).size(), 2, "both writes, in order")
