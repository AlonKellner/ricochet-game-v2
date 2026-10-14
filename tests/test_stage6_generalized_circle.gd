extends GutTest

func test_stage6_line_construction() -> void:
	var gc := GeneralizedCircle.make(0, 1, 0, -5)
	assert_true(gc.is_line(), "a=0 should be a line")

func test_stage6_circle_construction() -> void:
	var gc := GeneralizedCircle.make(1, -4, -6, 12)
	assert_false(gc.is_line(), "a!=0 should be a circle")
	assert_almost_eq(gc.center().x, 2.0, 0.001, "Center x should be 2")
	assert_almost_eq(gc.center().y, 3.0, 0.001, "Center y should be 3")
	assert_almost_eq(gc.radius(), 1.0, 0.001, "Radius should be 1")

func test_stage6_center_from_coefficients() -> void:
	var gc := GeneralizedCircle.make(1, -200, -400, 40000)
	assert_almost_eq(gc.center().x, 100.0, 0.001, "Center x should be 100")
	assert_almost_eq(gc.center().y, 200.0, 0.001, "Center y should be 200")
	assert_almost_eq(gc.radius(), 100.0, 0.001, "Radius should be 100")

func test_stage6_evaluate_on_line() -> void:
	var gc := GeneralizedCircle.make(0, 1, 0, -5)
	assert_almost_eq(gc.evaluate(Vector2(5, 3)), 0.0, 0.001, "Point on line should evaluate to 0")
	assert_almost_eq(gc.evaluate(Vector2(3, 3)), -2.0, 0.001, "Point off line should evaluate to -2")

func test_stage6_evaluate_on_circle() -> void:
	var gc := GeneralizedCircle.from_circle(Vector2(200, 200), 100)
	assert_almost_eq(gc.evaluate(Vector2(300, 200)), 0.0, 0.1, "Point on circle should evaluate to ~0")
	assert_lt(gc.evaluate(Vector2(200, 200)), 0.0, "Point inside circle should evaluate negative (a>0)")
	assert_gt(gc.evaluate(Vector2(400, 200)), 0.0, "Point outside circle should evaluate positive")

func test_stage6_from_line_convenience() -> void:
	var gc := GeneralizedCircle.from_line(1, 0, -100)
	assert_true(gc.is_line(), "from_line should create a line (a=0)")
	assert_eq(gc.a, 0.0, "a should be 0")
	assert_almost_eq(gc.evaluate(Vector2(100, 42)), 0.0, 0.001, "x=100 should be on line")

func test_stage6_from_circle_convenience() -> void:
	var gc := GeneralizedCircle.from_circle(Vector2(200, 200), 100)
	assert_false(gc.is_line(), "from_circle should create a circle")
	assert_almost_eq(gc.center().x, 200.0, 0.001, "Center x should be 200")
	assert_almost_eq(gc.center().y, 200.0, 0.001, "Center y should be 200")
	assert_almost_eq(gc.radius(), 100.0, 0.001, "Radius should be 100")

func test_stage6_no_nan_inf() -> void:
	var gc := GeneralizedCircle.from_circle(Vector2(500, 500), 50)
	var val := gc.evaluate(Vector2(510, 510))
	assert_false(is_nan(val), "S16: evaluate should not return NaN")
	assert_false(is_inf(val), "S16: evaluate should not return Inf")
	assert_false(is_nan(gc.center().x), "S16: center.x should not be NaN")
	assert_false(is_nan(gc.radius()), "S16: radius should not be NaN")
