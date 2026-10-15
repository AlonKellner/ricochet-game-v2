extends GutTest

# Stage 88: plannable infinity. The oracle (docs/research/oracle/trace2.py,
# EMIT_INFINITY) specifies the "infinity" event and the INFINITY_ID plan entry;
# bench/fixtures/stage88_infinity.json holds its traces. The solver does not
# emit the event before Stage 88c: the trace comparison is pending until then.
# This file already pins the fixture schema so the spec cannot drift unnoticed.

const FIXTURES := "res://bench/fixtures/stage88_infinity.json"
const INFINITY_ID := -3

var _fx: Dictionary = {}

func before_all():
	var f := FileAccess.open(FIXTURES, FileAccess.READ)
	assert_not_null(f, "fixture file exists")
	var d: Dictionary = JSON.parse_string(f.get_as_text())
	for x in d["fixtures"]:
		_fx[x["name"]] = x

func test_fixture_schema():
	assert_eq(_fx.size(), 26, "26 Stage 88 fixtures")
	var infinity_events := 0
	var consumed := 0
	for name in _fx:
		var x: Dictionary = _fx[name]
		for e in x["expected"]:
			assert_true(e["kind"] in ["hit", "cursor", "origin", "escape", "max", "infinity"], "%s: kind %s" % [name, e["kind"]])
			if e["kind"] == "infinity":
				infinity_events += 1
				assert_eq(int(e["surface_id"]), INFINITY_ID, "%s: infinity carries INFINITY_ID" % name)
				assert_false(e.has("x"), "%s: infinity has no point" % name)
				if e["applied"]:
					consumed += 1
		for p in x["plan"]:
			if int(p["surface_id"]) == INFINITY_ID:
				assert_eq(p["side"], "", "%s: the infinity entry has no side" % name)
	assert_gt(infinity_events, 0)
	assert_gt(consumed, 0, "some fixtures consume the infinity entry")

func test_both_realizations_of_one_mirror_plan_are_recorded():
	var direct: Array = _fx["mirror_direct_reaches_through_unapplied_wrap_planned"]["expected"]
	var other: Array = _fx["infinity_then_mirror_planned"]["expected"]
	assert_eq([direct[0]["kind"], direct[1]["kind"], direct[2]["kind"]], ["hit", "infinity", "cursor"])
	assert_false(direct[1]["applied"])
	assert_eq([other[0]["kind"], other[1]["kind"], other[2]["kind"]], ["infinity", "hit", "cursor"])
	assert_true(other[0]["applied"])

const Rules := preload("res://tests/test_stage81_trace_rules.gd")
const POINT_TOL := 1e-9

# Walk a fixture with the generic solver at `ctx`, the infinity event on.
func _walk(name: String, ctx: NumCtx) -> Array:
	var fx: Dictionary = _fx[name]
	var scene := BeamScene.from_defs(ctx, fx["surfaces"])
	scene.emit_infinity = true
	var player := ctx.cn(fx["player"][0], fx["player"][1])
	var cursor = null if fx["cursor"] == null else ctx.cn(fx["cursor"][0], fx["cursor"][1])
	var mode: int = BeamSolver.PHYSICAL if fx["mode"] == "physical" else BeamSolver.PLANNED
	var plan := Rules.plan_of(fx["plan"])
	var flags: Dictionary = fx["flags"] if fx.get("flags") != null else {}
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
			e.applied, e.terminal, e.on_segment, e.rehit]
		var want := [x["kind"], int(x["surface_id"]), x["side"], x["applied"], x["terminal"], x["on_segment"], x["rehit"]]
		if got != want:
			fail_test("%s event %d: got %s, oracle %s" % [label, i, str(got), str(want)])
			return false
		if x["kind"] == "infinity" and e.point != null:
			fail_test("%s event %d: the infinity event has a point" % [label, i])
			return false
		if x.has("x") and e.point != null:
			var d := Vector2(e.point.fx(), e.point.fy()).distance_to(Vector2(String(x["x"]).to_float(), String(x["y"]).to_float()))
			if d > POINT_TOL:
				fail_test("%s event %d: point off by %s" % [label, i, d])
				return false
	return true

func test_solver_matches_stage88_fixtures():
	for name in _fx:
		for ctx: NumCtx in [NumCtx.f64(), NumCtx.with_limbs(4)]:
			if _check(_walk(name, ctx), _fx[name]["expected"], "%s [%s]" % [name, ctx.name()]):
				pass_test("%s [%s]" % [name, ctx.name()])

func test_switch_off_never_emits_the_event():
	var fx: Dictionary = _fx["wrap_between_ahead_and_wrapped_crossings"]
	var ctx := NumCtx.f64()
	var scene := BeamScene.from_defs(ctx, fx["surfaces"])
	assert_false(scene.emit_infinity, "off by default")
	var events: Array = BeamSolver.trace_direction(ctx, scene, ctx.cn(fx["player"][0], fx["player"][1]),
		ctx.cn(fx["direction"][0], fx["direction"][1]), [], null, BeamSolver.PHYSICAL)
	assert_eq(events.map(func(e): return e.kind), ["hit", "hit", "escape"], "the pre-Stage-88 trace")
	assert_eq(BeamSolver.INFINITY_ID, INFINITY_ID)

# The tracer: the infinity event's outgoing half carries INFINITY_ID, and the
# infinity plan entry makes the planned path reach the cursor the other way.
func test_tracer_infinity_entry_reaches_the_cursor_through_the_wrap():
	var fx: Dictionary = _fx["infinity_only_planned"]
	var tracer := BeamTracer.new()
	var player := Vector2(fx["player"][0], fx["player"][1])
	var cursor := Vector2(fx["cursor"][0], fx["cursor"][1])
	var plan := Rules.plan_of(fx["plan"])
	var off: BeamTracerPair = tracer.trace_pair(fx["surfaces"], player, cursor, plan)
	assert_not_null(off)
	assert_eq(off.planned.cursor_index, -1, "switch off: the infinity entry is never consumed, the cursor is not reached")
	tracer.emit_infinity = true
	var on: BeamTracerPair = tracer.trace_pair(fx["surfaces"], player, cursor, plan)
	assert_not_null(on)
	assert_gt(on.planned.cursor_index, 0, "switch on: the planned path reaches the cursor")
	var steps: Array = on.planned.steps
	assert_eq(steps[0].surface_id, INFINITY_ID, "the outgoing half of the wrap carries INFINITY_ID")
	assert_true(steps[0].end.x == INF or steps[0].end.x == -INF, "it leaves to infinity")
	assert_true(steps[1].start.x == INF or steps[1].start.x == -INF, "the incoming half returns from infinity")
	assert_eq(steps[1].surface_id, -1)
	var dir0: Vector2 = (steps[0].via).normalized()
	var aim: Vector2 = (cursor - player).normalized()
	assert_lt(dir0.dot(aim), 0.0, "the aim is reversed: away from the cursor")
	assert_true(on.physical.physical_hits().has(INFINITY_ID), "physical_hits lists the wrap under INFINITY_ID")
