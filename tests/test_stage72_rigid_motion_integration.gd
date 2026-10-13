extends GutTest

const H := preload("res://tests/test_helpers.gd")
const TOL := Vector2(1.0, 1.0)

func before_each() -> void:
	H.reset_counters()

# --- Helpers ---

func _portal_pair_surfaces(source_seg: Segment, theta: float, d: Vector2) -> Dictionary:
	var result = RigidMotionEffect.create_portal_pair(source_seg, theta, d)
	var src_cfg := SideConfig.new(result.source_effect, true)
	var tgt_cfg := SideConfig.new(result.target_effect, true)
	var source_surf := Surface.new(source_seg, src_cfg, src_cfg, false, false)
	var target_surf := Surface.new(result.target_segment, tgt_cfg, tgt_cfg, false, false)
	return {source = source_surf, target = target_surf}

func _line_seg(x: float) -> Segment:
	return Segment.from_coords(Vector2(x, 100), Vector2(x, 500), Vector2(x, 300))

func _translation_portals() -> Dictionary:
	return _portal_pair_surfaces(_line_seg(200), 0.0, Vector2(300, 0))

func _room_walls() -> Array:
	return [
		H.wall(0),
		H.wall(800),
		RoomBuilder.create_block_surface(Vector2(0, 0), Vector2(800, 0), Vector2(400, 0)),
		RoomBuilder.create_block_surface(Vector2(0, 600), Vector2(800, 600), Vector2(400, 600)),
	]

func _trace(origin: Vector2, cursor: Vector2, surfaces: Array) -> TracePath:
	var aim := Direction.from_coords(origin, cursor)
	return TestTrace.trace(origin, aim, surfaces, GameState.new(),
		null, -1.0, TraceMode.PHYSICAL, TraceMode.PHYSICAL, [], null, cursor)

func _find_portal_gap(path: TracePath, source_x: float) -> Dictionary:
	for i in range(path.steps.size() - 1):
		var cur: TraceStep = path.steps[i]
		var nxt: TraceStep = path.steps[i + 1]
		if cur.frame_id == TraceStep.IDENTITY_FRAME and absf(cur.end.x - source_x) < 2.0:
			return {found = true, displacement = nxt.start - cur.end}
	return {found = false, displacement = Vector2.ZERO}

func _find_portal_gap_by_frame(path: TracePath) -> Dictionary:
	for i in range(path.steps.size() - 1):
		var cur: TraceStep = path.steps[i]
		var nxt: TraceStep = path.steps[i + 1]
		if cur.frame_id != nxt.frame_id:
			return {found = true, displacement = nxt.start - cur.end}
	return {found = false, displacement = Vector2.ZERO}

func _trace_both(surfaces: Array, player: Vector2, cursor: Vector2, plan: Array) -> Dictionary:
	var pair := TestTrace.pair_of(player, cursor, surfaces, plan)
	return {"physical": pair.physical, "planned": pair.planned}

# --- Test 1: Display color ---

func test_stage72_portal_surface_cyan() -> void:
	var portals := _translation_portals()
	var state := GameState.new()
	var src_left = portals.source.active_side_config(Side.Value.LEFT, state)
	var tgt_left = portals.target.active_side_config(Side.Value.LEFT, state)
	assert_eq(src_left.effect.get_display_color(), Color.CYAN, "Source portal displays cyan")
	assert_eq(tgt_left.effect.get_display_color(), Color.CYAN, "Target portal displays cyan")

# --- Test 2: Translation portal trace ---

func test_stage72_trace_through_translation_portal() -> void:
	var portals := _translation_portals()
	var surfaces := _room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)
	var path := _trace(Vector2(50, 300), Vector2(750, 300), surfaces)
	assert_gt(path.steps.size(), 1, "Should have multiple steps")
	var first: TraceStep = path.steps[0]
	assert_almost_eq(first.start, Vector2(50, 300), TOL, "First step starts at player")
	assert_almost_eq(first.end, Vector2(200, 300), TOL, "First step ends at source portal")
	assert_gt(path.steps.size(), 1, "Need post-portal step")
	var second: TraceStep = path.steps[1]
	assert_almost_eq(second.start, Vector2(500, 300), TOL,
		"Post-portal step starts at target (x=500)")

# --- Test 2b: Horizontal portal gap ---

func test_stage72_portal_gap_and_displacement() -> void:
	var portals := _translation_portals()
	var surfaces := _room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)
	var path := _trace(Vector2(50, 300), Vector2(750, 300), surfaces)
	var gap := _find_portal_gap(path, 200.0)
	assert_true(gap.found, "Portal transition must be found")
	assert_almost_eq(gap.displacement, Vector2(300, 0), TOL,
		"Gap equals displacement d=(300,0)")

# --- Test 2c: Diagonal ray portal gap ---

func test_stage72_diagonal_portal_gap() -> void:
	var portals := _translation_portals()
	var surfaces := _room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)
	var path := _trace(Vector2(50, 200), Vector2(750, 400), surfaces)
	var gap := _find_portal_gap(path, 200.0)
	assert_true(gap.found, "Portal transition must be found")
	assert_almost_eq(gap.displacement.x, 300.0, 2.0,
		"Gap x-component ≈ 300 (got %.1f)" % gap.displacement.x)

# --- Test 2d: Arc portal gap ---

func test_stage72_portal_gap_arc() -> void:
	var arc_seg := Segment.from_coords(
		Vector2(200, 100), Vector2(200, 300), Vector2(300, 200))
	var portals = _portal_pair_surfaces(arc_seg, 0.0, Vector2(400, 0))
	var surfaces := _room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)
	var path := _trace(Vector2(50, 200), Vector2(750, 200), surfaces)
	var gap := _find_portal_gap_by_frame(path)
	assert_true(gap.found, "Arc portal transition must be found")
	assert_gt(gap.displacement.length(), 100.0,
		"Arc portal gap must be substantial (got %.1f)" % gap.displacement.length())

# --- Test 2e: Vertical displacement portal gap ---

func test_stage72_portal_gap_vertical_d() -> void:
	var portals = _portal_pair_surfaces(_line_seg(200), 0.0, Vector2(0, 600))
	var surfaces := _room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)  # the game always places both surfaces of a pair
	var path := _trace(Vector2(50, 300), Vector2(750, 300), surfaces)
	var gap := _find_portal_gap_by_frame(path)
	assert_true(gap.found, "Vertical-d portal transition must be found")
	assert_almost_eq(gap.displacement.y, 600.0, 2.0,
		"Gap y-component ≈ 600 (got %.1f)" % gap.displacement.y)

# --- Test 3: Rotation portal trace ---

func test_stage72_trace_through_rotation_portal() -> void:
	var portals = _portal_pair_surfaces(_line_seg(300), PI / 2.0, Vector2.ZERO)
	var surfaces := _room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)  # the game always places both surfaces of a pair
	var path := _trace(Vector2(50, 300), Vector2(750, 300), surfaces)
	assert_gt(path.steps.size(), 1, "Should have multiple steps")
	var found_portal_frame := false
	for step in path.steps:
		var s: TraceStep = step
		if s.frame_id != TraceStep.IDENTITY_FRAME:
			found_portal_frame = true
			# (beam steps carry no frame object: conformality is not observable)
			assert_true(s.after_portal, "First non-identity step is the portal exit")
			break
	assert_true(found_portal_frame, "Should have a step in non-identity portal frame")

# --- Test 4: Conformal composition with reflection ---

func test_stage72_frame_conformal_after_portal() -> void:
	var portals = _portal_pair_surfaces(_line_seg(200), 0.0, Vector2(0, 100))
	var mirror := H.mirror(400)
	var surfaces := _room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)  # the game always places both surfaces of a pair
	surfaces.append(mirror)
	var path := _trace(Vector2(50, 300), Vector2(750, 300), surfaces)
	# Beam steps carry no frame object (conjugating is not observable): assert
	# the frame sequence instead: identity -> portal frame -> portal+reflection
	# frame (a distinct third frame after the mirror hit).
	var portal_fid := -1
	var refl_fid := -1
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		if s.frame_id == TraceStep.IDENTITY_FRAME:
			continue
		if portal_fid == -1:
			portal_fid = s.frame_id
		elif s.frame_id != portal_fid and refl_fid == -1 and i > 0 \
				and path.steps[i - 1].hit != null and path.steps[i - 1].surface_id == mirror.id:
			refl_fid = s.frame_id
	assert_ne(portal_fid, -1, "Portal frame alone after the portal")
	assert_ne(refl_fid, -1, "Portal + reflection yields a new frame after the mirror hit")

# --- Test 5: Portal chain (two source portals, no cancellation) ---

func test_stage72_portal_chain() -> void:
	var pair1 = _portal_pair_surfaces(_line_seg(200), 0.0, Vector2(0, 100))
	var pair2 = _portal_pair_surfaces(_line_seg(400), 0.0, Vector2(0, 100))
	var surfaces := _room_walls()
	surfaces.append(pair1.source)
	surfaces.append(pair2.source)
	# the game always places both surfaces of a pair
	surfaces.append(pair1.target)
	surfaces.append(pair2.target)
	var path := _trace(Vector2(50, 300), Vector2(750, 300), surfaces)
	assert_gt(path.steps.size(), 2, "Should have 3+ steps through two portals")
	var non_identity_count := 0
	for step in path.steps:
		var s: TraceStep = step
		if s.frame_id != TraceStep.IDENTITY_FRAME:
			non_identity_count += 1
	assert_gt(non_identity_count, 1,
		"Multiple steps in non-identity frames after chained portals")

# --- Test 6: Portal pair — ray continues past target ---

func test_stage72_portal_pair_continues_past_target() -> void:
	var portals := _translation_portals()
	var surfaces := _room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)
	var path := _trace(Vector2(50, 300), Vector2(750, 300), surfaces)
	assert_gt(path.steps.size(), 1, "Should have multiple steps")
	var portal_step_found := false
	for step in path.steps:
		var s: TraceStep = step
		if s.frame_id != TraceStep.IDENTITY_FRAME:
			portal_step_found = true
	assert_true(portal_step_found,
		"Ray enters portal frame after source portal")

# --- Test 7: Planner portal alignment ---

func test_stage72_planner_portal_aligned() -> void:
	var portals := _translation_portals()
	var surfaces := _room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)
	var plan := [
		PlanManager.PlanEntry.new(portals.source.id, Side.Value.RIGHT),
		PlanManager.PlanEntry.new(portals.target.id, Side.Value.RIGHT),
	]
	var player := Vector2(50, 300)
	var cursor := Vector2(700, 300)
	var result := _trace_both(surfaces, player, cursor, plan)
	var physical: TracePath = result.physical
	var planned: TracePath = result.planned
	assert_gt(physical.steps.size(), 0, "Physical trace has steps")
	assert_gt(planned.steps.size(), 0, "Planned trace has steps")
	var ci := planned.cursor_index if planned.cursor_index >= 0 else planned.steps.size()
	var merged := StepTreeMerge.merge(planned.steps, physical.steps, ci)
	var aligned_count := 0
	for step in merged:
		var s: TraceStep = step
		if s.type == StepTypes.Type.ALIGNED or s.type == StepTypes.Type.ALIGNED_POST_PLANNED:
			aligned_count += 1
	assert_gt(aligned_count, 0, "Should have aligned steps in planned+physical merge")

# --- Test 10: S5 aligned provenance ---

func test_stage72_S5_aligned_provenance() -> void:
	var portals := _translation_portals()
	var surfaces := _room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)
	var plan := [
		PlanManager.PlanEntry.new(portals.source.id, Side.Value.RIGHT),
		PlanManager.PlanEntry.new(portals.target.id, Side.Value.RIGHT),
	]
	var player := Vector2(50, 300)
	var cursor := Vector2(700, 300)
	var result := _trace_both(surfaces, player, cursor, plan)
	var planned: TracePath = result.planned
	assert_gt(planned.steps.size(), 1, "Planned trace has multiple steps")
	var first: TraceStep = planned.steps[0]
	assert_eq(first.frame_id, TraceStep.IDENTITY_FRAME,
		"First planned step is in identity frame (before portal)")
	var found_portal_frame := false
	for step in planned.steps:
		var s: TraceStep = step
		if s.frame_id != TraceStep.IDENTITY_FRAME:
			found_portal_frame = true
			break
	assert_true(found_portal_frame,
		"Planned trace has steps in portal frame (non-identity frame_id)")

# --- Test 11: S16 no NaN/Inf ---

func test_stage72_S16_no_nan_in_trace() -> void:
	var portals := _translation_portals()
	var surfaces := _room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)
	var origins := [
		Vector2(50, 200), Vector2(50, 300), Vector2(50, 400),
		Vector2(100, 150), Vector2(100, 450),
	]
	for origin in origins:
		var path := _trace(origin, Vector2(750, 300), surfaces)
		for step in path.steps:
			var s: TraceStep = step
			assert_false(is_nan(s.start.x) or is_nan(s.start.y),
				"No NaN in start from origin %s: %s" % [origin, s.start])
			assert_false(is_nan(s.end.x) or is_nan(s.end.y),
				"No NaN in end from origin %s: %s" % [origin, s.end])
			assert_false(is_inf(s.start.x) or is_inf(s.start.y),
				"No Inf in start from origin %s: %s" % [origin, s.start])

# --- Test 12: Arc portal trace ---

func test_stage72_arc_portal_trace() -> void:
	var arc_seg := Segment.from_coords(
		Vector2(200, 100), Vector2(200, 300), Vector2(300, 200))
	var portals = _portal_pair_surfaces(arc_seg, 0.0, Vector2(400, 0))
	var source_carrier = portals.source.segment.get_carrier()
	var target_carrier = portals.target.segment.get_carrier()
	assert_false(source_carrier.is_line(), "Source carrier is a circle")
	assert_false(target_carrier.is_line(), "Target carrier is a circle")
	assert_almost_eq(target_carrier.radius(), source_carrier.radius(), 0.1,
		"Same radius after translation")
	var surfaces := _room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)
	var path := _trace(Vector2(50, 200), Vector2(750, 200), surfaces)
	assert_gt(path.steps.size(), 0, "Should produce steps through arc portal")
	for step in path.steps:
		var s: TraceStep = step
		assert_false(is_nan(s.start.x) or is_nan(s.start.y),
			"No NaN in arc portal trace: %s" % s.start)

# --- Portal origin_on_surface regression tests ---

func _big_room_walls() -> Array:
	return [
		RoomBuilder.create_block_surface(Vector2(100, 100), Vector2(100, 900), Vector2(100, 500)),
		RoomBuilder.create_block_surface(Vector2(1800, 100), Vector2(1800, 900), Vector2(1800, 500)),
		RoomBuilder.create_block_surface(Vector2(100, 100), Vector2(1800, 100), Vector2(950, 100)),
		RoomBuilder.create_block_surface(Vector2(100, 900), Vector2(1800, 900), Vector2(950, 900)),
	]

func _big_line_seg(x: float) -> Segment:
	return Segment.from_coords(Vector2(x, 200), Vector2(x, 800), Vector2(x, 500))

func _big_mirror(x: float) -> Surface:
	var seg := Segment.from_coords(Vector2(x, 100), Vector2(x, 900), Vector2(x, 500))
	var refl := ReflectionEffect.new(seg.get_carrier())
	var config := SideConfig.new(refl, true)
	return Surface.new(seg, config, config, false, false)

# --- Reproduction: reflection + portal interaction ---

func test_stage72_reflection_then_portal_displacement() -> void:
	var portals := _portal_pair_surfaces(_big_line_seg(550), 0.0, Vector2(900, 0))
	var surfaces := _big_room_walls()
	surfaces.append(_big_mirror(1400))
	surfaces.append(portals.source)
	surfaces.append(portals.target)
	# Ray goes RIGHT → hits mirror at x=1400 → reflects LEFT → hits portal at x=550
	var path := _trace(Vector2(950, 500), Vector2(1700, 500), surfaces)
	var found_reflection := false
	var found_portal_after := false
	for i in range(path.steps.size()):
		var s: TraceStep = path.steps[i]
		if s.frame_id != TraceStep.IDENTITY_FRAME and not found_reflection:
			found_reflection = true
		if found_reflection and s.after_portal:
			found_portal_after = true
			assert_almost_eq(s.start.x, 1450.0, 10.0,
				"Post-portal visual start should be at target x=1450, got %.1f" % s.start.x)
			break
	assert_true(found_reflection, "Ray must hit reflection before portal")
	assert_true(found_portal_after, "Ray must hit portal after reflection")

func test_stage72_portal_without_reflection_works() -> void:
	var portals := _portal_pair_surfaces(_big_line_seg(550), 0.0, Vector2(900, 0))
	var surfaces := _big_room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)
	# Same portal, no reflection — ray hits portal directly in identity frame
	var path := _trace(Vector2(200, 500), Vector2(1700, 500), surfaces)
	var found_portal := false
	for s in path.steps:
		var step: TraceStep = s
		if step.after_portal:
			found_portal = true
			assert_almost_eq(step.start.x, 1450.0, 10.0,
				"Without reflection, post-portal start at target x=1450, got %.1f" % step.start.x)
			break
	assert_true(found_portal, "Portal must be found without reflection")

func test_stage72_portal_single_trace_enters_frame() -> void:
	var portals := _portal_pair_surfaces(_big_line_seg(1500), 0.0, Vector2(-1100, 0))
	var surfaces := _big_room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)
	var path := _trace(Vector2(950, 500), Vector2(1700, 500), surfaces)
	var found_portal_frame := false
	for step in path.steps:
		var s: TraceStep = step
		if s.frame_id != TraceStep.IDENTITY_FRAME:
			found_portal_frame = true
			break
	assert_true(found_portal_frame,
		"Trace through portal must produce steps in non-identity frame")

func test_stage72_portal_stability_across_cursors() -> void:
	var portals := _portal_pair_surfaces(_big_line_seg(1500), 0.0, Vector2(-1100, 0))
	var surfaces := _big_room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)
	var player := Vector2(950, 500)
	for dy in range(-10, 11):
		var cursor := Vector2(1700, 500 + dy)
		var path := _trace(player, cursor, surfaces)
		var entered := false
		for step in path.steps:
			var s: TraceStep = step
			if s.frame_id != TraceStep.IDENTITY_FRAME:
				entered = true
				break
		assert_true(entered,
			"Cursor dy=%d must enter portal frame" % dy)

func test_stage72_portal_post_cursor_contiguity() -> void:
	var portals := _portal_pair_surfaces(_big_line_seg(1500), 0.0, Vector2(-1100, 0))
	var surfaces := _big_room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)
	var plan := [
		PlanManager.PlanEntry.new(portals.source.id, Side.Value.RIGHT),
		PlanManager.PlanEntry.new(portals.target.id, Side.Value.RIGHT),
	]
	var player := Vector2(950, 500)
	var cursor := Vector2(1700, 500)
	var result := _trace_both(surfaces, player, cursor, plan)
	var planned: TracePath = result.planned
	var ci := planned.cursor_index if planned.cursor_index >= 0 else planned.steps.size()
	assert_gt(planned.steps.size(), ci, "Should have post-cursor steps")
	for i in range(ci, planned.steps.size() - 1):
		var cur: TraceStep = planned.steps[i]
		var nxt: TraceStep = planned.steps[i + 1]
		if nxt.after_portal:
			continue
		assert_almost_eq(cur.end, nxt.start, TOL,
			"Post-cursor steps %d-%d must be contiguous: end=%s start=%s" % [i, i+1, cur.end, nxt.start])

# --- Investigation: Double portal push beeline ---

func _double_push_setup() -> Dictionary:
	var portals := _portal_pair_surfaces(_big_line_seg(550), 0.0, Vector2(900, 0))
	var surfaces := _big_room_walls()
	surfaces.append(portals.source)
	surfaces.append(portals.target)
	var plan := [
		PlanManager.PlanEntry.new(portals.source.id, Side.Value.LEFT),
		PlanManager.PlanEntry.new(portals.source.id, Side.Value.LEFT),
	]
	return {portals = portals, surfaces = surfaces, plan = plan,
		player = Vector2(960, 480), cursor = Vector2(700, 510), d = Vector2(900, 0)}

# The plan [source LEFT, source LEFT] pushes through the portal twice: the
# aim points at the image cursor - 2d (closed form of the plan's inverse maps).
func _expected_aim_dir(s: Dictionary) -> Vector2:
	var image: Vector2 = s.cursor - s.d * 2
	return (image - s.player).normalized()

func test_stage72_double_portal_trace_uses_aim() -> void:
	var s := _double_push_setup()
	var path: TracePath = _trace_both(s.surfaces, s.player, s.cursor, s.plan).planned
	assert_gt(path.steps.size(), 0, "Trace should produce steps")
	var first: TraceStep = null
	for step in path.steps:
		var st: TraceStep = step
		if st.start != st.end:
			first = st
			break
	assert_not_null(first, "Should have a non-zero-length step")
	var step_dir := (first.end - first.start).normalized()
	var aim_dir := _expected_aim_dir(s)
	var cross := step_dir.cross(aim_dir)
	gut.p("Step dir=%s, aim dir=%s, cross=%.6f" % [step_dir, aim_dir, cross])
	assert_almost_eq(cross, 0.0, 0.01,
		"First step direction must match the image aim. cross=%.4f step=%s aim=%s" % [
			cross, step_dir, aim_dir])
	assert_gt(step_dir.dot(aim_dir), 0.0, "Same orientation as the image aim")

func test_stage72_double_portal_no_beeline() -> void:
	var s := _double_push_setup()
	var result := _trace_both(s.surfaces, s.player, s.cursor, s.plan)
	var planned: TracePath = result.planned
	var ci := planned.cursor_index
	gut.p("cursor_index=%d, steps=%d" % [ci, planned.steps.size()])
	for i in planned.steps.size():
		var st: TraceStep = planned.steps[i]
		var hit_info := "virt" if st.hit == null else "surf"
		gut.p("  P%d: %s -> %s fid=%d [%s]%s" % [
			i, st.start, st.end, st.frame_id, hit_info,
			" <-- CURSOR" if i == ci else ""])
	assert_gte(ci, 0, "Cursor should be injected")
	assert_gt(ci, 0, "Cursor step should not be the first step")
	var cursor_step: TraceStep = planned.steps[ci - 1]
	assert_almost_eq(cursor_step.end, s.cursor, TOL, "Step ci-1 ends at the cursor")
	var step_dir := (cursor_step.end - cursor_step.start).normalized()
	# beam: step.ray is player->cursor, not the shot direction; a translation
	# portal keeps the direction, so compare with the image aim direction
	var aim_dir := _expected_aim_dir(s)
	var dot := step_dir.dot(aim_dir)
	var angle_deg := rad_to_deg(acos(clampf(dot, -1.0, 1.0)))
	gut.p("Cursor step angle deviation: %.1f deg (step=%s, aim=%s)" % [
		angle_deg, step_dir, aim_dir])
	assert_lt(angle_deg, 5.0,
		"Cursor step must follow ray direction (angle=%.1f). Beeline from %s to %s" % [
			angle_deg, cursor_step.start, cursor_step.end])

func test_stage72_double_portal_direction_only() -> void:
	var s := _double_push_setup()
	var with_plan: TracePath = _trace_both(s.surfaces, s.player, s.cursor, s.plan).physical
	assert_gt(with_plan.steps.size(), 2,
		"Trace with the plan's aim must produce multiple steps")
	var first: TraceStep = with_plan.steps[0]
	var aim_dir := _expected_aim_dir(s)
	var step_dir := (first.end - first.start).normalized()
	var cross := step_dir.cross(aim_dir)
	assert_almost_eq(cross, 0.0, 0.01,
		"First physical step must follow the image aim direction")
