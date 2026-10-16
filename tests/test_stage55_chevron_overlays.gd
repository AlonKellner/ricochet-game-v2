extends GutTest

const H := preload("res://tests/test_helpers.gd")
const PathRendererScript = preload("res://scripts/visual/path_renderer.gd")

func before_each() -> void:
	H.reset_counters()

# ============================================================
# Phase 1: SideLink
# ============================================================

func test_side_link_from_self() -> void:
	var seg := Segment.from_coords(Vector2(400, 0), Vector2(400, 600), Vector2(400, 300))
	var refl := ReflectionEffect.new(seg.get_carrier())
	var surf := Surface.new(seg, SideConfig.new(refl, true), SideConfig.new(null, false))
	var link := SideLink.from_self(surf, Side.Value.LEFT)
	assert_eq(link.outgoing, link, "from_self: outgoing should reference itself")

func test_side_link_from_pair() -> void:
	var seg_a := Segment.from_coords(Vector2(100, 0), Vector2(100, 600), Vector2(100, 300))
	var seg_b := Segment.from_coords(Vector2(500, 0), Vector2(500, 600), Vector2(500, 300))
	var surf_a := Surface.new(seg_a, SideConfig.new(null, false), SideConfig.new(null, false))
	var surf_b := Surface.new(seg_b, SideConfig.new(null, false), SideConfig.new(null, false))
	var link_a := SideLink.from_pair(surf_a, Side.Value.LEFT, surf_b, Side.Value.RIGHT)
	var link_b := link_a.outgoing
	assert_ne(link_a, link_b, "from_pair: a and b should be distinct")
	assert_eq(link_a.outgoing, link_b, "from_pair: a.outgoing == b")
	assert_eq(link_b.outgoing, link_a, "from_pair: b.outgoing == a")

func test_side_link_from_pair_same_surface() -> void:
	var seg := Segment.from_coords(Vector2(300, 0), Vector2(300, 600), Vector2(300, 300))
	var surf := Surface.new(seg, SideConfig.new(null, false), SideConfig.new(null, false))
	var link_a := SideLink.from_pair(surf, Side.Value.LEFT, surf, Side.Value.RIGHT)
	var link_b := link_a.outgoing
	assert_eq(link_a.surface, surf, "Same surface for both links")
	assert_eq(link_b.surface, surf, "Same surface for both links")
	assert_eq(link_a.side, Side.Value.LEFT, "Link a is LEFT")
	assert_eq(link_b.side, Side.Value.RIGHT, "Link b is RIGHT")

func test_side_link_surface_and_side() -> void:
	var seg := Segment.from_coords(Vector2(200, 0), Vector2(200, 600), Vector2(200, 300))
	var refl := ReflectionEffect.new(seg.get_carrier())
	var surf := Surface.new(seg, SideConfig.new(refl, true), SideConfig.new(null, false))
	var link := SideLink.from_self(surf, Side.Value.RIGHT)
	assert_eq(link.surface, surf, "Link stores correct surface")
	assert_eq(link.side, Side.Value.RIGHT, "Link stores correct side")

# ============================================================
# Phase 2: Surface auto-linking
# ============================================================

func test_reflection_surface_auto_links() -> void:
	var seg := Segment.from_coords(Vector2(400, 0), Vector2(400, 600), Vector2(400, 300))
	var refl := ReflectionEffect.new(seg.get_carrier())
	var surf := Surface.new(seg, SideConfig.new(refl, true), SideConfig.new(null, false))
	var link := surf.get_side_link(Side.Value.LEFT)
	assert_not_null(link, "Reflection side should have auto-link")
	assert_eq(link.outgoing, link, "Reflection link should be self-referential")
	assert_eq(link.surface, surf, "Link references own surface")
	assert_eq(link.side, Side.Value.LEFT, "Link references LEFT side")

func test_reflection_both_sides_auto_link() -> void:
	var seg := Segment.from_coords(Vector2(400, 0), Vector2(400, 600), Vector2(400, 300))
	var refl := ReflectionEffect.new(seg.get_carrier())
	var surf := Surface.new(seg, SideConfig.new(refl, true), SideConfig.new(refl, true))
	var left := surf.get_side_link(Side.Value.LEFT)
	var right := surf.get_side_link(Side.Value.RIGHT)
	assert_not_null(left, "LEFT should have auto-link")
	assert_not_null(right, "RIGHT should have auto-link")
	assert_eq(left.outgoing, left, "LEFT self-referential")
	assert_eq(right.outgoing, right, "RIGHT self-referential")
	assert_ne(left, right, "LEFT and RIGHT are distinct links")

func test_inversion_surface_auto_links() -> void:
	var seg := Segment.from_coords(Vector2(460, 300), Vector2(540, 300), Vector2(500, 260))
	var inv := CircleInversionEffect.new(seg.get_carrier())
	var surf := Surface.new(seg, SideConfig.new(inv, true), SideConfig.new(null, false))
	var link := surf.get_side_link(Side.Value.LEFT)
	assert_not_null(link, "Inversion side should have auto-link")
	assert_eq(link.outgoing, link, "Inversion link should be self-referential")

func test_non_interactive_side_no_link() -> void:
	var seg := Segment.from_coords(Vector2(400, 0), Vector2(400, 600), Vector2(400, 300))
	var refl := ReflectionEffect.new(seg.get_carrier())
	var surf := Surface.new(seg, SideConfig.new(refl, true), SideConfig.new(null, false))
	var link := surf.get_side_link(Side.Value.RIGHT)
	assert_null(link, "Non-interactive side should have no link")

func test_terminal_side_no_link() -> void:
	var seg := Segment.from_coords(Vector2(400, 0), Vector2(400, 600), Vector2(400, 300))
	var terminal := TerminalEffect.new()
	var surf := Surface.new(seg, SideConfig.new(terminal), SideConfig.new(terminal))
	assert_null(surf.get_side_link(Side.Value.LEFT), "Terminal LEFT has no link")
	assert_null(surf.get_side_link(Side.Value.RIGHT), "Terminal RIGHT has no link")

func test_portal_not_auto_linked() -> void:
	var seg := Segment.from_coords(Vector2(200, 200), Vector2(200, 400), Vector2(200, 300))
	var result := RigidMotionEffect.create_portal_pair(seg, 0.0, Vector2(300, 0))
	var cfg := SideConfig.new(result.source_effect, true)
	var surf := Surface.new(seg, cfg, cfg)
	assert_null(surf.get_side_link(Side.Value.LEFT), "Portal LEFT not auto-linked")
	assert_null(surf.get_side_link(Side.Value.RIGHT), "Portal RIGHT not auto-linked")

func test_portal_explicit_pair_linking() -> void:
	var seg_a := Segment.from_coords(Vector2(200, 200), Vector2(200, 400), Vector2(200, 300))
	var result := RigidMotionEffect.create_portal_pair(seg_a, 0.0, Vector2(300, 0))
	var cfg_a := SideConfig.new(result.source_effect, true)
	var surf_a := Surface.new(seg_a, cfg_a, cfg_a)
	var cfg_b := SideConfig.new(result.target_effect, true)
	var surf_b := Surface.new(result.target_segment, cfg_b, cfg_b)

	var link_a := SideLink.from_pair(surf_a, Side.Value.LEFT, surf_b, Side.Value.RIGHT)
	surf_a.set_side_link(Side.Value.LEFT, link_a)
	surf_b.set_side_link(Side.Value.RIGHT, link_a.outgoing)

	var left_a := surf_a.get_side_link(Side.Value.LEFT)
	assert_not_null(left_a, "Source LEFT should be linked")
	assert_eq(left_a.outgoing.surface, surf_b, "Source LEFT outgoing goes to target")
	assert_eq(left_a.outgoing.side, Side.Value.RIGHT, "Non-conjugating: LEFT -> RIGHT")

	var right_b := surf_b.get_side_link(Side.Value.RIGHT)
	assert_not_null(right_b, "Target RIGHT should be linked")
	assert_eq(right_b.outgoing.surface, surf_a, "Target RIGHT outgoing goes back to source")
	assert_eq(right_b.outgoing.side, Side.Value.LEFT, "Non-conjugating: RIGHT -> LEFT")

# ============================================================
# Phase 3: Physical hits has_continuation
# ============================================================

func test_physical_hits_has_continuation_mid_trace() -> void:
	var path := TracePath.new()
	var s1 := TraceStep.make()
	s1.surface_id = 1
	s1.hit_side = Side.Value.LEFT
	s1.hit_on_segment = true
	var s2 := TraceStep.make()
	s2.surface_id = 2
	s2.hit_side = Side.Value.RIGHT
	s2.hit_on_segment = true
	path.steps.append(s1)
	path.steps.append(s2)
	var hits := PathRendererScript.build_physical_hits(path)
	assert_true(hits[1][0].has_continuation, "Mid-trace step should have continuation")

func test_physical_hits_has_continuation_last_step() -> void:
	var path := TracePath.new()
	var s1 := TraceStep.make()
	s1.surface_id = 1
	s1.hit_side = Side.Value.LEFT
	s1.hit_on_segment = true
	var s2 := TraceStep.make()
	s2.surface_id = 2
	s2.hit_side = Side.Value.RIGHT
	s2.hit_on_segment = true
	path.steps.append(s1)
	path.steps.append(s2)
	var hits := PathRendererScript.build_physical_hits(path)
	assert_false(hits[2][0].has_continuation, "Last step should not have continuation")

func test_physical_hits_backward_compat() -> void:
	var path := TracePath.new()
	var step := TraceStep.make()
	step.surface_id = 5
	step.hit_side = Side.Value.LEFT
	step.hit_on_segment = true
	path.steps.append(step)
	var hits := PathRendererScript.build_physical_hits(path)
	var entry: Dictionary = hits[5][0]
	assert_eq(entry.side, Side.Value.LEFT, "Still has side field")
	assert_eq(entry.on_segment, true, "Still has on_segment field")
	assert_true(entry.has("has_continuation"), "New field exists")

# ============================================================
# Phase 4: Hover overlays (via ChevronOverlayBuilder)
# ============================================================

func _mirror_surf(x: float) -> Surface:
	return H.mirror(x)

func test_hover_reflection_both_same_side() -> void:
	var surf := _mirror_surf(400)
	var overlays := ChevronOverlayBuilder.build_hover_overlays(surf, Side.Value.LEFT)
	assert_true(overlays.has(surf.id), "Should have overlay for this surface")
	var list: Array = overlays[surf.id]
	assert_eq(list.size(), 1, "One overlay for reflection")
	var ov: ChevronOverlay = list[0]
	assert_true(ov.has_incoming, "Reflection has incoming")
	assert_true(ov.has_outgoing, "Reflection has outgoing")
	assert_eq(ov.side, Side.Value.LEFT, "Incoming on LEFT")
	assert_eq(ov.outgoing_side, Side.Value.LEFT, "Outgoing also on LEFT (conjugating)")

func test_hover_inversion_both_same_side() -> void:
	var seg := Segment.from_coords(Vector2(460, 300), Vector2(540, 300), Vector2(500, 260))
	var inv := CircleInversionEffect.new(seg.get_carrier())
	var surf := Surface.new(seg, SideConfig.new(inv, true), SideConfig.new(null, false))
	var overlays := ChevronOverlayBuilder.build_hover_overlays(surf, Side.Value.LEFT)
	var list: Array = overlays[surf.id]
	assert_eq(list.size(), 1, "One overlay for inversion")
	assert_true(list[0].has_incoming, "Inversion has incoming")
	assert_true(list[0].has_outgoing, "Inversion has outgoing")
	assert_eq(list[0].outgoing_side, Side.Value.LEFT, "Same side (conjugating)")

func test_hover_terminal_incoming_only() -> void:
	var seg := Segment.from_coords(Vector2(400, 0), Vector2(400, 600), Vector2(400, 300))
	var terminal := TerminalEffect.new()
	var left_cfg := SideConfig.new(terminal, true)
	var right_cfg := SideConfig.new(terminal, true)
	var surf := Surface.new(seg, left_cfg, right_cfg)
	var overlays := ChevronOverlayBuilder.build_hover_overlays(surf, Side.Value.LEFT)
	# every interactive side is highlighted; a block has no way out
	var list: Array = overlays.get(surf.id, [])
	assert_eq(list.size(), 1, "an interactive terminal side is highlighted")
	if list.size() == 1:
		assert_true(list[0].has_incoming)
		assert_false(list[0].has_outgoing, "the arrow stops: no outgoing chevrons")

func test_hover_portal_entry_incoming_only() -> void:
	var seg_a := Segment.from_coords(Vector2(200, 200), Vector2(200, 400), Vector2(200, 300))
	var result := RigidMotionEffect.create_portal_pair(seg_a, 0.0, Vector2(300, 0))
	var cfg_a := SideConfig.new(result.source_effect, true)
	var surf_a := Surface.new(seg_a, cfg_a, cfg_a)
	var cfg_b := SideConfig.new(result.target_effect, true)
	var surf_b := Surface.new(result.target_segment, cfg_b, cfg_b)
	var link := SideLink.from_pair(surf_a, Side.Value.LEFT, surf_b, Side.Value.RIGHT)
	surf_a.set_side_link(Side.Value.LEFT, link)
	surf_b.set_side_link(Side.Value.RIGHT, link.outgoing)

	var overlays := ChevronOverlayBuilder.build_hover_overlays(surf_a, Side.Value.LEFT)
	var entry_list: Array = overlays.get(surf_a.id, [])
	assert_eq(entry_list.size(), 1, "Entry surface has one overlay")
	assert_true(entry_list[0].has_incoming, "Entry has incoming")
	assert_false(entry_list[0].has_outgoing, "Entry has no outgoing (goes to partner)")

func test_hover_portal_partner_outgoing_only() -> void:
	var seg_a := Segment.from_coords(Vector2(200, 200), Vector2(200, 400), Vector2(200, 300))
	var result := RigidMotionEffect.create_portal_pair(seg_a, 0.0, Vector2(300, 0))
	var cfg_a := SideConfig.new(result.source_effect, true)
	var surf_a := Surface.new(seg_a, cfg_a, cfg_a)
	var cfg_b := SideConfig.new(result.target_effect, true)
	var surf_b := Surface.new(result.target_segment, cfg_b, cfg_b)
	var link := SideLink.from_pair(surf_a, Side.Value.LEFT, surf_b, Side.Value.RIGHT)
	surf_a.set_side_link(Side.Value.LEFT, link)
	surf_b.set_side_link(Side.Value.RIGHT, link.outgoing)

	var overlays := ChevronOverlayBuilder.build_hover_overlays(surf_a, Side.Value.LEFT)
	var partner_list: Array = overlays.get(surf_b.id, [])
	assert_eq(partner_list.size(), 1, "Partner has one overlay")
	assert_false(partner_list[0].has_incoming, "Partner has no incoming")
	assert_true(partner_list[0].has_outgoing, "Partner has outgoing")
	assert_eq(partner_list[0].outgoing_side, Side.Value.RIGHT, "Outgoing on RIGHT (non-conjugating)")

func test_hover_colors_are_hover_color() -> void:
	var surf := _mirror_surf(400)
	var overlays := ChevronOverlayBuilder.build_hover_overlays(surf, Side.Value.LEFT)
	var ov: ChevronOverlay = overlays[surf.id][0]
	assert_eq(ov.incoming_color, ChevronOverlayBuilder.HOVER_COLOR, "Incoming is HOVER_COLOR")
	assert_eq(ov.outgoing_color, ChevronOverlayBuilder.HOVER_COLOR, "Outgoing is HOVER_COLOR")
	assert_eq(ov.gradient_color, ChevronOverlayBuilder.HOVER_COLOR, "Gradient is HOVER_COLOR")

# ============================================================
# Phase 5: Plan overlays (via ChevronOverlayBuilder)
# ============================================================

func _surface_lookup(surfaces: Array) -> Callable:
	return func(id: int) -> Surface:
		for s in surfaces:
			if s.id == id:
				return s
		return null

func test_plan_reflection_valid_both() -> void:
	var surf := _mirror_surf(400)
	var plan := PlanManager.new()
	plan.add_entry(surf.id, Side.Value.LEFT)
	var phits := {surf.id: [{"side": Side.Value.LEFT, "on_segment": true, "has_continuation": true}]}
	var overlays := ChevronOverlayBuilder.build_plan_overlays(plan, phits, _surface_lookup([surf]))
	var ov: ChevronOverlay = overlays[surf.id][0]
	assert_eq(ov.incoming_color, ChevronOverlayBuilder.PLAN_VALID_COLOR, "Incoming valid")
	assert_eq(ov.outgoing_color, ChevronOverlayBuilder.PLAN_VALID_COLOR, "Outgoing valid")

func test_plan_reflection_valid_incoming_invalid_outgoing() -> void:
	var surf := _mirror_surf(400)
	var plan := PlanManager.new()
	plan.add_entry(surf.id, Side.Value.LEFT)
	var phits := {surf.id: [{"side": Side.Value.LEFT, "on_segment": true, "has_continuation": false}]}
	var overlays := ChevronOverlayBuilder.build_plan_overlays(plan, phits, _surface_lookup([surf]))
	var ov: ChevronOverlay = overlays[surf.id][0]
	assert_eq(ov.incoming_color, ChevronOverlayBuilder.PLAN_VALID_COLOR, "Incoming valid (hit on-segment)")
	assert_eq(ov.outgoing_color, ChevronOverlayBuilder.PLAN_INVALID_COLOR, "Outgoing invalid (no continuation)")

func test_plan_reflection_both_invalid() -> void:
	var surf := _mirror_surf(400)
	var plan := PlanManager.new()
	plan.add_entry(surf.id, Side.Value.LEFT)
	var overlays := ChevronOverlayBuilder.build_plan_overlays(plan, {}, _surface_lookup([surf]))
	var ov: ChevronOverlay = overlays[surf.id][0]
	assert_eq(ov.incoming_color, ChevronOverlayBuilder.PLAN_INVALID_COLOR, "Incoming invalid")
	assert_eq(ov.outgoing_color, ChevronOverlayBuilder.PLAN_INVALID_COLOR, "Outgoing invalid")

func test_plan_portal_splits_across_surfaces() -> void:
	var seg_a := Segment.from_coords(Vector2(200, 200), Vector2(200, 400), Vector2(200, 300))
	var result := RigidMotionEffect.create_portal_pair(seg_a, 0.0, Vector2(300, 0))
	var cfg_a := SideConfig.new(result.source_effect, true)
	var surf_a := Surface.new(seg_a, cfg_a, cfg_a)
	var cfg_b := SideConfig.new(result.target_effect, true)
	var surf_b := Surface.new(result.target_segment, cfg_b, cfg_b)
	var link := SideLink.from_pair(surf_a, Side.Value.LEFT, surf_b, Side.Value.RIGHT)
	surf_a.set_side_link(Side.Value.LEFT, link)
	surf_b.set_side_link(Side.Value.RIGHT, link.outgoing)

	var plan := PlanManager.new()
	plan.add_entry(surf_a.id, Side.Value.LEFT)
	var phits := {surf_a.id: [{"side": Side.Value.LEFT, "on_segment": true, "has_continuation": true}]}
	var overlays := ChevronOverlayBuilder.build_plan_overlays(plan, phits, _surface_lookup([surf_a, surf_b]))

	var entry_list: Array = overlays.get(surf_a.id, [])
	assert_eq(entry_list.size(), 1, "Entry surface has overlay")
	assert_true(entry_list[0].has_incoming, "Entry has incoming")
	assert_false(entry_list[0].has_outgoing, "Entry has no local outgoing")

	var partner_list: Array = overlays.get(surf_b.id, [])
	assert_eq(partner_list.size(), 1, "Partner has overlay")
	assert_false(partner_list[0].has_incoming, "Partner has no incoming")
	assert_true(partner_list[0].has_outgoing, "Partner has outgoing")

func test_plan_terminal_no_overlay() -> void:
	var seg := Segment.from_coords(Vector2(400, 0), Vector2(400, 600), Vector2(400, 300))
	var terminal := TerminalEffect.new()
	var surf := Surface.new(seg, SideConfig.new(terminal, true), SideConfig.new(terminal, true))
	var plan := PlanManager.new()
	plan.add_entry(surf.id, Side.Value.LEFT)
	var overlays := ChevronOverlayBuilder.build_plan_overlays(plan, {}, _surface_lookup([surf]))
	var list: Array = overlays.get(surf.id, [])
	assert_eq(list.size(), 1, "a planned terminal side is marked")
	if list.size() == 1:
		assert_false(list[0].has_outgoing, "incoming only")

# ============================================================
# Phase 6: Chevron geometry
# ============================================================

const SurfaceNodeScript = preload("res://scripts/game/surface_node.gd")

func test_inner_chevron_direction_toward_surface() -> void:
	var start := Vector2(0, 0)
	var end_v := Vector2(0, 600)
	var sample := SurfaceNodeScript.line_sample(start, end_v, 0.5)
	var normal: Vector2 = sample.normal
	var outward := normal
	var inward := -outward
	var inner_dist: float = SurfaceNodeScript.INNER_CHEVRON_DIST
	var tip: Vector2 = sample.position + outward * inner_dist
	var chev_size: float = SurfaceNodeScript.CHEVRON_SIZE
	var verts := SurfaceNodeScript.chevron_vertices(tip, inward, chev_size)
	var tip_v: Vector2 = verts[0]
	var base_center := (Vector2(verts[1]) + Vector2(verts[2])) / 2.0
	var tip_dist := tip_v.distance_to(sample.position)
	var base_dist := base_center.distance_to(sample.position)
	assert_true(tip_dist < base_dist, "Inner chevron tip should be closer to surface than base")

func test_outer_chevron_direction_away_from_surface() -> void:
	var start := Vector2(0, 0)
	var end_v := Vector2(0, 600)
	var sample := SurfaceNodeScript.line_sample(start, end_v, 0.5)
	var normal: Vector2 = sample.normal
	var outward := normal
	var outer_dist: float = SurfaceNodeScript.OUTER_CHEVRON_DIST
	var tip: Vector2 = sample.position + outward * outer_dist
	var chev_size: float = SurfaceNodeScript.CHEVRON_SIZE
	var verts := SurfaceNodeScript.chevron_vertices(tip, outward, chev_size)
	var tip_v: Vector2 = verts[0]
	var base_center := (Vector2(verts[1]) + Vector2(verts[2])) / 2.0
	var tip_dist := tip_v.distance_to(sample.position)
	var base_dist := base_center.distance_to(sample.position)
	assert_true(tip_dist > base_dist, "Outer chevron tip should be farther from surface than base")

func test_inner_outer_no_overlap() -> void:
	var inner_dist: float = SurfaceNodeScript.INNER_CHEVRON_DIST
	var outer_dist: float = SurfaceNodeScript.OUTER_CHEVRON_DIST
	var chev_size: float = SurfaceNodeScript.CHEVRON_SIZE
	var inner_max: float = inner_dist + chev_size * cos(0.5)
	var outer_min: float = outer_dist - chev_size * cos(0.5)
	assert_true(outer_min >= inner_max, "Outer base must not overlap inner base: outer_min=%f inner_max=%f" % [outer_min, inner_max])

func test_set_hover_overlays_stores_data() -> void:
	var surf := _mirror_surf(400)
	var node := SurfaceNodeScript.new()
	node.setup(surf)
	add_child_autofree(node)
	var ov := ChevronOverlay.hover(Side.Value.LEFT, true, Side.Value.LEFT, Color.WHITE)
	node.set_hover_overlays([ov])
	assert_eq(node._hover_overlays.size(), 1, "Overlay stored")

func test_clear_hover_empties_overlays() -> void:
	var surf := _mirror_surf(400)
	var node := SurfaceNodeScript.new()
	node.setup(surf)
	add_child_autofree(node)
	var ov := ChevronOverlay.hover(Side.Value.LEFT, true, Side.Value.LEFT, Color.WHITE)
	node.set_hover_overlays([ov])
	node.clear_hover()
	assert_eq(node._hover_overlays.size(), 0, "Overlay cleared")


# ============================================================
# Every interactive side is highlighted (hover) and marked (plan)
# ============================================================

func _outgoing_sides(overlays: Dictionary, sid: int) -> Array:
	var out: Array = []
	for o: ChevronOverlay in overlays.get(sid, []):
		if o.has_outgoing:
			out.append(o.outgoing_side)
	return out

func test_hover_projective_through_exits_opposite_side() -> void:
	var seg := Segment.from_coords(Vector2(400, 0), Vector2(400, 600), Vector2(400, 300))
	var cfg := SideConfig.new(LineNormalProjection.new(false), true)
	var surf := Surface.new(seg, cfg, cfg)
	var ov := ChevronOverlayBuilder.build_hover_overlays(surf, Side.Value.LEFT)
	assert_false(ov.is_empty(), "a projective side is highlighted")
	assert_eq(_outgoing_sides(ov, surf.id), [Side.Value.RIGHT], "through: it leaves on the far side")

func test_hover_projective_back_exits_same_side() -> void:
	var seg := Segment.from_coords(Vector2(400, 0), Vector2(400, 600), Vector2(400, 300))
	var cfg := SideConfig.new(CircleNormalProjection.new(true), true)
	var surf := Surface.new(seg, cfg, cfg)
	var ov := ChevronOverlayBuilder.build_hover_overlays(surf, Side.Value.RIGHT)
	assert_eq(_outgoing_sides(ov, surf.id), [Side.Value.RIGHT], "back: it leaves on the same side")

func test_hover_toggler_passes_through() -> void:
	var seg := Segment.from_coords(Vector2(400, 0), Vector2(400, 600), Vector2(400, 300))
	var cfg := SideConfig.new(null, true, [StateChangeHit.new("ghost", StateChange.Op.TOGGLE)])
	var surf := Surface.new(seg, cfg, cfg)
	var ov := ChevronOverlayBuilder.build_hover_overlays(surf, Side.Value.LEFT)
	assert_eq(_outgoing_sides(ov, surf.id), [Side.Value.RIGHT], "a toggler is passed through")

func test_hover_flag_mirror_follows_state() -> void:
	var seg := Segment.from_coords(Vector2(400, 0), Vector2(400, 600), Vector2(400, 300))
	var refl := SideConfig.new(ReflectionEffect.new(seg.get_carrier()), true)
	var off := SideConfig.new(null, false)
	var surf := Surface.with_resolver(seg, CategoricalResolver.new("open", {"true": [refl, refl], "DEFAULT": [off, off]}))
	assert_true(ChevronOverlayBuilder.build_hover_overlays(surf, Side.Value.LEFT, GameState.new({})).is_empty(),
		"closed: not interactive, no highlight")
	var ov := ChevronOverlayBuilder.build_hover_overlays(surf, Side.Value.LEFT, GameState.new({"open": true}))
	assert_eq(_outgoing_sides(ov, surf.id), [Side.Value.LEFT], "open: a mirror, same side")

func test_plan_projective_marked() -> void:
	var seg := Segment.from_coords(Vector2(400, 0), Vector2(400, 600), Vector2(400, 300))
	var cfg := SideConfig.new(LineDirectionalProjection.new(Vector2(1, 0)), true)
	var surf := Surface.new(seg, cfg, cfg)
	var plan := PlanManager.new()
	plan.add_entry(surf.id, Side.Value.LEFT)
	var ov := ChevronOverlayBuilder.build_plan_overlays(plan, {}, _surface_lookup([surf]))
	assert_false(ov.is_empty(), "a planned projective side is marked")

func test_non_interactive_side_not_highlighted() -> void:
	var seg := Segment.from_coords(Vector2(400, 0), Vector2(400, 600), Vector2(400, 300))
	var surf := Surface.new(seg, SideConfig.new(null, false), SideConfig.new(null, false))
	assert_true(ChevronOverlayBuilder.build_hover_overlays(surf, Side.Value.LEFT).is_empty())
