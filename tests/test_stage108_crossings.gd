extends GutTest

# Stage 108: proper crossings of two segments (GAME_SPEC 9.7). Geometry is
# exactly representable so every expectation is exact.

func _line(a: Vector2, b: Vector2) -> Segment:
	return Segment.from_coords(a, b, (a + b) / 2.0)

func _circle(c: Vector2, r: float) -> Segment:
	return Segment.full_from_carrier(GeneralizedCircle.from_circle(c, r))

func _points(list: Array) -> Array:
	return list.map(func(x): return x["point"])

func test_stage108_two_lines_cross_once():
	var a := _line(Vector2(500, -300), Vector2(500, 300))
	var b := _line(Vector2(400, 200), Vector2(600, 200))
	var ab := Crossings.of(a, b)
	assert_eq(_points(ab), [Vector2(500, 200)])
	assert_almost_eq(ab[0]["ta"], 500.0 / 600.0, 1e-12, "5/6 along a")
	assert_almost_eq(ab[0]["tb"], 0.5, 1e-12)
	var ba := Crossings.of(b, a)
	assert_eq(_points(ba), [Vector2(500, 200)], "symmetric")
	assert_eq(ba[0]["ta"], ab[0]["tb"])

func test_stage108_touching_is_not_crossing():
	var a := _line(Vector2(0, 0), Vector2(100, 0))
	assert_eq(Crossings.of(a, _line(Vector2(50, 0), Vector2(50, 80))), [], "a T-junction (an endpoint on the other)")
	assert_eq(Crossings.of(a, _line(Vector2(100, 0), Vector2(100, 80))), [], "a corner")
	assert_eq(Crossings.of(a, _line(Vector2(0, 10), Vector2(100, 10))), [], "parallel")
	assert_eq(Crossings.of(a, _line(Vector2(50, 0), Vector2(150, 0))), [], "collinear overlap")
	assert_eq(Crossings.of(a, _line(Vector2(200, -10), Vector2(200, 10))), [], "carriers cross outside a")
	assert_eq(Crossings.of(a, _circle(Vector2(50, 30), 30.0)), [], "a tangent circle (a touch)")

func test_stage108_line_and_circle():
	var c := _circle(Vector2(0, 0), 5.0)
	var l := _line(Vector2(3, -10), Vector2(3, 10))
	var x := Crossings.of(l, c)
	assert_eq(_points(x), [Vector2(3, -4), Vector2(3, 4)], "ordered along the line")
	assert_lt(x[0]["ta"], x[1]["ta"])
	assert_ne(x[0]["enters"], x[1]["enters"], "A enters B at one crossing and exits at the other")
	var y := Crossings.of(c, l)
	assert_eq(y.size(), 2)
	assert_ne(y[0]["enters"], y[1]["enters"])
	# a line ending exactly on the circle: only its other crossing
	assert_eq(_points(Crossings.of(_line(Vector2(3, -10), Vector2(3, 4)), c)), [Vector2(3, -4)])

func test_stage108_arc_limits_its_crossings():
	# the upper half (y < 0 on screen) of the circle r = 5 about the origin
	var arc := Segment.from_coords(Vector2(5, 0), Vector2(-5, 0), Vector2(0, -5))
	var l := _line(Vector2(3, -10), Vector2(3, 10))
	assert_eq(_points(Crossings.of(arc, l)), [Vector2(3, -4)], "the lower crossing is off the arc")

func test_stage108_two_circles():
	var a := _circle(Vector2(0, 0), 5.0)
	var b := _circle(Vector2(6, 0), 5.0)
	var x := Crossings.of(a, b)
	assert_eq(x.size(), 2)
	var pts := _points(x)
	assert_true(pts.has(Vector2(3, 4)) and pts.has(Vector2(3, -4)))
	assert_ne(x[0]["enters"], x[1]["enters"])
	assert_eq(Crossings.of(a, _circle(Vector2(10, 0), 5.0)), [], "externally tangent: a touch")
	assert_eq(Crossings.of(a, _circle(Vector2(0, 0), 3.0)), [], "concentric")

func test_stage108_param_along_segments():
	var l := _line(Vector2(0, 0), Vector2(10, 0))
	assert_eq(Crossings.param(l, Vector2(2.5, 0)), 0.25)
	var arc := Segment.from_coords(Vector2(5, 0), Vector2(-5, 0), Vector2(0, -5))
	assert_almost_eq(Crossings.span(arc), PI, 1e-12)
	assert_almost_eq(Crossings.param(arc, Vector2(0, -5)), PI / 2.0, 1e-12)
	assert_eq(Crossings.span(_circle(Vector2.ZERO, 3.0)), TAU)
