extends GutTest

# Stage 107: the trace's hit provenance (TracePath.hits, GAME_SPEC 10.6): one
# record per hit event -- zero-length steps are not emitted, so per step is
# not enough -- with what the solver decided there. The landing fold reads
# these and never re-derives a firing, a resolved config or a direction.

func _vline(id: int, x: float, left, right, presence = null) -> Dictionary:
	var d := {"id": id, "line": true, "start": [x, -400.0], "end": [x, 400.0], "via": [x, 0.0], "full": false,
		"circle": null, "left": left, "right": right, "is_target": false}
	if presence != null:
		d["presence"] = presence
	return d

func _side(type: String, states: Array = []) -> Dictionary:
	return {"type": type, "interactive": true, "states": states}

func _write(key: String, value) -> Dictionary:
	return {"key": key, "op": "set", "value": value}

func _trace(defs: Array, player: Vector2, cursor: Vector2, flags := {}) -> BeamTracerPair:
	var tr := BeamTracer.new()
	tr.set_emit_infinity(FeatureFlags.infinity_event())
	return tr.trace_pair(defs, player, cursor, [], flags, {})

func test_stage107_hit_records_carry_the_solver_decisions():
	var pend := _side("pass", [_write("chan:c", "pending")])
	var defs := [
		_vline(1, 300.0, pend, pend, [{"key": "chan:c", "values": ["DEFAULT", "armed"]}]),
		_vline(2, 500.0, _side("reflection"), _side("reflection"), [{"key": "chan:c", "values": ["DEFAULT", "armed"]}]),
		_vline(3, 900.0, _side("terminal"), _side("terminal")),
	]
	var pair := _trace(defs, Vector2(100, 0), Vector2(200, 0))
	assert_not_null(pair)
	var hits: Array = pair.physical.hits
	assert_eq(hits.size(), 2, "the host, then the wall: the mirror vanished with the write")
	if hits.size() != 2:
		return
	assert_eq(hits[0]["surface_id"], 1)
	assert_true(hits[0]["fired"] and hits[0]["state"] and hits[0]["on_segment"])
	assert_eq(hits[0]["point"], Vector2(300, 0))
	assert_eq(hits[0]["tangent"], Vector2(1, 0), "the beam's travel direction at the hit")
	assert_eq(hits[0]["flags_before"], {}, "resolved under the flags before its own write")
	assert_eq(hits[1]["surface_id"], 3)
	assert_eq(hits[1]["flags_before"], {"chan:c": "pending"})
	assert_eq(pair.final_flags, {"chan:c": "pending"})
	assert_gt(hits[1]["step_index"], hits[0]["step_index"])

func test_stage107_a_ghosted_side_does_not_fire():
	var ghost := _side("pass", [{"key": "ghost", "op": "toggle", "value": true}])
	var defs := [
		_vline(1, 300.0, ghost, ghost),
		_vline(2, 500.0, _side("pass", [_write("seen", true)]), _side("pass", [_write("seen", true)])),
		_vline(3, 700.0, ghost, ghost),
		_vline(4, 900.0, _side("terminal"), _side("terminal")),
	]
	var pair := _trace(defs, Vector2(100, 0), Vector2(200, 0))
	var by_id := {}
	for h in pair.physical.hits:
		if not by_id.has(h["surface_id"]):
			by_id[h["surface_id"]] = h
	assert_true(by_id[1]["fired"], "the toggler fires")
	assert_false(by_id[2]["fired"], "a ghosted side does not fire")
	assert_false(pair.final_flags.has("seen"), "nor write")
	assert_true(by_id[3]["fired"], "the second toggler materializes")

func test_stage107_categorical_side_resolved_under_flags_before():
	# a gate keyed on "open": the toggler before it opens it in the same shot;
	# the gate's record carries the flags it was resolved under
	var tog := _side("pass", [{"key": "open", "op": "toggle", "value": true}])
	var gate := _vline(2, 500.0, null, null)
	gate["resolver"] = {"key": "open", "table": {"true": {"left": _side("pass"), "right": _side("pass")},
		"DEFAULT": {"left": _side("terminal"), "right": _side("terminal")}}}
	var defs := [_vline(1, 300.0, tog, tog), gate, _vline(3, 900.0, _side("terminal"), _side("terminal"))]
	var pair := _trace(defs, Vector2(100, 0), Vector2(200, 0))
	var hits: Array = pair.physical.hits
	assert_eq(hits.map(func(h): return h["surface_id"]), [1, 2, 3], "the open gate passes")
	assert_eq(hits[1]["flags_before"], {"open": true})

func test_stage107_absent_surface_cannot_be_planned():
	var defs := [
		_vline(1, 300.0, _side("pass", [_write("chan:c", "pending")]), _side("pass", [_write("chan:c", "pending")]),
			[{"key": "chan:c", "values": ["DEFAULT", "armed"]}]),
		_vline(2, 500.0, _side("reflection"), _side("reflection"), [{"key": "chan:c", "values": ["g1"]}]),
	]
	var tr := BeamTracer.new()
	assert_true(tr.plan_refuses(defs, [], 2, true, {}), "the group is absent")
	assert_false(tr.plan_refuses(defs, [], 2, true, {"chan:c": "g1"}), "present at g1")
	assert_false(tr.plan_refuses(defs, [], 1, true, {}), "the armed host")
	var after_host := [BeamSolverPlanItem.make(1, true, null)]
	assert_true(tr.plan_refuses(defs, after_host, 1, true, {}), "the host consumed itself in the plan")
