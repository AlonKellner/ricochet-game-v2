extends GutTest

# Stage 107: the spawn placement frame (GAME_SPEC 10.6.1). Inputs are exactly
# representable so every expectation is exact.

const M := SpawnFrame.Mode

func test_stage107_carrier_tangent_follows_the_segment():
	assert_eq(SpawnFrame.carrier_tangent(Vector2(0, 0), Vector2(5, 0), Vector2(10, 0), true, Vector2.ZERO, Vector2(3, 0)),
		Vector2(1, 0), "a line: start -> end")
	assert_eq(SpawnFrame.carrier_tangent(Vector2(10, 0), Vector2(5, 0), Vector2(0, 0), true, Vector2.ZERO, Vector2(3, 0)),
		Vector2(-1, 0), "the reversed line")
	# the arc (10, 0) -> (0, 10) -> (-10, 0) about the origin: at (0, 10) it moves toward -x
	assert_eq(SpawnFrame.carrier_tangent(Vector2(10, 0), Vector2(0, 10), Vector2(-10, 0), false, Vector2.ZERO, Vector2(0, 10)),
		Vector2(-1, 0))
	assert_eq(SpawnFrame.carrier_tangent(Vector2(-10, 0), Vector2(0, 10), Vector2(10, 0), false, Vector2.ZERO, Vector2(0, 10)),
		Vector2(1, 0), "the reversed arc")
	assert_eq(SpawnFrame.carrier_tangent(Vector2(10, 0), Vector2(0, 10), Vector2(-10, 0), false, Vector2.ZERO, Vector2(10, 0)),
		Vector2(0, 1), "at the start")

func test_stage107_the_four_required_modes_and_the_extras():
	var beam := Vector2(1, 0)
	var tangent := Vector2(0, 1)
	assert_eq(SpawnFrame.axis(M.BEAM_TANGENT, beam, tangent), Vector2(1, 0), "aligned with the beam")
	assert_eq(SpawnFrame.axis(M.BEAM_NORMAL, beam, tangent), Vector2(0, 1), "perpendicular to the beam: (x, y) -> (-y, x)")
	assert_eq(SpawnFrame.axis(M.CARRIER_TANGENT, beam, tangent), Vector2(0, 1), "aligned with the carrier")
	assert_eq(SpawnFrame.axis(M.CARRIER_NORMAL, beam, tangent), Vector2(-1, 0), "perpendicular to the carrier, toward the incoming side")
	assert_eq(SpawnFrame.axis(M.CARRIER_NORMAL, -beam, tangent), Vector2(1, 0), "from the other side: the other normal")
	assert_eq(SpawnFrame.axis(M.BEAM_REFLECTED, beam, tangent), Vector2(-1, 0), "a head-on hit reflects back")
	assert_eq(SpawnFrame.axis(M.BEAM_REFLECTED, Vector2(0.75, 0.5), Vector2(1, 0)), Vector2(0.75, -0.5), "mirrored in the tangent")
	assert_eq(SpawnFrame.axis(M.WORLD, beam, tangent), Vector2(1, 0))

func test_stage107_frame_axes_and_the_mirror():
	var f := SpawnFrame.frame(M.BEAM_TANGENT, Vector2(100, 50), Vector2(0, 1), Vector2(1, 0), false, true)
	assert_eq([f.x, f.y, f.origin], [Vector2(0, 1), Vector2(-1, 0), Vector2(100, 50)], "v = u rotated by +90")
	var g := SpawnFrame.frame(M.BEAM_TANGENT, Vector2(100, 50), Vector2(0, 1), Vector2(1, 0), true, true)
	assert_eq(g.y, Vector2(1, 0), "mirrored on a RIGHT-side hit")
	var h := SpawnFrame.frame(M.BEAM_TANGENT, Vector2(100, 50), Vector2(0, 1), Vector2(1, 0), true, false)
	assert_eq(h.y, Vector2(-1, 0), "not mirrored without mirror_with_side")
	assert_lt(g.determinant(), 0.0, "a mirrored frame reflects")

func _side(type := "pass", interactive := false) -> SideSpec:
	return SideSpec.make({"type": type}, interactive)

func test_stage107_tangent_and_perpendicular_placement():
	# a line along the local x axis lies along u; along y, perpendicular to it
	var along := SpawnTemplate.make(SurfaceSpec.line(Vector2(-20, 0), Vector2(20, 0), _side("reflection", true), _side("reflection", true)),
		M.CARRIER_TANGENT)
	var across := SpawnTemplate.make(SurfaceSpec.line(Vector2(0, -20), Vector2(0, 20), _side("terminal"), _side("terminal")),
		M.CARRIER_TANGENT)
	var f := SpawnFrame.frame(M.CARRIER_TANGENT, Vector2(100, 50), Vector2(0, 1), Vector2(1, 0), false, false)
	var a := along.place(f)
	assert_eq([a.start, a.end], [Vector2(80, 50), Vector2(120, 50)], "tangent to the carrier")
	var b := across.place(f)
	assert_eq([b.start, b.end], [Vector2(100, 30), Vector2(100, 70)], "perpendicular to the carrier")

func test_stage107_circle_template_is_placed_by_its_center():
	var t := SpawnTemplate.make(SurfaceSpec.circle(Vector2(30, 0), 10.0, _side("inversion", true), _side("inversion", true)), M.BEAM_TANGENT)
	var f := SpawnFrame.frame(M.BEAM_TANGENT, Vector2(100, 50), Vector2(0, -1), Vector2(1, 0), false, false)
	var c := t.place(f)
	assert_true(c.full_circle)
	assert_eq(c.center, Vector2(100, 20), "30 along the beam")
	assert_eq(c.radius, 10.0)

func test_stage107_landing_places_at_the_hit_on_an_arc_host():
	# the host: the arc (10, 0) -> (0, 10) -> (-10, 0) around (0, 0); a hit at
	# (0, 10) from outside (the beam going -y): the carrier tangent there is -x
	var host_spec := SurfaceSpec.arc(Vector2(10, 0), Vector2(-10, 0), Vector2(0, 10), _side(), _side())
	Surface.reset_id_counter()
	var host: Surface = host_spec.build()[0]
	var t := SpawnTemplate.make(SurfaceSpec.line(Vector2(5, 0), Vector2(15, 0), _side("terminal"), _side("terminal")), M.CARRIER_TANGENT)
	var spec := LandingFold.place(t, host, {"point": Vector2(0, 10), "tangent": Vector2(0, -1), "side": Side.Value.LEFT})
	assert_almost_eq(spec.start, Vector2(-5, 10), Vector2(1e-4, 1e-4))
	assert_almost_eq(spec.end, Vector2(-15, 10), Vector2(1e-4, 1e-4))

# A line placed at an arbitrary angle stays an exact line: rotated float32
# points are almost never exactly collinear, and a three-point segment would
# become a huge arc (the solver would see a circle; the preview's dashes
# overflowed the canvas). Regression for the user's report on hit_effects.tscn.
func test_stage107_rotated_line_template_stays_a_line():
	var t := SpawnTemplate.make(SurfaceSpec.line(Vector2(100, -40), Vector2(100, 40), _side("reflection", true), _side("reflection", true)),
		M.BEAM_TANGENT)
	for angle in [0.3, 1.234567, 2.9, -0.71]:
		var beam := Vector2.from_angle(angle)
		var f := SpawnFrame.frame(M.BEAM_TANGENT, Vector2(503.37, 271.91), beam, Vector2(0, 1), false, false)
		var placed := t.place(f)
		assert_true(placed.straight, "marked straight")
		var seg := placed.segment()
		assert_true(seg.is_line(), "an exact line at %s rad" % angle)
		Surface.reset_id_counter()
		var d := SceneDefs.describe_surface(placed.build()[0])
		assert_true(d["line"], "the solver sees a line")
	var p := _side("portal", true)
	p.effect = {"type": "portal", "theta": 0.7, "d": [300.5, -20.25]}
	var pair := SurfaceSpec.line(Vector2(10, 3), Vector2(91, 57), p, p).build()
	assert_true(pair[1].segment.is_line(), "a rotated portal's image of a line is a line")

# A template is centred on the hit point by default; `offset` (in the frame's
# axes) moves it deliberately.
func test_stage107_template_offset_is_in_the_frame_axes():
	var line := SurfaceSpec.line(Vector2(-20, 0), Vector2(20, 0), _side("terminal"), _side("terminal"))
	var f := SpawnFrame.frame(M.BEAM_TANGENT, Vector2(100, 50), Vector2(0, 1), Vector2(1, 0), false, false)
	var centred := SpawnTemplate.make(line, M.BEAM_TANGENT).place(f)
	assert_eq([centred.start, centred.end], [Vector2(100, 30), Vector2(100, 70)], "centred on the hit point")
	var ahead := SpawnTemplate.make(line, M.BEAM_TANGENT, false, Vector2(50, 10)).place(f)
	assert_eq([ahead.start, ahead.end], [Vector2(90, 80), Vector2(90, 120)], "50 along u (the beam), 10 along v")
