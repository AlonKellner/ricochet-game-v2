extends GutTest

const H := preload("res://tests/test_helpers.gd")
const TOL := Vector2(0.5, 0.5)

func before_each() -> void:
	H.reset_counters()

# Reflection maps are checked on the beam solver's map (BeamEffects.map_point
# of the surface built from the game effect) against closed forms.

func _surf_of(seg: Segment, eff: Effect) -> BeamSceneSurf:
	var cfg := SideConfig.new(eff, true)
	var s := Surface.new(seg, cfg, cfg, false, false)
	return BeamScene.from_defs(NumCtx.f64(), SceneDefs.defs_from_surfaces([s])).surfaces[0]

func _line_surf(s: Vector2, e: Vector2) -> BeamSceneSurf:
	var seg := Segment.from_coords(s, e, (s + e) / 2.0)
	return _surf_of(seg, ReflectionEffect.new(seg.get_carrier()))

func _circle_surf(eff_is_inversion: bool) -> BeamSceneSurf:
	var carrier := GeneralizedCircle.from_circle(Vector2(960, 540), 200.0)
	var eff: Effect = CircleInversionEffect.new(carrier) if eff_is_inversion else ReflectionEffect.new(carrier)
	return _surf_of(Segment.full_from_carrier(carrier), eff)

func _map(surf: BeamSceneSurf, z: Vector2) -> Vector2:
	var w = BeamEffects.map_point(NumCtx.f64().cn(z.x, z.y), surf)
	return Vector2(INF, INF) if w == null else Vector2(w.fx(), w.fy())

# --- Line reflection correctness ---

func test_vertical_line_reflection() -> void:
	var result := _map(_line_surf(Vector2(500, 0), Vector2(500, 1000)), Vector2(300, 400))
	assert_almost_eq(result, Vector2(700, 400), TOL, "Vertical x=500: (300,400) → (700,400)")

func test_horizontal_line_reflection() -> void:
	var result := _map(_line_surf(Vector2(0, 500), Vector2(1000, 500)), Vector2(300, 400))
	assert_almost_eq(result, Vector2(300, 600), TOL, "Horizontal y=500: (300,400) → (300,600)")

func test_diagonal_line_reflection() -> void:
	var result := _map(_line_surf(Vector2(0, 0), Vector2(1000, 1000)), Vector2(300, 100))
	assert_almost_eq(result, Vector2(100, 300), TOL, "Diagonal y=x: (300,100) → (100,300)")

# --- Self-inverse property ---

func test_horizontal_reflection_self_inverse() -> void:
	var m := _line_surf(Vector2(0, 500), Vector2(1000, 500))
	var z := Vector2(300, 400)
	var result := _map(m, _map(m, z))
	assert_almost_eq(result, z, TOL, "T(T(z)) = z for horizontal line")

func test_diagonal_reflection_self_inverse() -> void:
	var m := _line_surf(Vector2(0, 0), Vector2(1000, 1000))
	var z := Vector2(300, 100)
	var result := _map(m, _map(m, z))
	assert_almost_eq(result, z, TOL, "T(T(z)) = z for diagonal line")

# --- Circle reflection ---

func test_circle_reflection_point_on_circle_fixed() -> void:
	var on_circle := Vector2(960, 340)
	var result := _map(_circle_surf(false), on_circle)
	assert_almost_eq(result, on_circle, TOL, "Point on circle maps to itself")

func test_circle_reflection_equals_inversion() -> void:
	var z := Vector2(800, 400)
	var r_result := _map(_circle_surf(false), z)
	var i_result := _map(_circle_surf(true), z)
	var u := z - Vector2(960, 540)
	assert_almost_eq(r_result, Vector2(960, 540) + u * (40000.0 / u.length_squared()), TOL,
		"circle reflection is inversion c + r²/conj(z − c)")
	assert_almost_eq(r_result, i_result, TOL,
		"ReflectionEffect on circle matches CircleInversionEffect")

func test_circle_reflection_self_inverse() -> void:
	var m := _circle_surf(false)
	var z := Vector2(800, 400)
	var result := _map(m, _map(m, z))
	assert_almost_eq(result, z, TOL, "T(T(z)) = z for circle carrier")

# --- Semi-circle trace produces reflected steps ---

func test_semicircle_interior_hit_reflects() -> void:
	var seg := Segment.from_coords(
		Vector2(1160, 540), Vector2(760, 540), Vector2(960, 340))
	var carrier := seg.get_carrier()
	var refl := ReflectionEffect.new(carrier)
	var config := SideConfig.new(refl, true)
	var surf := Surface.new(seg, config, config, false, false)
	var room := RoomBuilder.create_room_surfaces(Rect2(0, 0, 1920, 1080))
	var surfaces: Array = room + [surf]
	var player := Vector2(600, 540)
	var cursor := Vector2(960, 400)
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())
	var has_reflected := false
	for step in path.steps:
		if step.frame_id != 0:
			has_reflected = true
			break
	assert_true(has_reflected, "Ray hitting semicircle interior should produce reflected steps")
