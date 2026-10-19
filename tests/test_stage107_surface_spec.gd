extends GutTest

# Stage 107: surfaces as data (SurfaceSpec, SideSpec, EffectSpec,
# HitEffectSpec): every path effect round-trips, validation knows the
# carriers, a rigid frame maps a spec (sides swap under a reflection, a
# portal's motion is conjugated), and a level builds any combination from its
# `surface_specs` export.

func _side(type := "pass", interactive := false, params := {}, hits: Array = []) -> SideSpec:
	var e := {"type": type}
	e.merge(params)
	return SideSpec.make(e, interactive, hits)

func _defs(specs: Array) -> Array:
	Surface.reset_id_counter()
	var surfaces: Array = []
	for s: SurfaceSpec in specs:
		surfaces.append_array(s.build())
	return SceneDefs.defs_from_surfaces(surfaces)

func _line() -> Array:
	return [Vector2(100, -50), Vector2(100, 50)]

func test_stage107_every_path_effect_round_trips():
	var arc := [Vector2(0, -100), Vector2(0, 100), Vector2(100, 0)]
	var cases := [
		[_side("terminal"), false],
		[_side("reflection", true), false],
		[_side("reflection", true), true],
		[_side("inversion", true), true],
		[_side("line_normal_projection", true, {"back": true}), false],
		[_side("circle_normal_projection", true, {"back": false}), true],
		[_side("line_directional_projection", true, {"back": false, "normal": [1.0, 0.0]}), false], # (across the line: B9)
		[_side("circle_directional_projection", true, {"back": true, "normal": [1.0, 0.0]}), true],
		[_side("pass", true, {}, [{"kind": "state", "key": "k", "op": "set", "value": 3, "deferred": false},
			{"kind": "target", "deferred": true}, {"kind": "reset", "channels": ["a"], "deferred": false}]), false],
	]
	for c in cases:
		var side: SideSpec = c[0]
		var spec: SurfaceSpec = SurfaceSpec.arc(arc[0], arc[1], arc[2], side, _side()) if c[1] \
			else SurfaceSpec.line(_line()[0], _line()[1], side, _side())
		assert_eq(spec.validate(), [] as Array[String], "%s valid" % side.effect["type"])
		var surf: Surface = spec.build()[0]
		var back := SurfaceSpec.of(surf)
		assert_eq(back.left.effect, side.effect, "%s: the effect round-trips" % side.effect["type"])
		assert_eq(back.left.hits.size(), side.hits.size(), "the hit effects round-trip")
		for i in side.hits.size():
			var want: Dictionary = side.hits[i].duplicate()
			var got: Dictionary = back.left.hits[i]
			for k in want:
				assert_eq(got.get(k), want[k], "hit %d %s" % [i, k])
		assert_eq(DefsCanon.canon(_defs([back])), DefsCanon.canon(_defs([spec])), "of(build(spec)) builds the same defs")

func test_stage107_portal_builds_its_pair():
	var p := _side("portal", true, {"theta": PI / 2.0, "d": [300.0, 0.0]})
	var spec := SurfaceSpec.line(Vector2(0, -50), Vector2(0, 50), p, p)
	var built := spec.build()
	assert_eq(built.size(), 2, "the pair")
	var defs := _defs([spec])
	assert_eq(defs[0]["left"]["type"], "portal")
	assert_eq(defs[0]["left"]["pair"], defs[1]["id"], "paired")
	assert_eq(defs[1]["left"]["pair"], defs[0]["id"])
	assert_not_null(built[0].get_side_link(Side.Value.LEFT), "linked side to side")

func test_stage107_validation_knows_the_carriers():
	var line := SurfaceSpec.line(_line()[0], _line()[1], _side("inversion", true), _side())
	assert_false(line.validate().is_empty(), "inversion on a line")
	var circ := SurfaceSpec.circle(Vector2.ZERO, 50.0, _side("line_normal_projection", true), _side())
	assert_false(circ.validate().is_empty(), "a line projection on a circle")
	var degenerate := SurfaceSpec.line(Vector2(1, 1), Vector2(1, 1), _side(), _side())
	assert_false(degenerate.validate().is_empty(), "zero length")
	assert_false(SurfaceSpec.circle(Vector2.ZERO, 0.0, _side(), _side()).validate().is_empty(), "zero radius")
	var unknown := SurfaceSpec.line(_line()[0], _line()[1], _side("teleport"), _side())
	assert_false(unknown.validate().is_empty(), "an unknown effect")
	var silly := SurfaceSpec.line(_line()[0], _line()[1], _side("pass", true), _side())
	assert_false(silly.validate().is_empty(), "a plannable pass without hit effects")

func test_stage107_identity_frame_is_bit_exact():
	var spec := SurfaceSpec.arc(Vector2(0.1, -100.3), Vector2(0.7, 100.9), Vector2(100.5, 0.25),
		_side("reflection", true), _side("line_normal_projection", true))
	var m := spec.mapped(Transform2D.IDENTITY)
	assert_eq([m.start, m.end, m.via], [spec.start, spec.end, spec.via])
	assert_eq(m.left.effect, spec.left.effect)
	assert_eq(m.right.effect, spec.right.effect)

func test_stage107_rotation_maps_points_and_normals():
	var spec := SurfaceSpec.line(Vector2(10, 0), Vector2(20, 0), _side("line_directional_projection", true, {"normal": [1.0, 0.0]}), _side())
	var xf := Transform2D(PI / 2.0, Vector2(5, 5))
	var m := spec.mapped(xf)
	assert_almost_eq(m.start, Vector2(5, 15), Vector2(1e-4, 1e-4))
	assert_almost_eq(m.end, Vector2(5, 25), Vector2(1e-4, 1e-4))
	var n := EffectSpec.vec(m.left.effect["normal"])
	assert_almost_eq(n, Vector2(0, 1), Vector2(1e-6, 1e-6), "the normal turns with the frame")
	assert_eq(m.left.effect["type"], "line_directional_projection", "no swap under a rotation")

func test_stage107_reflection_swaps_sides():
	var spec := SurfaceSpec.line(Vector2(0, -10), Vector2(0, 10), _side("reflection", true), _side("terminal"))
	var mirror := Transform2D(Vector2(-1, 0), Vector2(0, 1), Vector2.ZERO)   # x -> -x
	var m := spec.mapped(mirror)
	assert_eq(m.left.effect["type"], "terminal", "the side facing +x faces -x after the mirror: sides swap")
	assert_eq(m.right.effect["type"], "reflection")
	# a beam from a region meets the same behaviour as the mirrored beam from
	# the mirrored region (the side it hits carries the same effect)
	var met := []
	for w in [[spec, Vector2(-50, 0), Vector2(-40, 0)], [m, Vector2(50, 0), Vector2(40, 0)]]:
		Surface.reset_id_counter()
		var surf: Surface = w[0].build()[0]
		var pair: BeamTracerPair = BeamTracer.new().trace_pair(SceneDefs.defs_from_surfaces([surf]), w[1], w[2], [], {}, {})
		var side: int = pair.physical.hits[0]["side"]
		var cfg: SideSpec = w[0].left if side == Side.Value.LEFT else w[0].right
		met.append(cfg.effect["type"])
	assert_eq(met[0], met[1], "the mirrored beam meets the same effect")

func test_stage107_portal_motion_is_conjugated_by_the_frame():
	for xf: Transform2D in [Transform2D(0.7, Vector2(30, -20)), Transform2D(Vector2(-1, 0), Vector2(0, 1), Vector2(4, 9))]:
		var p := _side("portal", true, {"theta": 0.4, "d": [100.0, 20.0]})
		var spec := SurfaceSpec.line(Vector2(0, -50), Vector2(0, 50), p, p)
		var m := spec.mapped(xf)
		var th: float = m.left.effect["theta"]
		var d := EffectSpec.vec(m.left.effect["d"])
		for z in [Vector2(3, 4), Vector2(-60, 10), Vector2(0, 0)]:
			var want: Vector2 = xf * RigidMotionEffect.apply_motion(z, 0.4, Vector2(100, 20))
			var got := RigidMotionEffect.apply_motion(xf * z, th, d)
			assert_almost_eq(got, want, Vector2(1e-3, 1e-3), "T'(F z) = F(T z)")

func test_stage107_presence_survives_build():
	var spec := SurfaceSpec.line(_line()[0], _line()[1], _side("reflection", true), _side())
	spec.presence = [{"key": "chan:c", "values": ["g1"]}]
	var s: Surface = spec.build()[0]
	assert_false(s.present({}))
	assert_true(s.present({"chan:c": "g1"}))
	assert_eq(SceneDefs.describe_surface(s)["presence"], [{"key": "chan:c", "values": ["g1"]}])
	assert_eq(SurfaceSpec.of(s).presence, spec.presence)

func test_stage107_level_builds_surface_specs():
	Surface.reset_id_counter()
	var level := Node2D.new()
	level.set_script(load("res://scripts/game/level_settings.gd"))
	level.build_room = false
	var combo := SurfaceSpec.circle(Vector2(500, 500), 40.0,
		_side("inversion", true, {}, [{"kind": "target"}, {"kind": "state", "key": "k", "op": "toggle", "value": true}]),
		_side("terminal"))
	combo.presence = [{"key": "on", "values": ["true"]}]
	level.surface_specs.append(combo)
	add_child_autofree(level)
	var built: Array = level.surfaces.filter(func(s): return not s.presence.is_empty())
	assert_eq(built.size(), 1, "the spec surface is in the level")
	if built.size() == 1:
		var s: Surface = built[0]
		assert_true(s.is_target)
		var d := SceneDefs.describe_surface(s)
		assert_eq(d["left"]["type"], "inversion")
		assert_eq(d["left"]["states"].size(), 2, "target + toggle, in order")
		assert_eq(d["right"]["type"], "terminal")
