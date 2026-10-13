extends GutTest

func test_project_onto_vertical_line() -> void:
	var carrier := GeneralizedCircle.from_line(1.0, 0.0, -1700.0)
	var point := Vector2(1697.5, 400.0)
	var projected := VisualConverter.project_onto_carrier(point, carrier)
	assert_almost_eq(projected.x, 1700.0, 0.001, "X should snap to x=1700")
	assert_almost_eq(projected.y, 400.0, 0.001, "Y should be unchanged")

func test_project_onto_horizontal_line() -> void:
	var carrier := GeneralizedCircle.from_line(0.0, 1.0, -100.0)
	var point := Vector2(500.0, 102.3)
	var projected := VisualConverter.project_onto_carrier(point, carrier)
	assert_almost_eq(projected.x, 500.0, 0.001, "X should be unchanged")
	assert_almost_eq(projected.y, 100.0, 0.001, "Y should snap to y=100")

func test_project_onto_circle_outside() -> void:
	var carrier := GeneralizedCircle.from_circle(Vector2(350, 250), 50.0)
	var point := Vector2(410.0, 250.0)
	var projected := VisualConverter.project_onto_carrier(point, carrier)
	assert_almost_eq(projected.x, 400.0, 0.01, "Should snap to circle at (400, 250)")
	assert_almost_eq(projected.y, 250.0, 0.01, "Y should stay on horizontal radius")

func test_project_onto_circle_inside() -> void:
	var carrier := GeneralizedCircle.from_circle(Vector2(350, 250), 50.0)
	var point := Vector2(330.0, 250.0)
	var projected := VisualConverter.project_onto_carrier(point, carrier)
	assert_almost_eq(projected.x, 300.0, 0.01, "Should snap to circle at (300, 250)")
	assert_almost_eq(projected.y, 250.0, 0.01, "Y should stay on horizontal radius")

func test_point_already_on_line_carrier() -> void:
	var carrier := GeneralizedCircle.from_line(1.0, 0.0, -500.0)
	var point := Vector2(500.0, 300.0)
	var projected := VisualConverter.project_onto_carrier(point, carrier)
	assert_almost_eq(projected, point, Vector2(0.001, 0.001), "Point on carrier should stay unchanged")

func test_point_already_on_circle_carrier() -> void:
	var carrier := GeneralizedCircle.from_circle(Vector2(0, 0), 100.0)
	var point := Vector2(100.0, 0.0)
	var projected := VisualConverter.project_onto_carrier(point, carrier)
	assert_almost_eq(projected, point, Vector2(0.001, 0.001), "Point on carrier should stay unchanged")

func test_project_carrier_distance_is_zero() -> void:
	var carrier := GeneralizedCircle.from_circle(Vector2(350, 250), 50.0)
	var point := Vector2(355.0, 260.0)
	var projected := VisualConverter.project_onto_carrier(point, carrier)
	var dist := absf(carrier.evaluate(projected))
	assert_lt(dist, 0.01, "Projected point should evaluate to ~0 on carrier")
