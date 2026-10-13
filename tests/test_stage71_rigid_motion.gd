extends GutTest

const TOL := Vector2(0.01, 0.01)

func _effect(theta: float = PI / 2.0, d: Vector2 = Vector2(100, 50)) -> RigidMotionEffect:
	return RigidMotionEffect.new(theta, d)

func _line_seg() -> Segment:
	return Segment.from_coords(Vector2(100, 200), Vector2(200, 200), Vector2(150, 200))

func _arc_seg() -> Segment:
	return Segment.from_coords(Vector2(300, 200), Vector2(200, 300), Vector2(270.71, 270.71))

# Closed form of a portal map T(z) = e^{iθ}z + d (complex multiply = rotation).
static func _T(theta: float, d: Vector2, z: Vector2) -> Vector2:
	return z.rotated(theta) + d

# The map an effect of a portal pair applies, from its own (_theta, _d).
static func _apply(eff: RigidMotionEffect, z: Vector2) -> Vector2:
	return _T(eff._theta, eff._d, z)

# --- Portal pair: line source ---

func test_stage71_portal_pair_translated() -> void:
	var seg := _line_seg()
	var result := RigidMotionEffect.create_portal_pair(seg, 0.0, Vector2(0, 200))
	var target: Segment = result.target_segment
	assert_almost_eq(target.start.coords, Vector2(100, 400), TOL, "start translated by (0,200)")
	assert_almost_eq(target.end.coords, Vector2(200, 400), TOL, "end translated by (0,200)")
	assert_almost_eq(target.via.coords, Vector2(150, 400), TOL, "via translated by (0,200)")

func test_stage71_portal_pair_rotated() -> void:
	var seg := _line_seg()
	var result := RigidMotionEffect.create_portal_pair(seg, PI / 2.0, Vector2(100, 0))
	var target: Segment = result.target_segment
	# T((100,200)) = e^{iPI/2}*(100,200) + (100,0) = (-200,100) + (100,0) = (-100,100)
	assert_almost_eq(target.start.coords, Vector2(-100, 100), TOL, "start rotated+translated")
	# T((200,200)) = e^{iPI/2}*(200,200) + (100,0) = (-200,200) + (100,0) = (-100,200)
	assert_almost_eq(target.end.coords, Vector2(-100, 200), TOL, "end rotated+translated")

func test_stage71_portal_pair_inverse_composition() -> void:
	var seg := _line_seg()
	var result := RigidMotionEffect.create_portal_pair(seg, PI / 3.0, Vector2(50, -75))
	var points := [Vector2(0, 0), Vector2(100, 200), Vector2(-50, 300), Vector2(999, 1)]
	for p in points:
		assert_almost_eq(_apply(result.source_effect, p), _T(PI / 3.0, Vector2(50, -75), p), TOL,
			"source effect is T(z) = e^{iθ}z + d")
		var roundtrip = _apply(result.target_effect, _apply(result.source_effect, p))
		assert_almost_eq(roundtrip, p, TOL,
			"T_BA(T_AB(%s)) == %s (identity composition)" % [p, p])

# --- Portal pair: arc source ---

func test_stage71_portal_pair_arc_translated() -> void:
	var seg := _arc_seg()
	var result := RigidMotionEffect.create_portal_pair(seg, 0.0, Vector2(300, 0))
	var target: Segment = result.target_segment
	assert_almost_eq(target.start.coords, Vector2(600, 200), TOL, "arc start translated")
	assert_almost_eq(target.end.coords, Vector2(500, 300), TOL, "arc end translated")
	var target_carrier := target.get_carrier()
	var source_carrier := seg.get_carrier()
	assert_false(target_carrier.is_line(), "Target carrier is a circle")
	assert_almost_eq(target_carrier.radius(), source_carrier.radius(), 0.01,
		"Same radius after translation")

func test_stage71_portal_pair_arc_rotated() -> void:
	var seg := _arc_seg()
	var result := RigidMotionEffect.create_portal_pair(seg, PI / 4.0, Vector2.ZERO)
	var target: Segment = result.target_segment
	assert_almost_eq(target.start.coords, _T(PI / 4.0, Vector2.ZERO, seg.start.coords), TOL,
		"start_B.coords == T(start_A.coords)")
	assert_almost_eq(target.end.coords, _T(PI / 4.0, Vector2.ZERO, seg.end.coords), TOL,
		"end_B.coords == T(end_A.coords)")
	assert_almost_eq(target.via.coords, _T(PI / 4.0, Vector2.ZERO, seg.via.coords), TOL,
		"via_B.coords == T(via_A.coords)")
	var target_carrier := target.get_carrier()
	var source_carrier := seg.get_carrier()
	assert_almost_eq(target_carrier.radius(), source_carrier.radius(), 0.01,
		"Same radius after rotation")

func test_stage71_portal_pair_arc_inverse_composition() -> void:
	var seg := _arc_seg()
	var result := RigidMotionEffect.create_portal_pair(seg, PI / 6.0, Vector2(100, -50))
	var points := [Vector2(300, 200), Vector2(250, 250), Vector2(0, 0)]
	for p in points:
		var roundtrip = _apply(result.target_effect, _apply(result.source_effect, p))
		assert_almost_eq(roundtrip, p, TOL,
			"Arc portal round-trip: T_BA(T_AB(%s)) == %s" % [p, p])

# --- Display ---

func test_stage71_display_name_and_color() -> void:
	var eff := _effect()
	assert_eq(eff.get_display_name(), "portal", "Display name is 'portal'")
	assert_eq(eff.get_display_color(), Color.CYAN, "Display color is CYAN")
