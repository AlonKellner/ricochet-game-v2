extends GutTest

func before_each() -> void:
	Surface.reset_id_counter()

func _build_portals_scene() -> Array:
	var surfs: Array = []
	surfs.append_array(RoomBuilder.create_room_surfaces(Rect2(100, 80, 1720, 920)))
	# mirror_lines: L=reflect
	var m_seg := Segment.from_coords(Vector2(400, 200), Vector2(400, 700), Vector2(400, 450))
	var m_refl := ReflectionEffect.new(m_seg.get_carrier())
	surfs.append(Surface.new(m_seg, SideConfig.new(m_refl, true), SideConfig.new(null, false), false, false))
	# mirror_right_lines: R=reflect
	var mr_seg := Segment.from_coords(Vector2(960, 150), Vector2(960, 400), Vector2(960, 275))
	var mr_refl := ReflectionEffect.new(mr_seg.get_carrier())
	surfs.append(Surface.new(mr_seg, SideConfig.new(null, false), SideConfig.new(mr_refl, true), false, false))
	# mirror_both_lines: L=reflect R=reflect
	var mb_seg := Segment.from_coords(Vector2(1400, 200), Vector2(1400, 500), Vector2(1400, 350))
	var mb_refl := ReflectionEffect.new(mb_seg.get_carrier())
	surfs.append(Surface.new(mb_seg, SideConfig.new(mb_refl, true), SideConfig.new(mb_refl, true), false, false))
	# inversion_left_arcs: L=inversion
	var inv_seg := Segment.from_coords(Vector2(700, 300), Vector2(700, 600), Vector2(820, 450))
	var inv_effect := CircleInversionEffect.new(inv_seg.get_carrier())
	surfs.append(Surface.new(inv_seg, SideConfig.new(inv_effect, true), SideConfig.new(null, false), false, true))
	# reflective_arcs: L=reflect R=reflect (THE ARC UNDER TEST)
	var arc_seg := Segment.from_coords(Vector2(1100, 700), Vector2(1300, 700), Vector2(1200, 580))
	var arc_refl := ReflectionEffect.new(arc_seg.get_carrier())
	var arc_surf := Surface.new(arc_seg, SideConfig.new(arc_refl, true), SideConfig.new(arc_refl, true), false, false)
	surfs.append(arc_surf)
	# portal_lines: (550,600)→(550,900), rotation=0, translation=(900,0)
	var pl_seg := Segment.from_coords(Vector2(550, 600), Vector2(550, 900), Vector2(550, 750))
	var pl_result := RigidMotionEffect.create_portal_pair(pl_seg, 0.0, Vector2(900, 0))
	var pl_src_cfg := SideConfig.new(pl_result.source_effect, true)
	surfs.append(Surface.new(pl_seg, pl_src_cfg, pl_src_cfg, false, false))
	var pl_tgt_cfg := SideConfig.new(pl_result.target_effect, true)
	surfs.append(Surface.new(pl_result.target_segment, pl_tgt_cfg, pl_tgt_cfg, false, false))
	# portal_arcs: (1500,550)→(1500,800) via (1430,675), rotation=0, translation=(-1100,0)
	var pa_seg := Segment.from_coords(Vector2(1500, 550), Vector2(1500, 800), Vector2(1430, 675))
	var pa_result := RigidMotionEffect.create_portal_pair(pa_seg, 0.0, Vector2(-1100, 0))
	var pa_src_cfg := SideConfig.new(pa_result.source_effect, true)
	surfs.append(Surface.new(pa_seg, pa_src_cfg, pa_src_cfg, false, false))
	var pa_tgt_cfg := SideConfig.new(pa_result.target_effect, true)
	surfs.append(Surface.new(pa_result.target_segment, pa_tgt_cfg, pa_tgt_cfg, false, false))
	# screen_bounds (passthrough)
	for def in [Vector4(0, 0, 1920, 0), Vector4(1920, 0, 1920, 1080),
				Vector4(1920, 1080, 0, 1080), Vector4(0, 1080, 0, 0)]:
		var sb_seg := Segment.from_coords(Vector2(def.x, def.y), Vector2(def.z, def.w),
			Vector2((def.x + def.z) / 2.0, (def.y + def.w) / 2.0))
		surfs.append(Surface.new(sb_seg, SideConfig.new(null, false), SideConfig.new(null, false), false, false))
	return surfs

func _count_zero_length_steps(path: TracePath) -> int:
	var count := 0
	for s in path.steps:
		var step: TraceStep = s
		if step.start.distance_to(step.end) < 0.01:
			count += 1
	return count

# === Regression tests for norm_cache mapping corruption fix ===

func test_step1_physical_no_oscillation() -> void:
	var surfs := _build_portals_scene()
	var player := Vector2(970, 703)
	var cursor := Vector2(1027, 675)
	var path: TracePath = TestTrace.pair_of(player, cursor, surfs).physical
	assert_gt(path.steps.size(), 0, "Physical trace has steps")
	var zero_count := _count_zero_length_steps(path)
	assert_eq(zero_count, 0, "Physical trace should have no zero-length steps")

func test_step1_planned_oscillation() -> void:
	var surfs := _build_portals_scene()
	var player := Vector2(970, 703)
	var cursor := Vector2(1027, 675)
	var planned: TracePath = TestTrace.pair_of(player, cursor, surfs).planned
	assert_gt(planned.steps.size(), 0, "Planned trace has steps")
	var zero_count := _count_zero_length_steps(planned)
	assert_eq(zero_count, 0,
		"Planned trace should have no zero-length steps (got %d)" % zero_count)
