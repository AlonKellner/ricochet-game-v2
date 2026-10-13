extends GutTest

func before_each() -> void:
	Surface.reset_id_counter()

func _build_portals_scene() -> Array:
	var surfs: Array = []
	surfs.append_array(RoomBuilder.create_room_surfaces(Rect2(100, 80, 1720, 920)))
	var m_seg := Segment.from_coords(Vector2(400, 200), Vector2(400, 700), Vector2(400, 450))
	var m_refl := ReflectionEffect.new(m_seg.get_carrier())
	surfs.append(Surface.new(m_seg, SideConfig.new(m_refl, true), SideConfig.new(null, false), false, false))
	var mr_seg := Segment.from_coords(Vector2(960, 150), Vector2(960, 400), Vector2(960, 275))
	var mr_refl := ReflectionEffect.new(mr_seg.get_carrier())
	surfs.append(Surface.new(mr_seg, SideConfig.new(null, false), SideConfig.new(mr_refl, true), false, false))
	var mb_seg := Segment.from_coords(Vector2(1400, 200), Vector2(1400, 500), Vector2(1400, 350))
	var mb_refl := ReflectionEffect.new(mb_seg.get_carrier())
	surfs.append(Surface.new(mb_seg, SideConfig.new(mb_refl, true), SideConfig.new(mb_refl, true), false, false))
	var inv_seg := Segment.from_coords(Vector2(700, 300), Vector2(700, 600), Vector2(820, 450))
	var inv_effect := CircleInversionEffect.new(inv_seg.get_carrier())
	var inv_surf := Surface.new(inv_seg, SideConfig.new(inv_effect, true), SideConfig.new(null, false), false, true)
	surfs.append(inv_surf)
	var arc_seg := Segment.from_coords(Vector2(1100, 700), Vector2(1300, 700), Vector2(1200, 580))
	var arc_refl := ReflectionEffect.new(arc_seg.get_carrier())
	surfs.append(Surface.new(arc_seg, SideConfig.new(arc_refl, true), SideConfig.new(arc_refl, true), false, false))
	var pl_seg := Segment.from_coords(Vector2(550, 600), Vector2(550, 900), Vector2(550, 750))
	var pl_result := RigidMotionEffect.create_portal_pair(pl_seg, 0.0, Vector2(900, 0))
	var pl_src_cfg := SideConfig.new(pl_result.source_effect, true)
	surfs.append(Surface.new(pl_seg, pl_src_cfg, pl_src_cfg, false, false))
	var pl_tgt_cfg := SideConfig.new(pl_result.target_effect, true)
	surfs.append(Surface.new(pl_result.target_segment, pl_tgt_cfg, pl_tgt_cfg, false, false))
	var pa_seg := Segment.from_coords(Vector2(1500, 550), Vector2(1500, 800), Vector2(1430, 675))
	var pa_result := RigidMotionEffect.create_portal_pair(pa_seg, 0.0, Vector2(-1100, 0))
	var pa_src_cfg := SideConfig.new(pa_result.source_effect, true)
	surfs.append(Surface.new(pa_seg, pa_src_cfg, pa_src_cfg, false, false))
	var pa_tgt_cfg := SideConfig.new(pa_result.target_effect, true)
	surfs.append(Surface.new(pa_result.target_segment, pa_tgt_cfg, pa_tgt_cfg, false, false))
	for def in [Vector4(0, 0, 1920, 0), Vector4(1920, 0, 1920, 1080),
				Vector4(1920, 1080, 0, 1080), Vector4(0, 1080, 0, 0)]:
		var sb_seg := Segment.from_coords(Vector2(def.x, def.y), Vector2(def.z, def.w),
			Vector2((def.x + def.z) / 2.0, (def.y + def.w) / 2.0))
		surfs.append(Surface.new(sb_seg, SideConfig.new(null, false), SideConfig.new(null, false), false, false))
	return surfs

# Beam steps carry no frame: "arc mode" (the frame maps lines to arcs) starts
# at the first step after an applied on-segment hit on a circle carrier
# (inversion or circle reflection; frame_id changes there).
func _arc_entry(path: TracePath) -> int:
	for i in range(1, path.steps.size()):
		var prev: TraceStep = path.steps[i - 1]
		var s: TraceStep = path.steps[i]
		if prev.hit != null and not prev.hit.segment.is_line() and prev.hit_on_segment \
				and s.frame_id != prev.frame_id:
			return i
	return -1

func _arc_radius(step: TraceStep) -> float:
	var seg := Segment.from_coords(step.start, step.end, step.via)
	return seg.get_carrier().radius()

# ==========================================================================
# Step 1: Reproduce — inversion then reflection is broken
# ==========================================================================

func test_inversion_then_reflection_visual_continuity() -> void:
	var surfs := _build_portals_scene()
	var player := Vector2(1024.95, 408.3824)
	var cursor := Vector2(1115.162, 526.976)
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, surfs, GameState.new())

	gut.p("=== Trace: %d steps ===" % path.steps.size())
	var arc_entry_idx := _arc_entry(path)
	var frame_change_idx := -1
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		gut.p("  [%d] %s -> %s fid=%d arc=%s" % [
			i, s.start, s.end, s.frame_id, s.is_arc_step])
		if arc_entry_idx >= 0 and i > arc_entry_idx and s.frame_id != path.steps[arc_entry_idx].frame_id:
			if frame_change_idx < 0:
				frame_change_idx = i

	assert_gt(arc_entry_idx, -1, "Ray should enter arc mode")
	assert_gt(frame_change_idx, -1, "Should find a frame change after arc entry")

	var pre: TraceStep = path.steps[frame_change_idx - 1]
	var post: TraceStep = path.steps[frame_change_idx]

	var gap: float = pre.end.distance_to(post.start)
	gut.p("Visual gap at frame change: %.4f px" % gap)
	assert_lt(gap, 2.0, "Visual continuity: end of pre-step must match start of post-step (gap=%.2f)" % gap)

	if pre.is_arc_step and post.is_arc_step:
		var r_pre := _arc_radius(pre)
		var r_post := _arc_radius(post)
		gut.p("Arc radius: pre=%.2f post=%.2f ratio=%.4f" % [r_pre, r_post, r_post / r_pre])

func test_inversion_then_reflection_radius_preserved() -> void:
	var surfs := _build_portals_scene()
	var player := Vector2(1024.95, 408.3824)
	var cursor := Vector2(1115.162, 526.976)
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, surfs, GameState.new())

	var arc_entry_idx := _arc_entry(path)

	assert_gt(arc_entry_idx, -1, "Ray should enter arc mode")

	var arc_entry_frame_id: int = path.steps[arc_entry_idx].frame_id
	var arc_steps_before: Array = []
	var arc_steps_after: Array = []
	var found_change := false
	for i in range(arc_entry_idx, path.steps.size()):
		var s: TraceStep = path.steps[i]
		if not s.is_arc_step:
			break
		if s.frame_id != arc_entry_frame_id and not found_change:
			found_change = true
		if not found_change:
			arc_steps_before.append(s)
		else:
			arc_steps_after.append(s)

	if arc_steps_before.is_empty() or arc_steps_after.is_empty():
		gut.p("No frame change within arc mode — cannot test radius preservation")
		pass_test("No frame change to test")
		return

	var r_before := _arc_radius(arc_steps_before[0])
	var r_after := _arc_radius(arc_steps_after[0])
	var ratio := r_after / r_before
	gut.p("Radius before frame change: %.2f" % r_before)
	gut.p("Radius after frame change: %.2f" % r_after)
	gut.p("Ratio: %.4f (should be ~1.0 for isometric transform)" % ratio)

	assert_almost_eq(r_after, r_before, r_before * 0.02,
		"Line reflection is isometric — arc radius must be preserved. Ratio=%.4f" % ratio)

func test_inversion_then_reflection_direction_reversal() -> void:
	var surfs := _build_portals_scene()
	var player := Vector2(1024.95, 408.3824)
	var cursor := Vector2(1115.162, 526.976)
	var aim := Direction.from_coords(player, cursor)
	var path := TestTrace.trace(player, aim, surfs, GameState.new())

	var arc_entry_idx := _arc_entry(path)
	var frame_change_idx := -1
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		if arc_entry_idx >= 0 and i > arc_entry_idx and s.frame_id != path.steps[arc_entry_idx].frame_id:
			if frame_change_idx < 0:
				frame_change_idx = i

	if frame_change_idx < 0:
		gut.p("No frame change in arc mode — skip direction test")
		pass_test("No frame change")
		return

	var pre: TraceStep = path.steps[frame_change_idx - 1]
	var post: TraceStep = path.steps[frame_change_idx]

	var pre_dir: Vector2 = (pre.end - pre.start).normalized()
	var post_dir: Vector2 = (post.end - post.start).normalized()
	var dot: float = pre_dir.dot(post_dir)
	gut.p("Pre-dir: %s  Post-dir: %s  dot: %.4f" % [pre_dir, post_dir, dot])

	assert_lt(dot, 0.5,
		"After reflection, visual direction should reverse (dot=%.4f, expected < 0.5)" % dot)
