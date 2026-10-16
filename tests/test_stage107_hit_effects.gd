extends GutTest

# Stage 107: hit effects, the second axis of a side (GAME_SPEC 9.1, 10.6):
# their flag writes, deferral, the compile into the solver's scene defs, the
# derived target, presence.

func _line(x: float) -> Segment:
	return Segment.from_coords(Vector2(x, -100), Vector2(x, 100), Vector2(x, 0))

func test_stage107_writes_and_deferral():
	var s := StateChangeHit.new("open", StateChange.Op.TOGGLE)
	assert_eq(s.in_shot_writes(7).map(func(w): return w.to_def()), [{"key": "open", "op": "toggle", "value": true}])
	assert_eq(s.landing_writes(7), [] as Array[StateChange])
	var d := StateChangeHit.new("k", StateChange.Op.SET, 2, true)
	assert_eq(d.in_shot_writes(7), [] as Array[StateChange], "deferred: nothing in the shot")
	assert_eq(d.landing_writes(7).map(func(w): return w.to_def()), [{"key": "k", "op": "set", "value": 2}])
	assert_eq(TargetHit.new().in_shot_writes(7).map(func(w): return w.to_def()), [{"key": "target:7", "op": "set", "value": true}])
	assert_eq(ResetHit.new(PackedStringArray(["a", "b"])).in_shot_writes(7).map(func(w): return w.to_def()),
		[{"key": "chan:a", "op": "set", "value": "armed"}, {"key": "chan:b", "op": "set", "value": "armed"}])

func test_stage107_side_compiles_its_writes_in_order():
	var cfg := SideConfig.new(ReflectionEffect.new(_line(0).get_carrier()), true, [
		StateChangeHit.new("a", StateChange.Op.TOGGLE),
		TargetHit.new(),
		StateChangeHit.new("late", StateChange.Op.SET, 1, true),
		ResetHit.new(PackedStringArray(["c"])),
	])
	var d: Dictionary = SceneDefs.describe_config(cfg, 4)
	assert_eq(d["type"], "reflection", "the path axis is untouched")
	assert_eq(d["states"], [
		{"key": "a", "op": "toggle", "value": true},
		{"key": "target:4", "op": "set", "value": true},
		{"key": "chan:c", "op": "set", "value": "armed"},
	], "in-shot writes in list order; the deferred one is not the solver's")
	assert_false(SceneDefs.describe_config(SideConfig.new(null, false), 4).has("states"), "no writes: no states")

func test_stage107_target_is_derived_from_a_target_hit():
	var plain := Surface.new(_line(0), SideConfig.new(), SideConfig.new())
	assert_false(plain.is_target)
	var shared := SideConfig.new(null, false)
	var t := Surface.new(_line(10), shared, shared, true)
	assert_true(t.is_target, "a target constructor flag becomes a TargetHit")
	assert_true(shared.hit_effects.is_empty(), "the shared config is not modified")
	var d := SceneDefs.describe_surface(t)
	assert_true(d["is_target"])
	assert_eq(d["left"]["states"], [{"key": "target:%d" % t.id, "op": "set", "value": true}])
	var side_only := Surface.new(_line(20), SideConfig.new(null, true, [TargetHit.new()]), SideConfig.new())
	assert_true(side_only.is_target, "one side suffices")

func test_stage107_presence_conditions():
	var host := Channel.host_presence("c")
	assert_true(host.holds({}), "a missing channel is armed")
	assert_true(host.holds({"chan:c": "armed"}))
	assert_false(host.holds({"chan:c": "pending"}))
	var g2 := Channel.group_presence("c", 2)
	assert_true(g2.holds({"chan:c": "g2"}))
	assert_false(g2.holds({"chan:c": "g1"}))
	assert_eq(Channel.generation_of("g12"), 12)
	assert_eq(Channel.generation_of("armed"), 0)
	var s := Surface.new(_line(0), SideConfig.new(), SideConfig.new())
	assert_false(SceneDefs.describe_surface(s).has("presence"), "always present: no presence def")
	s.presence.append(host)
	s.presence.append(PresenceCond.new("d", PackedStringArray(["true"])))
	assert_eq(SceneDefs.describe_surface(s)["presence"],
		[{"key": "chan:c", "values": ["DEFAULT", "armed"]}, {"key": "d", "values": ["true"]}])
	assert_false(s.present({}))
	assert_true(s.present({"d": true}))

func test_stage107_toggler_colours_follow_the_hit_axis():
	var ghost := SideConfig.new(null, true, [StateChangeHit.new(BeamScene.GHOST(), StateChange.Op.TOGGLE)])
	var flag := SideConfig.new(null, true, [StateChangeHit.new("open", StateChange.Op.TOGGLE)])
	assert_eq(SurfaceNode.config_color(ghost), SurfaceNode.GHOST_TOGGLER_COLOR)
	assert_eq(SurfaceNode.config_color(flag), SurfaceNode.FLAG_TOGGLER_COLOR)
	assert_eq(SurfaceNode.config_color(SideConfig.new()), SurfaceNode.PASSTHROUGH_COLOR)
