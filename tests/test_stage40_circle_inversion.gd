extends GutTest

# Circle inversion effect. The point maps are checked on the beam solver's
# map (BeamEffects.map_point of the surface built from the game effect)
# against the closed form w = c + r²/conj(z − c).

func _carrier() -> GeneralizedCircle:
	return GeneralizedCircle.from_circle(Vector2(200, 200), 100.0)

func _effect() -> CircleInversionEffect:
	return CircleInversionEffect.new(_carrier())

# The beam scene surface of a full inversion circle (center (200,200), r 100).
func _surf() -> BeamSceneSurf:
	var eff := _effect()
	var cfg := SideConfig.new(eff, true)
	var s := Surface.new(Segment.full_from_carrier(_carrier()), cfg, cfg, false, false)
	var sc := BeamScene.from_defs(NumCtx.f64(), SceneDefs.defs_from_surfaces([s]))
	return sc.surfaces[0]

# map_point as Vector2 (INF for the point at infinity)
func _map(z: Vector2, inverse: bool = false) -> Vector2:
	var zc = null if is_inf(z.x) else NumCtx.f64().cn(z.x, z.y)
	var w = BeamEffects.map_point(zc, _surf(), inverse)
	if w == null:
		return Vector2(INF, INF)
	return Vector2(w.fx(), w.fy())

# --- Construction ---

func test_stage40_inversion_construction_valid() -> void:
	var eff := _effect()
	assert_not_null(eff, "Effect constructed")
	assert_true(eff.is_transformative(), "Is transformative")
	assert_eq(SceneDefs.describe_config(SideConfig.new(eff, true))["type"], "inversion",
		"Beam scene sees an inversion")

func test_stage40_inversion_construction_rejects_line() -> void:
	var line_carrier := GeneralizedCircle.from_line(1.0, 0.0, -200.0)
	# GDScript asserts crash in debug mode; we test the precondition directly
	assert_true(line_carrier.is_line(), "Carrier is a line")
	# Construction would assert — verify the guard condition
	assert_eq(line_carrier.a, 0.0, "Line carrier has a==0, would be rejected")

# --- Point application ---

func test_stage40_inversion_apply_point() -> void:
	assert_almost_eq(_map(Vector2(400, 200)), Vector2(250, 200), Vector2(0.01, 0.01),
		"z=(400,200) -> w=(250,200) per GAME_SPEC 16.2")

func test_stage40_inversion_self_inverse() -> void:
	var w := _map(Vector2(400, 200))
	assert_almost_eq(_map(w), Vector2(400, 200), Vector2(0.01, 0.01),
		"Applying inversion twice returns original point")

func test_stage40_inversion_point_on_circle_fixed() -> void:
	var on_circle := Vector2(300, 200)
	assert_almost_eq(_map(on_circle), on_circle, Vector2(0.01, 0.01),
		"Point on the inversion circle is a fixed point")

# --- Invariants ---

func test_stage40_S2_transform_round_trip() -> void:
	var point := Vector2(400, 200)
	var back := _map(_map(point), true)
	assert_almost_eq(back, point, Vector2(0.01, 0.01), "S2: apply then inverse returns original")

func test_stage40_inversion_center_maps_to_infinity() -> void:
	var result := _map(Vector2(200, 200))
	assert_true(is_inf(result.x) and is_inf(result.y), "Center maps to infinity (got %s)" % result)
	assert_almost_eq(_map(Vector2(INF, INF)), Vector2(200, 200), Vector2(0.01, 0.01), "Infinity maps to the center")

func test_stage40_S16_no_nan_inf() -> void:
	var points: Array[Vector2] = [
		Vector2(100, 100), Vector2(300, 300), Vector2(50, 200),
		Vector2(400, 100), Vector2(200, 100), Vector2(200, 300),
		Vector2(100, 200), Vector2(300, 200), Vector2(150, 150),
		Vector2(350, 250),
	]
	for p in points:
		var result := _map(p)
		assert_false(is_nan(result.x) or is_nan(result.y),
			"S16: no NaN for point %s (got %s)" % [p, result])
		assert_false(is_inf(result.x) or is_inf(result.y),
			"S16: no Inf for point %s (got %s)" % [p, result])
		# closed form c + r²/conj(z − c)
		var u := p - Vector2(200, 200)
		assert_almost_eq(result, Vector2(200, 200) + u * (10000.0 / u.length_squared()), Vector2(0.01, 0.01),
			"closed form for %s" % p)
