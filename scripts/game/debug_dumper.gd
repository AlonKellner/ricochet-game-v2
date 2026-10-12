class_name DebugDumper
extends RefCounted

static func dump(gm) -> void:
	var lines: PackedStringArray = []
	lines.append("=== DEBUG STATE (F12) ===")

	var player: CharacterBody2D = gm._player
	var cursor: Node2D = gm._cursor
	var path_renderer: Node2D = gm._path_renderer

	var player_pos := Vector2.ZERO
	var cursor_pos := Vector2.ZERO
	if player:
		player_pos = player.global_position
		lines.append("Player: %s" % player_pos)
	if cursor:
		cursor_pos = cursor.global_position
		lines.append("Cursor: %s" % cursor_pos)
	if player and cursor:
		var aim_vec := (cursor_pos - player_pos)
		lines.append("Aim vector: %s (len=%.2f)" % [aim_vec.normalized(), aim_vec.length()])

	var surfaces: Array = gm._get_surfaces()
	lines.append("Surfaces: %d" % surfaces.size())
	for surf in surfaces:
		var state := GameState.new()
		var left: SideConfig = surf.active_side_config(Side.Value.LEFT, state)
		var right: SideConfig = surf.active_side_config(Side.Value.RIGHT, state)
		var left_type := _effect_name(left.effect if left else null)
		var right_type := _effect_name(right.effect if right else null)
		var seg_len: float = surf.segment.start.coords.distance_to(surf.segment.end.coords)
		var solid_str := " SOLID" if surf.player_solid else ""
		var target_str := " TARGET" if surf.is_target else ""
		lines.append("  Surface %d: (%s → %s) len=%.1f L=%s R=%s%s%s" % [
			surf.id, surf.segment.start.coords, surf.segment.end.coords,
			seg_len, left_type, right_type, solid_str, target_str])

	var plan: PlanManager = gm.plan
	lines.append("Plan: %d entries" % plan.entries.size())
	for i in plan.entries.size():
		var entry: PlanManager.PlanEntry = plan.entries[i]
		var side_name := "LEFT" if entry.side == Side.Value.LEFT else "RIGHT"
		lines.append("  [%d] surface_id=%d side=%s" % [i, entry.surface_id, side_name])

	if path_renderer:
		lines.append("Display mode: %s (TAB to cycle)" % path_renderer.DISPLAY_MODE_NAMES[path_renderer.display_mode])
		lines.append("Solver: %s" % path_renderer.last_solver)
		var br: PrecisionLadder.Result = path_renderer.last_beam_result
		if br != null:
			lines.append("  planned: %d bits   physical: %d bits (tiers tried %s, converged=%s)   %.1f ms" % [
				NumCtx.with_limbs(br.planned_tier).bits(), NumCtx.with_limbs(br.physical_tier).bits(),
				str(br.tiers_tried.map(func(l): return NumCtx.with_limbs(l).bits())), br.converged, br.usec / 1000.0])

	if player and cursor and path_renderer:
		var plan_entries: Array = []
		var p: PlanManager = path_renderer._get_plan()
		if p and not p.is_empty():
			plan_entries = p.entries
		var aim_dir: Direction = Planner.compute_aim_direction(
			player_pos, cursor_pos, plan_entries, surfaces, GameState.new())
		lines.append("Planner aim: direction=%s end=%s" % [aim_dir.to_normalized(), aim_dir.end.coords])
		if plan_entries.size() > 0:
			var image = aim_dir.end.coords
			lines.append("  Image (aim target): %s" % image)
			lines.append("  Cursor→Image dist: %.2f" % cursor_pos.distance_to(image))

	_dump_trace(lines, "Physical", "P", path_renderer.get_traced_path() if path_renderer else null, surfaces)
	_dump_trace(lines, "Planned", "L", path_renderer.get_planned_path() if path_renderer else null, surfaces)
	_dump_merged(lines, path_renderer)

	lines.append("=== END DEBUG STATE ===")
	print("\n".join(lines))

static func _dump_trace(lines: PackedStringArray, trace_name: String, prefix: String, path: Tracer.TracedPath, surfaces: Array) -> void:
	if path == null:
		lines.append("%s trace: (null)" % trace_name)
		return
	lines.append("%s trace: %d steps, cursor_index=%d, targets_hit=%s" % [
		trace_name, path.steps.size(), path.cursor_index, path.targets_hit])
	for i in path.steps.size():
		var step: Tracer.Step = path.steps[i]
		var parts: PackedStringArray = []
		parts.append("  %s%d: %s → %s" % [prefix, i, step.start, step.end])
		var seg_len := step.start.distance_to(step.end)
		parts.append("len=%.1f" % seg_len)
		parts.append("fid=%d" % step.frame_id)
		if step.is_arc_step:
			parts.append("ARC")
		if step.frame and step.frame.conjugating:
			parts.append("CONJ")
		if step.hit == null:
			parts.append("[virt]")
		else:
			var hit: Intersection.HitRecord = step.hit
			var side_name := "L" if hit.side == Side.Value.LEFT else "R"
			var on_seg_str := "on" if hit.on_segment else "off"
			var surf_id := str(step.surface_id) if step.surface_id >= 0 else _find_surface_id(hit.segment, surfaces)
			parts.append("[hit t=%.4f side=%s %s-seg surf=%s]" % [hit.t, side_name, on_seg_str, surf_id])
		if step.precision_bits > 0:
			parts.append("err≈%s@%db" % [String.num_scientific(step.est_error), step.precision_bits])
		if i == path.cursor_index:
			parts.append("<-- CURSOR")
		lines.append(" ".join(parts))

static func _dump_merged(lines: PackedStringArray, path_renderer: Node2D) -> void:
	if not path_renderer:
		return
	var typed: Array = path_renderer.get_typed_steps()
	if typed.size() == 0:
		lines.append("Merged: (empty)")
		return
	var type_names := {
		StepTypes.Type.ALIGNED: "ALIGNED",
		StepTypes.Type.ALIGNED_POST_PLANNED: "ALIGNED_POST",
		StepTypes.Type.DIVERGED_PHYSICAL: "DIV_PHYS",
		StepTypes.Type.DIVERGED_PLANNED: "DIV_PLAN",
		StepTypes.Type.DIVERGED_POST_PLANNED: "DIV_POST",
	}
	lines.append("Merged: %d steps" % typed.size())
	for i in typed.size():
		var ms: Tracer.Step = typed[i]
		var tname: String = type_names.get(ms.type, "?%d" % ms.type)
		var solid_str := "solid" if StepTypes.is_solid(ms.type) else "dash"
		lines.append("  M%d: %s → %s [%s %s] fid=%d" % [
			i, ms.start, ms.end, tname, solid_str, ms.frame_id])

static func _find_surface_id(seg: Segment, surfaces: Array) -> String:
	for surf in surfaces:
		if surf.segment == seg:
			return str(surf.id)
	return "?"

static func _effect_name(effect: Effect) -> String:
	if effect == null:
		return "pass"
	return effect.get_display_name()
