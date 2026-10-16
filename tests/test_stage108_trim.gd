extends GutTest

# Stage 108: spawn trimming (GAME_SPEC 10.6.3) -- a spawned surface is cut by
# what is present when it lands and keeps the piece containing its midpoint
# (a full circle: the arc nearest the hit point).

const M := SpawnFrame.Mode

func _side(type := "pass", interactive := false, hits: Array = []) -> SideSpec:
	return SideSpec.make({"type": type}, interactive, hits)

func _host(x: float, channel: String, template: SpawnTemplate) -> Surface:
	var s := _side("pass", true, [{"kind": "spawn", "channel": channel, "template": template}])
	return SurfaceSpec.line(Vector2(x, -200), Vector2(x, 200), s, s).build()[0]

func _wall(a: Vector2, b: Vector2, presence := []) -> Surface:
	var spec := SurfaceSpec.line(a, b, _side("terminal"), _side("terminal"))
	spec.presence.assign(presence)
	return spec.build()[0]

func _hit(s: Surface, y: float) -> Dictionary:
	return {"surface_id": s.id, "side": Side.Value.LEFT, "on_segment": true, "fired": true, "state": true,
		"point": Vector2(s.segment.start.coords.x, y), "tangent": Vector2(1, 0), "flags_before": {}, "step_index": 0}

func _by_id(list: Array) -> Dictionary:
	var out := {}
	for s in list:
		out[s.id] = s
	return out

# a mirror across the beam, centred on the hit: from (x, y - 60) to (x, y + 60)
func _across(trim := true) -> SpawnTemplate:
	var t := SpawnTemplate.make(SurfaceSpec.line(Vector2(-60, 0), Vector2(60, 0), _side("reflection", true), _side("reflection", true)),
		M.BEAM_NORMAL)
	t.trim = trim
	return t

func before_each():
	Surface.reset_id_counter()

func test_stage108_a_spawn_is_trimmed_by_a_present_wall():
	var host := _host(100, "c", _across())
	var wall := _wall(Vector2(0, 40), Vector2(200, 40))      # crosses the mirror at (100, 40)
	var r := LandingFold.fold([_hit(host, 10)], _by_id([host, wall]), {"chan:c": "pending"}, {})
	var spec: SurfaceSpec = r["placed"][0]["spec"]
	assert_eq([spec.start, spec.end], [Vector2(100, -50), Vector2(100, 40)], "the midpoint's piece (BEAM_NORMAL: u = +y)")
	assert_true(spec.straight)
	assert_eq(Crossings.of(spec.segment(), wall.segment), [], "it no longer crosses the wall")

func test_stage108_absent_surfaces_do_not_cut_and_trim_can_be_off():
	var host := _host(100, "c", _across())
	var absent := _wall(Vector2(0, 40), Vector2(200, 40), [{"key": "on", "values": ["true"]}])
	var r := LandingFold.fold([_hit(host, 10)], _by_id([host, absent]), {"chan:c": "pending"}, {})
	assert_eq(r["placed"][0]["spec"].end, Vector2(100, 70), "an absent surface does not cut")
	Surface.reset_id_counter()
	var host2 := _host(100, "c", _across(false))
	var wall := _wall(Vector2(0, 40), Vector2(200, 40))
	var r2 := LandingFold.fold([_hit(host2, 10)], _by_id([host2, wall]), {"chan:c": "pending"}, {})
	assert_eq(r2["placed"][0]["spec"].end, Vector2(100, 70), "trim off: whole")

func test_stage108_a_spawned_circle_keeps_its_longest_arc():
	var t := SpawnTemplate.make(SurfaceSpec.circle(Vector2(0, 0), 50.0, _side("terminal"), _side("terminal")), M.BEAM_TANGENT)
	var host := _host(100, "c", t)
	var wall := _wall(Vector2(130, -200), Vector2(130, 200))   # cuts the circle about (100, 10) at x = 130
	var r := LandingFold.fold([_hit(host, 10)], _by_id([host, wall]), {"chan:c": "pending"}, {})
	var spec: SurfaceSpec = r["placed"][0]["spec"]
	assert_false(spec.full_circle, "an arc now")
	assert_almost_eq(spec.start.x, 130.0, 1e-3)
	assert_almost_eq(spec.end.x, 130.0, 1e-3)
	assert_lt(spec.via.x, 100.0, "the longer arc (through x = 50, the far left)")

func test_stage108_two_spawns_of_one_landing_do_not_cross():
	var a := _host(100, "a", _across())
	# b's spawn: a horizontal mirror along the beam through (100, 30)
	var along := SpawnTemplate.make(SurfaceSpec.line(Vector2(-150, 0), Vector2(150, 0), _side("reflection", true),
		_side("reflection", true)), M.BEAM_TANGENT)
	var b := _host(300, "b", along)
	var hb := _hit(b, 30)
	hb["point"] = Vector2(100, 30)   # placed through x = 100 (crossing a's spawn)
	var r := LandingFold.fold([_hit(a, 10), hb], _by_id([a, b]), {"chan:a": "pending", "chan:b": "pending"}, {})
	var sa: SurfaceSpec = r["placed"][0]["spec"]
	var sb: SurfaceSpec = r["placed"][1]["spec"]
	assert_eq(Crossings.of(sa.segment(), sb.segment()), [], "the later one is trimmed by the earlier")
	assert_eq(sa.end, Vector2(100, 70), "the first placed is whole")

func test_stage108_untrimmed_when_nothing_crosses():
	var host := _host(100, "c", _across())
	var r := LandingFold.fold([_hit(host, 10)], _by_id([host]), {"chan:c": "pending"}, {})
	var spec: SurfaceSpec = r["placed"][0]["spec"]
	assert_eq([spec.start, spec.end], [Vector2(100, -50), Vector2(100, 70)])
