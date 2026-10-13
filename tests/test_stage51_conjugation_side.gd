extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

# --- Scene setup (same as test_inversion_normalization._build_debug_scene) ---

func _build_scene() -> Dictionary:
	var room_walls := RoomBuilder.create_room_surfaces(Rect2(160, 90, 1600, 900))
	var mirror_left := _mirror_line(Vector2(500, 200), Vector2(500, 700))
	var mirror_right := _mirror_line(Vector2(1400, 300), Vector2(1400, 800))
	var mirror_bottom := _mirror_line(Vector2(700, 800), Vector2(1200, 800))
	var mirror_mid := _mirror_right_line(Vector2(960, 200), Vector2(960, 500))
	var inv_seg := Segment.from_coords(Vector2(1100, 400), Vector2(1100, 700), Vector2(1230, 550))
	var inv_effect := CircleInversionEffect.new(inv_seg.get_carrier())
	var inv_surf := Surface.new(inv_seg, SideConfig.new(inv_effect, true), SideConfig.new(null, false), false, true)
	var screen_bounds := _screen_bounds()
	var surfaces: Array = []
	surfaces.append_array(room_walls)
	surfaces.append(mirror_left)
	surfaces.append(mirror_right)
	surfaces.append(mirror_bottom)
	surfaces.append(mirror_mid)
	surfaces.append(inv_surf)
	surfaces.append_array(screen_bounds)
	return {
		"surfaces": surfaces,
		"inversion": inv_surf,
		"mirror_bottom": mirror_bottom,
	}

func _mirror_line(start: Vector2, end_v: Vector2) -> Surface:
	var mid := (start + end_v) / 2.0
	var seg := Segment.from_coords(start, end_v, mid)
	var refl := ReflectionEffect.new(seg.get_carrier())
	return Surface.new(seg, SideConfig.new(refl, true), SideConfig.new(null, false), false, false)

func _mirror_right_line(start: Vector2, end_v: Vector2) -> Surface:
	var mid := (start + end_v) / 2.0
	var seg := Segment.from_coords(start, end_v, mid)
	var refl := ReflectionEffect.new(seg.get_carrier())
	return Surface.new(seg, SideConfig.new(null, false), SideConfig.new(refl, true), false, false)

func _screen_bounds() -> Array:
	var result: Array = []
	var bounds_defs := [
		[Vector2(0, 0), Vector2(1920, 0)],
		[Vector2(1920, 0), Vector2(1920, 1080)],
		[Vector2(1920, 1080), Vector2(0, 1080)],
		[Vector2(0, 1080), Vector2(0, 0)],
	]
	for bd in bounds_defs:
		var s: Vector2 = bd[0]
		var e: Vector2 = bd[1]
		var config := SideConfig.new(null, false)
		result.append(Surface.new(Segment.from_coords(s, e, (s + e) / 2.0), config, config, false, false))
	return result


# Steps carry no frame on the beam solver. A transform was applied at a hit
# when the step ending at the hit carries the surface id and the next step is
# in a different frame (frame_id changes).
func _applied_hits(path: TracePath) -> Array:
	var out: Array = []
	for i in range(path.steps.size() - 1):
		var s: TraceStep = path.steps[i]
		var n: TraceStep = path.steps[i + 1]
		if s.hit != null and s.surface_id >= 0 and n.frame_id != s.frame_id:
			out.append(s.surface_id)
	return out

func _dump(path: TracePath) -> void:
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		gut.p("step %d: %s->%s fid=%d arc=%s sid=%d hit=%s" % [
			i, s.start, s.end, s.frame_id, s.is_arc_step, s.surface_id,
			"side=%d on=%s" % [s.hit.side, s.hit.on_segment] if s.hit else "none"])

# Was a mirror applied, followed (later) by an applied inversion?
func _mirror_then_inversion(path: TracePath, inv_id: int) -> Dictionary:
	var hits := _applied_hits(path)
	var mirror_first := false
	var inverted := false
	for sid in hits:
		if sid == inv_id:
			inverted = true
		elif not inverted:
			mirror_first = true
	return {"mirror_first": mirror_first, "inverted": inverted}

# ==========================================================================
# Regression tests — these must FAIL before the fix, PASS after
# ==========================================================================

func test_no_spurious_inversion_after_reflection() -> void:
	var scene := _build_scene()
	var player := Vector2(844.6, 551.4)
	var aim := Direction.from_coords(player, Vector2(979.0, 767.4))
	var path := TestTrace.trace(player, aim, scene.surfaces, GameState.new())
	_dump(path)
	var inv_id: int = (scene.inversion as Surface).id
	var hits := _applied_hits(path)
	var reflected := false
	for sid in hits:
		if sid != inv_id:
			reflected = true
	assert_true(reflected,
		"After reflecting off mirror, a reflection should be applied")
	assert_false(inv_id in hits,
		"Should NOT apply the inversion — ray hit pass-through side of inversion arc")

func test_pass_through_side_respected_after_reflection() -> void:
	var scene := _build_scene()
	var player := Vector2(844.6, 551.4)
	var aim := Direction.from_coords(player, Vector2(979.0, 767.4))
	var path := TestTrace.trace(player, aim, scene.surfaces, GameState.new())

	var frame_count := 0
	var prev_frame_id := -1
	for step in path.steps:
		var s: TraceStep = step
		if s.frame_id != prev_frame_id:
			frame_count += 1
			prev_frame_id = s.frame_id

	assert_lte(frame_count, 2,
		"Should have at most 2 frames (identity + one reflection). Got %d — inversion was wrongly triggered" % frame_count)

# ==========================================================================
# Carrier identity tests — same_circle and is_inverse_of
# ==========================================================================

func test_same_circle_exact_equality() -> void:
	var c1 := GeneralizedCircle.new(1.0, -2200.0, -1100.0, 907475.0)
	var c2 := GeneralizedCircle.new(1.0, -2200.0, -1100.0, 907475.0)
	assert_true(c1.same_circle(c2),
		"Two GeneralizedCircles with identical coefficients should be same_circle")

	var c3 := GeneralizedCircle.new(1.0, -2200.0, -1100.0, 999999.0)
	assert_false(c1.same_circle(c3),
		"GeneralizedCircles with different coefficients should NOT be same_circle")

	assert_true(c1.same_circle(c1),
		"A circle should be same_circle with itself (reference equality fast path)")

# ==========================================================================
# Arc rehit tests — second hit on same arc after inversion (grazing)
# ==========================================================================

func test_arc_rehit_after_mirror_reflection() -> void:
	var scene := _build_scene()
	var player := Vector2(992.7743, 577.3065)
	var aim := Direction.from_coords(player, Vector2(1068.113, 737.3656))
	var path := TestTrace.trace(player, aim, scene.surfaces, GameState.new())
	_dump(path)
	var r := _mirror_then_inversion(path, (scene.inversion as Surface).id)
	assert_true(r.mirror_first,
		"Ray should reflect off mirror before hitting inversion")
	assert_true(r.inverted,
		"Ray should enter arc mode after mirror reflection + inversion hit")

func test_single_transform_arc_exit() -> void:
	var carrier := GeneralizedCircle.from_circle(Vector2(960, 540), 200.0)
	var inv_seg := Segment.full_from_carrier(carrier)
	var inv_effect := CircleInversionEffect.new(carrier)
	var inv_surf := Surface.new(inv_seg, SideConfig.new(inv_effect, true), SideConfig.new(inv_effect, true), false, false)
	var surfaces: Array = []
	surfaces.append_array(RoomBuilder.create_room_surfaces(Rect2(0, 0, 1920, 1080)))
	surfaces.append(inv_surf)

	var player := Vector2(960, 540)
	var aim := Direction.from_coords(player, Vector2(1200, 540))
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())
	_dump(path)

	# arc mode = steps after the applied inversion (not identity); exit = a later
	# step back in the identity frame (self-inverse re-hit pops the stack)
	var has_arc_frame := false
	var arc_frame_exits := false
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		if s.frame_id != TraceStep.IDENTITY_FRAME:
			has_arc_frame = true
		elif has_arc_frame:
			arc_frame_exits = true

	assert_true(inv_surf.id in _applied_hits(path), "Inversion should be applied")
	assert_true(has_arc_frame,
		"Ray should enter arc mode hitting full circle inversion")
	assert_true(arc_frame_exits,
		"Single-transform arc mode should exit via self-inverse cancellation")

# ==========================================================================
# Investigation: Premature trace termination in arc mode
# ==========================================================================

func test_trace_continues_past_arc_mode() -> void:
	var scene := _build_scene()
	var player := Vector2(960.0, 540.0)
	var aim := Direction.from_coords(player, Vector2(1051.095, 751.3915))
	var path := TestTrace.trace(player, aim, scene.surfaces, GameState.new())
	_dump(path)
	var r := _mirror_then_inversion(path, (scene.inversion as Surface).id)
	assert_true(r.mirror_first,
		"Ray should reflect off mirror before hitting inversion")
	assert_true(r.inverted,
		"Ray should enter arc mode after mirror + inversion")
