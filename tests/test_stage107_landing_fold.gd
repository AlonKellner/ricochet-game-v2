extends GutTest

# Stage 107: the landing fold (GAME_SPEC 10.6) on synthetic hit records --
# what the solver decided (fired, flags before, point, tangent) is given; the
# fold only replays the landing parts.

const M := SpawnFrame.Mode

func _side(type := "pass", interactive := false, hits: Array = []) -> SideSpec:
	return SideSpec.make({"type": type}, interactive, hits)

func _template() -> SpawnTemplate:
	return SpawnTemplate.make(SurfaceSpec.line(Vector2(0, -20), Vector2(0, 20), _side("terminal"), _side("terminal")), M.CARRIER_TANGENT)

func _spawn(channel: String, deferred := false) -> Dictionary:
	return {"kind": "spawn", "channel": channel, "template": _template(), "deferred": deferred}

func _reset(channels: Array, deferred := false) -> Dictionary:
	return {"kind": "reset", "channels": channels, "deferred": deferred}

func _surface(x: float, hits: Array, type := "pass") -> Surface:
	var s := _side(type, true, hits)
	return SurfaceSpec.line(Vector2(x, -100), Vector2(x, 100), s, s).build()[0]

func _hit(s: Surface, y: float, flags_before := {}, fired := true) -> Dictionary:
	return {"surface_id": s.id, "side": Side.Value.LEFT, "on_segment": true, "fired": fired, "state": fired,
		"point": Vector2(s.segment.start.coords.x, y), "tangent": Vector2(1, 0), "flags_before": flags_before, "step_index": 0}

func _by_id(list: Array) -> Dictionary:
	var out := {}
	for s in list:
		out[s.id] = s
	return out

func before_each():
	Surface.reset_id_counter()

func test_stage107_a_fired_spawn_lands_as_the_next_generation():
	var host := _surface(100, [_spawn("c")])
	var r := LandingFold.fold([_hit(host, 10)], _by_id([host]), {"chan:c": "pending"}, {"c": 3})
	assert_eq(r["flags"], {"chan:c": "g4"})
	assert_eq(r["generations"], {"c": 4})
	assert_eq(r["placed"].size(), 1)
	var spec: SurfaceSpec = r["placed"][0]["spec"]
	assert_eq([spec.start, spec.end], [Vector2(120, 10), Vector2(80, 10)], "placed at the hit, across the carrier (local y is v)")
	assert_eq(spec.presence, [{"key": "chan:c", "values": ["g4"]}], "the group exists at its generation")

func test_stage107_a_hit_that_did_not_fire_does_nothing():
	var host := _surface(100, [_spawn("c")])
	var r := LandingFold.fold([_hit(host, 10, {}, false)], _by_id([host]), {}, {})
	assert_eq(r["placed"], [])
	assert_eq(r["flags"], {})

func test_stage107_a_reset_in_the_shot_cancels_the_spawn():
	var host := _surface(100, [_spawn("c")])
	var reset := _surface(200, [_reset(["c"])])
	var hits := [_hit(host, 10), _hit(reset, 10, {"chan:c": "pending"})]
	var r := LandingFold.fold(hits, _by_id([host, reset]), {"chan:c": "armed"}, {})
	assert_eq(r["placed"], [], "the solver's final flags say armed: nothing lands")
	assert_eq(r["flags"], {"chan:c": "armed"})

func test_stage107_spawn_reset_spawn_lands_the_last_firing():
	var host := _surface(100, [_spawn("c")])
	var reset := _surface(200, [_reset(["c"])])
	var hits := [_hit(host, 10), _hit(reset, 10, {"chan:c": "pending"}), _hit(host, 60, {"chan:c": "armed"})]
	var r := LandingFold.fold(hits, _by_id([host, reset]), {"chan:c": "pending"}, {})
	assert_eq(r["placed"].size(), 1)
	assert_eq(r["placed"][0]["spec"].start, Vector2(120, 60), "at the second firing (y = 60)")

func test_stage107_a_deferred_reset_applies_at_landing():
	var host := _surface(100, [_spawn("c")])
	var reset := _surface(200, [_reset(["c"], true)])
	var hits := [_hit(host, 10), _hit(reset, 10, {"chan:c": "pending"})]
	# deferred: the solver did not apply it -- the shot ended pending
	var r := LandingFold.fold(hits, _by_id([host, reset]), {"chan:c": "pending"}, {})
	assert_eq(r["placed"], [], "applied at landing, it cancels the pending spawn")
	assert_eq(r["flags"], {"chan:c": "armed"})

func test_stage107_a_deferred_spawn_lands_from_its_landing_write():
	var host := _surface(100, [_spawn("c", true)])
	var r := LandingFold.fold([_hit(host, 10)], _by_id([host]), {}, {})
	assert_eq(r["placed"].size(), 1, "pending is written at landing, then the spawn lands")
	assert_eq(r["flags"], {"chan:c": "g1"})

func test_stage107_a_shared_channel_lands_the_host_that_fired():
	var a := _surface(100, [_spawn("c")])
	var b := _surface(300, [_spawn("c")])
	var r := LandingFold.fold([_hit(b, 10)], _by_id([a, b]), {"chan:c": "pending"}, {})
	assert_eq(r["placed"][0]["spec"].start, Vector2(320, 10), "at the host that fired")

func test_stage107_list_order_on_one_side():
	# target, spawn, reset of the same channel: in the shot the reset is the
	# last writer (armed), so the spawn does not land; the target registered
	var host := _surface(100, [{"kind": "target"}, _spawn("c"), _reset(["c"])])
	var final := {"target:%d" % host.id: true, "chan:c": "armed"}
	var r := LandingFold.fold([_hit(host, 10)], _by_id([host]), final, {})
	assert_eq(r["placed"], [])
	assert_eq(r["flags"], final)

func test_stage107_categorical_side_is_resolved_under_the_flags_before():
	var spec := SurfaceSpec.line(Vector2(100, -100), Vector2(100, 100), null, null)
	spec.resolver_key = "mode"
	spec.resolver_table = {"x": [_side("pass", true, [_spawn("c1")]), _side("pass", true, [_spawn("c1")])],
		"DEFAULT": [_side("pass", true, [_spawn("c2")]), _side("pass", true, [_spawn("c2")])]}
	var host: Surface = spec.build()[0]
	var r := LandingFold.fold([_hit(host, 10, {"mode": "x"})], _by_id([host]), {"mode": "x", "chan:c1": "pending"}, {})
	assert_eq(r["placed"].map(func(p): return p["channel"]), ["c1"])
	var r2 := LandingFold.fold([_hit(host, 10, {})], _by_id([host]), {"chan:c2": "pending"}, {})
	assert_eq(r2["placed"].map(func(p): return p["channel"]), ["c2"])

func test_stage107_the_fold_is_pure():
	var host := _surface(100, [_spawn("c")])
	var hits := [_hit(host, 10)]
	var final := {"chan:c": "pending"}
	var gens := {"c": 1}
	var hits_before := hits.duplicate(true)
	LandingFold.fold(hits, _by_id([host]), final, gens)
	assert_eq(final, {"chan:c": "pending"})
	assert_eq(gens, {"c": 1})
	assert_eq(hits, hits_before)

func test_stage107_a_host_gets_its_channel_presence():
	var host := _surface(100, [_spawn("c"), _spawn("d")])
	assert_true(host.present({}))
	assert_false(host.present({"chan:c": "pending"}))
	assert_false(host.present({"chan:d": "g2"}), "a host needs every channel it spawns into armed")
	assert_true(host.present({"chan:c": "armed", "chan:d": "armed"}))
