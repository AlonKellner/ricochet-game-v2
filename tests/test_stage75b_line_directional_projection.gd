extends GutTest

const H := preload("res://tests/test_helpers.gd")
const TOL := Vector2(1.0, 1.0)

func before_each() -> void:
	H.reset_counters()

# --- Helpers ---

# Vertical line at x=300, from (300,100) to (300,500)
func _standard_line() -> Segment:
	return Segment.from_coords(Vector2(300, 100), Vector2(300, 500), Vector2(300, 300))

func _room_walls() -> Array:
	return [
		H.wall(0),
		H.wall(800),
		RoomBuilder.create_block_surface(Vector2(0, 0), Vector2(800, 0), Vector2(400, 0)),
		RoomBuilder.create_block_surface(Vector2(0, 600), Vector2(800, 600), Vector2(400, 600)),
	]

# Direction of the step after the first hit on the projective line (beam
# trace; the effects' closed-form apply_forward was removed with the legacy
# solver). Approach from x < 300 hits the side seg.determine_side((200, 300)).
func _exit_dir(effect: Effect, player: Vector2, cursor: Vector2) -> Vector2:
	var surfaces := _room_walls()
	var cfg := SideConfig.new(effect, true)
	var proj_surf := Surface.new(_standard_line(), cfg, cfg, false, false)
	surfaces.append(proj_surf)
	var path := TestTrace.trace(player, Direction.from_coords(player, cursor), surfaces, GameState.new(),
		null, -1.0, TraceMode.PHYSICAL, TraceMode.PHYSICAL, [], null, cursor)
	for i in range(path.steps.size() - 1):
		var step: TraceStep = path.steps[i]
		if step.hit != null and step.surface_id == proj_surf.id:
			var post: TraceStep = path.steps[i + 1]
			assert_false(is_nan(post.start.x) or is_nan(post.end.x) or is_nan(post.start.y) or is_nan(post.end.y),
				"no NaN after the projective")
			return (post.end - post.start).normalized()
	fail_test("no step after the projective hit (player %s, cursor %s)" % [player, cursor])
	return Vector2.ZERO

const FROM_RIGHT_P := Vector2(200, 300)   # x < 300: seg.determine_side((200,300))
const FROM_RIGHT_C := Vector2(400, 300)
const FROM_LEFT_P := Vector2(400, 300)
const FROM_LEFT_C := Vector2(200, 300)

# --- Test 1: exit direction from RIGHT side ---

func test_stage75b_exit_direction_from_right() -> void:
	var dir := _exit_dir(LineDirectionalProjection.new(Vector2(1, 0)), FROM_RIGHT_P, FROM_RIGHT_C)
	assert_almost_eq(absf(dir.x), 1.0, 0.01, "Exit should be along normal (horizontal)")
	assert_almost_eq(dir.y, 0.0, 0.01, "Exit y should be 0")

# --- Test 2: exit direction from LEFT side ---

func test_stage75b_exit_direction_from_left() -> void:
	var effect := LineDirectionalProjection.new(Vector2(1, 0))
	var dir := _exit_dir(effect, FROM_LEFT_P, FROM_LEFT_C)
	assert_almost_eq(absf(dir.x), 1.0, 0.01, "Exit should be along normal (horizontal)")
	assert_almost_eq(dir.y, 0.0, 0.01, "Exit y should be 0")
	# Opposite side should produce opposite direction
	var other := _exit_dir(effect, FROM_RIGHT_P, FROM_RIGHT_C)
	assert_almost_eq(dir.x + other.x, 0.0, 0.01,
		"Opposite sides should produce opposite directions")

# --- Test 3: hit position independent ---

func test_stage75b_hit_position_independent() -> void:
	var effect := LineDirectionalProjection.new(Vector2(1, 0))
	# hits at (300, 150), (300, 300), (300, 450), all from x < 300
	var cursors := [Vector2(400, 0), Vector2(400, 300), Vector2(400, 600)]
	var first_dir := _exit_dir(effect, FROM_RIGHT_P, cursors[0])
	for i in range(1, cursors.size()):
		var dir := _exit_dir(effect, FROM_RIGHT_P, cursors[i])
		assert_almost_eq(dir, first_dir, Vector2(0.01, 0.01),
			"Hit at position %d should produce same direction as position 0" % i)

# --- Test 4: diagonal normal ---

func test_stage75b_diagonal_normal() -> void:
	var dir := _exit_dir(LineDirectionalProjection.new(Vector2(1, 1)), FROM_RIGHT_P, FROM_RIGHT_C)
	var expected := Vector2(1, 1).normalized()
	assert_almost_eq(absf(dir.x), absf(expected.x), 0.01,
		"Diagonal normal: |x| should be ~0.707")
	assert_almost_eq(absf(dir.y), absf(expected.y), 0.01,
		"Diagonal normal: |y| should be ~0.707")

# --- Test 8: S16 - no NaN/Inf ---

func test_stage75b_S16_no_nan_inf() -> void:
	var dir := _exit_dir(LineDirectionalProjection.new(Vector2(1, 0)), FROM_RIGHT_P, FROM_RIGHT_C)
	assert_false(is_nan(dir.x), "direction.x not NaN")
	assert_false(is_nan(dir.y), "direction.y not NaN")
	assert_false(is_inf(dir.x) or is_inf(dir.y), "direction finite")

# --- Test 9: kind is projective ---

func test_stage75b_kind_is_projective() -> void:
	var effect := LineDirectionalProjection.new(Vector2(1, 0))
	assert_eq(effect.kind(), Effect.Kind.PROJECTIVE, "Kind should be PROJECTIVE")

# --- Test 10: project_back exits opposite ---

func test_stage75b_project_back_exits_opposite() -> void:
	var through_dir := _exit_dir(LineDirectionalProjection.new(Vector2(1, 0), false), FROM_RIGHT_P, FROM_RIGHT_C)
	var back_dir := _exit_dir(LineDirectionalProjection.new(Vector2(1, 0), true), FROM_RIGHT_P, FROM_RIGHT_C)
	assert_almost_eq(through_dir.x + back_dir.x, 0.0, 0.01,
		"Through and back should have opposite directions")

# --- Test 11: color and name ---

func test_stage75b_color_and_name() -> void:
	var through := LineDirectionalProjection.new(Vector2(1, 0), false)
	var back := LineDirectionalProjection.new(Vector2(1, 0), true)
	assert_eq(through.get_display_color(), Color(1.0, 0.8, 0.2),
		"Through should be yellow-orange")
	assert_eq(back.get_display_color(), Color(1.0, 0.4, 0.1),
		"Back should be red-orange")
	assert_eq(through.get_display_name(), "line_directional",
		"Through display name")
	assert_eq(back.get_display_name(), "line_directional_back",
		"Back display name")

# --- Test 12: default is through ---

func test_stage75b_default_is_through() -> void:
	var normal := Vector2(1, 0)
	var effect := LineDirectionalProjection.new(normal)
	assert_eq(effect.get_display_color(), Color(1.0, 0.8, 0.2),
		"Default should be through (yellow-orange)")
	assert_eq(effect.get_display_name(), "line_directional",
		"Default display name")
	var explicit := LineDirectionalProjection.new(normal, false)
	var dir_default := _exit_dir(effect, FROM_RIGHT_P, FROM_RIGHT_C)
	var dir_explicit := _exit_dir(explicit, FROM_RIGHT_P, FROM_RIGHT_C)
	assert_almost_eq(dir_default, dir_explicit, Vector2(0.01, 0.01),
		"Default and explicit false should produce same direction")
