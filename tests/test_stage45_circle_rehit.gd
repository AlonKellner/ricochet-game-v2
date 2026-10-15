extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

func _full_circle_mirror(center: Vector2, r: float) -> Surface:
	var carrier := GeneralizedCircle.from_circle(center, r)
	var seg := Segment.full_from_carrier(carrier)
	var refl := ReflectionEffect.new(carrier)
	var config := SideConfig.new(refl, true)
	return Surface.new(seg, config, config, false, false)

# Beam steps carry no frame. With a single circle mirror in the scene the
# (conjugating) reflected frame is exactly "frame_id is not identity".
func _reflected(s: TraceStep) -> bool:
	return s.frame_id != TraceStep.IDENTITY_FRAME

func test_ray_hits_full_circle_twice() -> void:
	var center := Vector2(960, 540)
	var r := 200.0
	var surf := _full_circle_mirror(center, r)
	# No room walls — under circle inversion, walls map through the center
	# and their terminal effects would absorb the ray before the re-hit.
	var surfaces: Array = [surf]
	var player := Vector2(600, 540)
	var cursor := Vector2(1300, 540)
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())

	var transitions: Array = []
	var prev_conj := false
	for step in path.steps:
		var s: TraceStep = step
		var conj := _reflected(s)
		if conj != prev_conj:
			transitions.append(conj)
			prev_conj = conj

	assert_true(transitions.size() >= 2,
		"Should transition identity→reflected→identity. Got transitions=%s" % [transitions])
	assert_true(transitions[0], "First transition should enter reflected frame")
	if transitions.size() >= 2:
		assert_false(transitions[1], "Second transition should return to identity")

	var reflected_steps := 0
	for step in path.steps:
		var s: TraceStep = step
		if _reflected(s) and s.hit != null:
			reflected_steps += 1
	assert_gt(reflected_steps, 0,
		"Should have at least one surface hit inside the reflected frame (the re-hit)")

func test_no_infinite_loop_from_same_point() -> void:
	var center := Vector2(960, 540)
	var r := 200.0
	var surf := _full_circle_mirror(center, r)
	var surfaces: Array = [surf]
	var player := Vector2(600, 540)
	var cursor := Vector2(1300, 540)
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())

	assert_lt(path.steps.size(), BeamSolver.MAX_HITS * 4,
		"Should be bounded by MAX_HITS. Got %d steps" % path.steps.size())

func _build_below_scene() -> Dictionary:
	var center := Vector2(960, 540)
	var r := 200.0
	var seg := Segment.from_coords(
		Vector2(center.x + r, center.y + 0.01),
		Vector2(center.x + r, center.y - 0.01),
		Vector2(center.x - r, center.y))
	var refl := ReflectionEffect.new(seg.get_carrier())
	var config := SideConfig.new(refl, true)
	var surf := Surface.new(seg, config, config, false, false)
	var walls := RoomBuilder.create_room_surfaces(Rect2(160, 90, 1600, 900))
	var surfaces: Array = [surf]
	surfaces.append_array(walls)
	return {"surfaces": surfaces, "bounds": Rect2(0, 0, 1920, 1080),
			"player": Vector2(960, 950), "center": center, "radius": r,
			"carrier": seg.get_carrier()}

func _aim_at_offset(player: Vector2, center: Vector2, offset_deg: float) -> Direction:
	var base_angle := (center - player).angle()
	var aim_angle := base_angle + deg_to_rad(offset_deg)
	var cursor := player + Vector2(cos(aim_angle), sin(aim_angle)) * 500.0
	return Direction.from_coords(player, cursor)

func test_grazing_angle_produces_reflected_arc() -> void:
	var scene := _build_below_scene()
	var surfaces: Array = scene["surfaces"]
	var player: Vector2 = scene["player"]
	var center: Vector2 = scene["center"]

	var aim := _aim_at_offset(player, center, 29.0)
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())

	assert_gte(path.steps.size(), 2,
		"Near-tangent ray at 29° should reflect and produce ≥2 steps, got %d" % path.steps.size())

	var aim_neg := _aim_at_offset(player, center, -29.0)
	var path_neg := TestTrace.trace(player, aim_neg, surfaces, GameState.new())

	assert_gte(path_neg.steps.size(), 2,
		"Near-tangent ray at -29° should reflect and produce ≥2 steps, got %d" % path_neg.steps.size())

func test_arc_via_within_drawn_arc() -> void:
	var scene := _build_below_scene()
	var surfaces: Array = scene["surfaces"]
	var player: Vector2 = scene["player"]
	var center: Vector2 = scene["center"]

	var violations: Array[String] = []
	for angle_i in range(-28, 29):
		var angle := float(angle_i)
		var aim := _aim_at_offset(player, center, angle)
		var path := TestTrace.trace(player, aim, surfaces, GameState.new())
		for i in path.steps.size():
			var step: TraceStep = path.steps[i]
			if not step.is_arc_step:
				continue
			if not VisualConverter.is_arc(step.start, step.via, step.end):
				continue
			var p := VisualConverter.arc_params(step.start, step.via, step.end)
			var ctr: Vector2 = p["center"]
			var sa: float = p["start_angle"]
			var span: float = p["span"]
			var cw: bool = p["clockwise"]
			var via_angle := (step.via - ctr).angle()
			var diff: float
			if cw:
				diff = fposmod(sa - via_angle, TAU)
			else:
				diff = fposmod(via_angle - sa, TAU)
			if diff > span + 0.01:
				violations.append("angle=%d step %d: diff=%.3f > span=%.3f" %
					[angle_i, i, diff, span])

	assert_eq(violations.size(), 0,
		"Via points must be on drawn arcs. Violations:\n%s" % "\n".join(violations))

func test_reflected_arc_symmetric_for_both_sides() -> void:
	# Rays hitting above vs below center must produce symmetric arcs —
	# same span, mirrored clockwise flags. Verifies the tracer logic is correct.
	var surf := _full_circle_mirror(Vector2(960, 540), 200.0)
	var walls := RoomBuilder.create_room_surfaces(Rect2(160, 90, 1600, 900))
	var surfaces: Array = [surf]
	surfaces.append_array(walls)
	var player := Vector2(600, 540)

	for dy in [20, 60, 100]:
		var aim_above := Direction.from_coords(player, Vector2(1300, 540 - dy))
		var aim_below := Direction.from_coords(player, Vector2(1300, 540 + dy))
		var path_above := TestTrace.trace(player, aim_above, surfaces, GameState.new())
		var path_below := TestTrace.trace(player, aim_below, surfaces, GameState.new())

		assert_eq(path_above.steps.size(), path_below.steps.size(),
			"dy=%d: both rays should produce same number of steps" % dy)

		for i in path_above.steps.size():
			var sa: TraceStep = path_above.steps[i]
			var sb: TraceStep = path_below.steps[i]
			if not sa.is_arc_step:
				continue
			if not VisualConverter.is_arc(sa.start, sa.via, sa.end):
				continue
			var pa := VisualConverter.arc_params(sa.start, sa.via, sa.end)
			var pb := VisualConverter.arc_params(sb.start, sb.via, sb.end)
			assert_almost_eq(pa["span"], pb["span"], 0.01,
				"dy=%d step %d: symmetric rays should have same arc span" % [dy, i])
			assert_ne(pa["clockwise"], pb["clockwise"],
				"dy=%d step %d: symmetric rays should have opposite clockwise flags" % [dy, i])

# --- Step 6: End-to-end circle grazing scene verification ---

func test_circle_grazing_double_bounce_is_straight_line() -> void:
	var center := Vector2(960, 540)
	var r := 200.0
	var surf := _full_circle_mirror(center, r)
	var surfaces: Array = [surf]
	var player := Vector2(600, 540)
	var violations: Array[String] = []
	for angle_i in range(-25, 26):
		var cursor := Vector2(1300, 540 + angle_i * 4)
		var aim := Direction.from_coords(player, cursor)
		var path := TestTrace.trace(player, aim, surfaces, GameState.new())
		var in_reflected := false
		for i in path.steps.size():
			var s: TraceStep = path.steps[i]
			if _reflected(s):
				in_reflected = true
			elif in_reflected:
				if s.frame_id != TraceStep.IDENTITY_FRAME:
					violations.append(
						"angle=%d step %d: post-double frame_id=%d, expected identity" %
						[angle_i, i, s.frame_id])
				if s.is_arc_step:
					violations.append(
						"angle=%d step %d: post-double step is arc, expected straight line" %
						[angle_i, i])
				break
	assert_eq(violations.size(), 0,
		"Circle grazing: post-double-reflection must be straight line.\n%s" %
		"\n".join(violations))

# --- Inner-side reflection investigation ---

func _semicircle_mirror(center: Vector2, r: float) -> Surface:
	var seg := Segment.from_coords(
		Vector2(center.x + r, center.y),
		Vector2(center.x - r, center.y),
		Vector2(center.x, center.y - r))
	var refl := ReflectionEffect.new(seg.get_carrier())
	var config := SideConfig.new(refl, true)
	return Surface.new(seg, config, config, false, false)

func test_full_circle_inner_hit_produces_no_escape() -> void:
	var center := Vector2(960, 540)
	var r := 200.0
	var surf := _full_circle_mirror(center, r)
	var surfaces: Array = [surf]
	# Player inside circle, aim left to hit left side (away from gap at top)
	var player := Vector2(960, 600)
	var aim := Direction.from_coords(player, Vector2(700, 540))
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())

	var circle_center := center
	var has_escape_to_center := _escapes_to_center(path, circle_center)

	assert_false(has_escape_to_center,
		"Inner-side hit should NOT produce escape arcs to circle center")

func test_semicircle_inner_hit_produces_no_escape() -> void:
	var center := Vector2(960, 540)
	var r := 200.0
	var surf := _semicircle_mirror(center, r)
	var surfaces: Array = [surf]
	var player := Vector2(960, 640)
	var aim := Direction.from_coords(player, Vector2(860, 340))
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())

	var circle_center := center
	var has_escape_to_center := _escapes_to_center(path, circle_center)

	assert_false(has_escape_to_center,
		"Semicircle inner-side hit should NOT produce escape arcs to circle center")

# A step touching the circle center that is not the passage of the inverted
# frame's infinity (Stage 101: the beam passes the center exactly, the step
# ending there carries INFINITY_ID and the next one continues from it).
func _escapes_to_center(path: TracePath, center: Vector2) -> bool:
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		if s.end.distance_to(center) < 5.0 and s.surface_id != BeamSolver.INFINITY_ID:
			return true
		if s.start.distance_to(center) < 5.0 and (i == 0 or path.steps[i - 1].surface_id != BeamSolver.INFINITY_ID):
			return true
	return false

func test_full_circle_inner_hit_returns_to_identity() -> void:
	var center := Vector2(960, 540)
	var r := 200.0
	var surf := _full_circle_mirror(center, r)
	var surfaces: Array = [surf]
	# Player inside circle, aim left (away from gap at top)
	var player := Vector2(960, 600)
	var aim := Direction.from_coords(player, Vector2(700, 540))
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())

	var transitions: Array = []
	var prev_conj := false
	for step in path.steps:
		var s: TraceStep = step
		var conj := _reflected(s)
		if conj != prev_conj:
			transitions.append(conj)
			prev_conj = conj

	assert_true(transitions.size() >= 2,
		"Inner hit should transition identity->reflected->identity. Got transitions=%s" % [transitions])
	if transitions.size() >= 2:
		assert_true(transitions[0], "First transition should enter reflected frame")
		assert_false(transitions[1], "Second transition should return to identity")

func test_outer_hit_still_works() -> void:
	var center := Vector2(960, 540)
	var r := 200.0
	var surf := _full_circle_mirror(center, r)
	var surfaces: Array = [surf]
	# Player outside circle, aim right to hit left side (away from gap at top)
	var player := Vector2(600, 540)
	var aim := Direction.from_coords(player, Vector2(1300, 540))
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())

	var transitions: Array = []
	var prev_conj := false
	for step in path.steps:
		var s: TraceStep = step
		var conj := _reflected(s)
		if conj != prev_conj:
			transitions.append(conj)
			prev_conj = conj

	assert_true(transitions.size() >= 2,
		"Outer hit should have at least one identity->reflected->identity cycle. Got transitions=%s" % [transitions])
	assert_true(transitions[0], "First transition should enter reflected frame")
	assert_false(transitions[1], "Second transition should return to identity")

func test_no_arcs_in_identity_frame_sweep() -> void:
	var center := Vector2(960, 540)
	var r := 200.0
	var surf := _full_circle_mirror(center, r)
	var surfaces: Array = [surf]
	var player := Vector2(600, 540)
	var violations: Array[String] = []
	for angle_i in range(-25, 26):
		var cursor := Vector2(1300, 540 + angle_i * 4)
		var aim := Direction.from_coords(player, cursor)
		var path := TestTrace.trace(player, aim, surfaces, GameState.new())
		for i in path.steps.size():
			var s: TraceStep = path.steps[i]
			if s.frame_id == TraceStep.IDENTITY_FRAME and s.is_arc_step:
				violations.append("angle=%d step %d: identity frame but is_arc_step" %
					[angle_i, i])
	assert_eq(violations.size(), 0,
		"No identity-frame step should be an arc.\n%s" % "\n".join(violations))

func test_proof_no_escape_to_center_after_fix() -> void:
	var center := Vector2(960, 540)
	var r := 200.0
	var surf := _full_circle_mirror(center, r)
	var surfaces: Array = [surf]
	var player := Vector2(960, 600)
	var aim := Direction.from_coords(player, Vector2(700, 540))
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())

	var found_center_step := _escapes_to_center(path, center)

	assert_false(found_center_step,
		"Inner hit should NOT produce escape arcs to circle center (bug was fixed)")

# --- Wrap carrier investigation ---

func _inner_trace() -> Dictionary:
	var center := Vector2(960, 540)
	var r := 200.0
	var surf := _full_circle_mirror(center, r)
	var carrier := surf.segment.get_carrier()
	var surfaces: Array = [surf]
	var bounds := Rect2(0, 0, 1920, 1080)
	var player := Vector2(960, 600)
	var aim := Direction.from_coords(player, Vector2(700, 540))
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())
	return {"path": path, "carrier": carrier, "center": center, "r": r,
			"surf": surf, "bounds": bounds}

# Phase 1: Reproduction

func test_repro_inner_arc_carrier_differs_from_surface_carrier() -> void:
	var d := _inner_trace()
	var path: TracePath = d["path"]
	var surface_carrier: GeneralizedCircle = d["carrier"]

	var arc_count := 0
	var on_carrier_count := 0
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		if not s.is_arc_step:
			continue
		arc_count += 1
		var eval_via := surface_carrier.evaluate(s.via)
		if absf(eval_via) < 1.0:
			on_carrier_count += 1

	assert_gt(arc_count, 0, "Should have reflected arc steps")
	assert_eq(on_carrier_count, 0,
		"Arc via should NOT sit on surface carrier (must curve outward, not follow circle)")

func test_repro_inner_arc_has_multiple_carriers() -> void:
	var d := _inner_trace()
	var path: TracePath = d["path"]

	var carriers: Array = []
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		if not s.is_arc_step or not VisualConverter.is_arc(s.start, s.via, s.end):
			continue
		var step_carrier := Segment.derive_carrier(s.start, s.end, s.via)
		if step_carrier.is_line():
			continue
		var c := step_carrier.center()
		var r := step_carrier.radius()
		var is_new := true
		for existing in carriers:
			if c.distance_to(existing["center"]) < 1.0 and absf(r - existing["radius"]) < 1.0:
				is_new = false
				break
		if is_new:
			carriers.append({"center": c, "radius": r})

	assert_eq(carriers.size(), 1,
		"All reflected arcs should share one carrier. Found %d distinct carriers: %s" %
		[carriers.size(), carriers])

# Phase 2: Evidence collection

func test_evidence_inner_hit_produces_regular_arc_steps() -> void:
	var d := _inner_trace()
	var path: TracePath = d["path"]

	var reflected_arc_steps: Array = []
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		if _reflected(s) and s.is_arc_step:
			reflected_arc_steps.append({"index": i, "start": s.start, "end": s.end, "via": s.via})

	assert_gt(reflected_arc_steps.size(), 0,
		"Should have arc steps in reflected frame")

	var bounds := Rect2(0, 0, 1920, 1080)
	var wrap_sub_steps := 0
	for arc in reflected_arc_steps:
		var s_pt: Vector2 = arc["start"]
		var e_pt: Vector2 = arc["end"]
		var on_bounds_start := (absf(s_pt.x - bounds.position.x) < 1.0 or
			absf(s_pt.x - bounds.end.x) < 1.0 or
			absf(s_pt.y - bounds.position.y) < 1.0 or
			absf(s_pt.y - bounds.end.y) < 1.0)
		var on_bounds_end := (absf(e_pt.x - bounds.position.x) < 1.0 or
			absf(e_pt.x - bounds.end.x) < 1.0 or
			absf(e_pt.y - bounds.position.y) < 1.0 or
			absf(e_pt.y - bounds.end.y) < 1.0)
		if on_bounds_start or on_bounds_end:
			wrap_sub_steps += 1

	gut.p("Reflected arc steps: %d, wrap sub-steps (bounds-clipped): %d" %
		[reflected_arc_steps.size(), wrap_sub_steps])
	assert_eq(wrap_sub_steps, 0,
		"Inner arcs should be regular steps, not bounds-clipped wrap sub-steps")

func test_evidence_arc_step_endpoints_are_on_consistent_carrier() -> void:
	var d := _inner_trace()
	var path: TracePath = d["path"]

	var ref_carrier: GeneralizedCircle = null
	var inconsistencies: Array[String] = []
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		if not _reflected(s) or not s.is_arc_step:
			continue
		if not VisualConverter.is_arc(s.start, s.via, s.end):
			continue
		var step_carrier := Segment.derive_carrier(s.start, s.end, s.via)
		if step_carrier.is_line():
			continue
		if ref_carrier == null:
			ref_carrier = step_carrier
			continue
		var rc := ref_carrier.center()
		var c := step_carrier.center()
		var dist := c.distance_to(rc)
		if dist > 1.0:
			inconsistencies.append(
				"step %d: carrier center=(%.1f,%.1f) vs ref center=(%.1f,%.1f) dist=%.1f" %
				[i, c.x, c.y, rc.x, rc.y, dist])

	assert_not_null(ref_carrier, "Should have at least one reflected arc step")
	assert_eq(inconsistencies.size(), 0,
		"All reflected arc steps should share the same carrier.\n%s" %
		"\n".join(inconsistencies))

func test_evidence_arc_step_via_is_on_its_own_carrier() -> void:
	var d := _inner_trace()
	var path: TracePath = d["path"]

	var off_carrier_vias: Array[String] = []
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		if not _reflected(s) or not s.is_arc_step:
			continue
		if not VisualConverter.is_arc(s.start, s.via, s.end):
			continue
		var step_carrier := Segment.derive_carrier(s.start, s.end, s.via)
		var eval_val := step_carrier.evaluate(s.via)
		var r := step_carrier.radius() if not step_carrier.is_line() else 1.0
		var normalized := absf(eval_val) / (r * r) if r > 0.0 else absf(eval_val)
		if normalized > 0.01:
			off_carrier_vias.append(
				"step %d: via=(%.1f,%.1f) eval=%.3f normalized=%.4f" %
				[i, s.via.x, s.via.y, eval_val, normalized])

	assert_eq(off_carrier_vias.size(), 0,
		"Arc step via points should lie on their own derived carrier.\n%s" %
		"\n".join(off_carrier_vias))

func test_evidence_all_arc_steps_share_single_carrier() -> void:
	var d := _inner_trace()
	var path: TracePath = d["path"]

	var distinct_carriers: Array = []
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		if not _reflected(s) or not s.is_arc_step:
			continue
		if not VisualConverter.is_arc(s.start, s.via, s.end):
			continue
		var step_carrier := Segment.derive_carrier(s.start, s.end, s.via)
		if step_carrier.is_line():
			continue
		var c := step_carrier.center()
		var r := step_carrier.radius()
		var is_new := true
		for existing in distinct_carriers:
			if c.distance_to(existing["center"]) < 1.0 and absf(r - existing["radius"]) < 1.0:
				is_new = false
				break
		if is_new:
			distinct_carriers.append({"center": c, "radius": r})

	assert_eq(distinct_carriers.size(), 1,
		"All arc steps should share one carrier. Found %d: %s" %
		[distinct_carriers.size(), distinct_carriers])
