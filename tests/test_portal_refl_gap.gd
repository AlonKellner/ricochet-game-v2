extends GutTest

const COMBO_BASE := "res://scenes/test_levels/combo_base.tscn"
const LINE1 := Vector4(600, 300, 600, 700)
const ARC1_START := Vector2(400, 250)
const ARC1_END := Vector2(300, 250)
const ARC1_VIA := Vector2(350, 200)
const ARC2_START := Vector2(1550, 750)
const ARC2_END := Vector2(1450, 750)
const ARC2_VIA := Vector2(1500, 700)

const PLAYER := Vector2(1334.662, 234.909)
const CURSOR := Vector2(400, 250)

func _build_scene() -> Node:
	Surface.reset_id_counter()
	MobiusTransform.reset_id_counter()
	var scene: Node = load(COMBO_BASE).instantiate()
	scene.gravity = Vector2.ZERO
	scene.portal_lines = PackedFloat64Array([
		LINE1.x, LINE1.y, LINE1.z, LINE1.w, 0, 1000, 0])
	scene.reflective_arcs = PackedFloat64Array([
		ARC1_START.x, ARC1_START.y, ARC1_END.x, ARC1_END.y, ARC1_VIA.x, ARC1_VIA.y,
		ARC2_START.x, ARC2_START.y, ARC2_END.x, ARC2_END.y, ARC2_VIA.x, ARC2_VIA.y])
	return scene

func _trace_raw(scene: Node) -> Tracer.TracedPath:
	var surfaces: Array[Surface] = scene.surfaces
	var cache := TransformCache.new()
	var aim_dir: Direction = Planner.compute_aim_direction(
		PLAYER, CURSOR, [], surfaces, GameState.new(), cache)
	var aim_ray := Ray.from_coords(PLAYER, aim_dir)
	return Tracer.trace(PLAYER, aim_dir, surfaces, GameState.new(),
		aim_ray, -1.0,
		Tracer.TraceMode.PHYSICAL, Tracer.TraceMode.PHYSICAL, [], cache, CURSOR)

# legacy: pin the renderer to the legacy Tracer (frame diagnostics inspect its
# normalized frames; removed with Tracer in Stage 84)
func _trace_via_renderer(scene: Node, legacy: bool = false) -> Tracer.TracedPath:
	var player := scene.get_node("Player")
	var cursor := scene.get_node("Cursor")
	var renderer := scene.get_node("PathRenderer")
	renderer.use_beam_tracer = not legacy
	player.global_position = PLAYER
	cursor.global_position = CURSOR
	renderer._compute_trace()
	return renderer.get_traced_path()

func test_no_non_portal_large_gaps() -> void:
	var scene := _build_scene()
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var raw_path := _trace_raw(scene)
	var rendered_path := _trace_via_renderer(scene)

	var raw_gaps := _find_large_gaps(raw_path, 5.0)
	var rendered_gaps := _find_large_gaps(rendered_path, 5.0)

	var non_portal_raw := raw_gaps.filter(func(g): return not g["after_portal"])
	var non_portal_rendered := rendered_gaps.filter(func(g): return not g["after_portal"])

	assert_eq(non_portal_raw.size(), 0,
		"Raw trace should have no non-portal gaps > 5px")
	assert_eq(non_portal_rendered.size(), 0,
		"Rendered trace should have no non-portal gaps > 5px")

func test_frame_transitions_at_gaps() -> void:
	var scene := _build_scene()
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var raw_path := _trace_raw(scene)
	var rendered_path := _trace_via_renderer(scene, true)

	gut.p("=== FRAME TRANSITIONS (raw) ===")
	_analyze_frame_transitions(raw_path, "RAW")

	gut.p("")
	gut.p("=== FRAME TRANSITIONS (rendered) ===")
	_analyze_frame_transitions(rendered_path, "RENDERED")

	gut.p("")
	gut.p("=== STEP COUNT DIFFERENCE ===")
	gut.p("Raw steps: %d, Rendered steps: %d, Diff: %d" % [
		raw_path.steps.size(), rendered_path.steps.size(),
		rendered_path.steps.size() - raw_path.steps.size()])
	if raw_path.steps.size() != rendered_path.steps.size():
		gut.p("VisualConverter CHANGED step count — potential after_portal flag loss")

	pass_test("Frame transition analysis complete")

func test_hitpoint_visual_end_consistency() -> void:
	var scene := _build_scene()
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var raw_path := _trace_raw(scene)
	var max_diff := 0.0
	for i in raw_path.steps.size():
		var step: Tracer.Step = raw_path.steps[i]
		if step.hit == null:
			continue
		var hp_coords := step.hit.point.coords
		var recomputed_end: Vector2 = step.frame.apply(hp_coords)
		var end_diff := step.end.distance_to(recomputed_end)
		if end_diff > max_diff:
			max_diff = end_diff
	assert_lt(max_diff, 1.0, "Visual end should match frame.apply(hitpoint) within 1px")

func _dump_steps(path: Tracer.TracedPath, label: String) -> void:
	for i in path.steps.size():
		var step: Tracer.Step = path.steps[i]
		var gap := 0.0
		if i > 0:
			gap = path.steps[i - 1].end.distance_to(step.start)
		var via_inf := is_inf(step.via.x) or is_inf(step.via.y)
		var frame_info := "frame=%d conj=%s" % [step.frame.id, step.frame.conjugating]
		var portal_flag := " PORTAL" if step.after_portal else ""
		var hit_info := ""
		if step.hit:
			hit_info = " hit=(%s) surf=%d side=%d" % [step.hit.point.coords, step.surface_id, step.hit_side]
		var gap_marker := ""
		if gap > 100.0:
			gap_marker = " *** GAP=%.1f ***" % gap
		elif gap > 1.5:
			gap_marker = " * gap=%.2f *" % gap
		gut.p("  [%s %02d] start=%s end=%s via_inf=%s %s%s%s%s" % [
			label, i, step.start, step.end, via_inf, frame_info, portal_flag, hit_info, gap_marker])

func _find_large_gaps(path: Tracer.TracedPath, threshold: float) -> Array[Dictionary]:
	var gaps: Array[Dictionary] = []
	for i in range(1, path.steps.size()):
		var prev: Tracer.Step = path.steps[i - 1]
		var curr: Tracer.Step = path.steps[i]
		var gap := prev.end.distance_to(curr.start)
		if gap > threshold:
			gaps.append({
				"index": i,
				"gap": gap,
				"prev_end": prev.end,
				"curr_start": curr.start,
				"after_portal": curr.after_portal,
				"prev_frame": prev.frame.id,
				"curr_frame": curr.frame.id,
			})
	return gaps


func test_root_cause_frame_discontinuity() -> void:
	var scene := _build_scene()
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var raw_path := _trace_raw(scene)

	gut.p("=== ROOT CAUSE ANALYSIS: frame.apply(hp) across frame transitions ===")
	gut.p("")
	for i in range(1, raw_path.steps.size()):
		var prev: Tracer.Step = raw_path.steps[i - 1]
		var curr: Tracer.Step = raw_path.steps[i]
		var gap := prev.end.distance_to(curr.start)
		if gap < 1.5:
			continue

		var hp_coords := prev.hit.point.coords if prev.hit else Vector2.ZERO
		var prev_frame_at_hp := prev.frame.apply(hp_coords)
		var curr_frame_at_hp := curr.frame.apply(hp_coords)
		var frame_diff := prev_frame_at_hp.distance_to(curr_frame_at_hp)

		gut.p("Gap at step %d->%d: %.1f px" % [i - 1, i, gap])
		gut.p("  prev.end = %s (= prev_frame.apply(hp) where hp=%s)" % [prev.end, hp_coords])
		gut.p("  curr.start = %s (= curr_frame.apply(ray_origin))" % curr.start)
		gut.p("  prev_frame.apply(hp) = %s" % prev_frame_at_hp)
		gut.p("  curr_frame.apply(hp) = %s  (diff = %.3f)" % [curr_frame_at_hp, frame_diff])
		gut.p("  prev_frame: id=%d conj=%s" % [prev.frame.id, prev.frame.conjugating])
		gut.p("  curr_frame: id=%d conj=%s" % [curr.frame.id, curr.frame.conjugating])
		gut.p("  after_portal=%s" % curr.after_portal)

		if curr.after_portal:
			gut.p("  -> EXPECTED portal gap (~1000px), covered by after_portal flag")
		elif frame_diff > 1.0:
			gut.p("  -> BUG: reflection changed frame, old and new frame map SAME normalized point to DIFFERENT visual positions")
			gut.p("     The gap IS the frame discontinuity: new_frame(hp) != old_frame(hp)")
			gut.p("     Reflection on arc carrier is NOT fixing the visual point")
		gut.p("")

	pass_test("Root cause analysis complete")

func _analyze_frame_transitions(path: Tracer.TracedPath, label: String) -> void:
	for i in range(1, path.steps.size()):
		var prev: Tracer.Step = path.steps[i - 1]
		var curr: Tracer.Step = path.steps[i]
		var gap := prev.end.distance_to(curr.start)
		if prev.frame.id != curr.frame.id or gap > 1.5:
			var transition := "SAME_FRAME" if prev.frame.id == curr.frame.id else "FRAME_CHANGE(%d->%d)" % [prev.frame.id, curr.frame.id]
			var portal := "after_portal=true" if curr.after_portal else "after_portal=FALSE"
			gut.p("  [%s %d->%d] gap=%.3f %s %s conj:%s->%s" % [
				label, i - 1, i, gap, transition, portal,
				prev.frame.conjugating, curr.frame.conjugating])
			if prev.frame.id != curr.frame.id:
				gut.p("    prev_frame: a=(%.4f,%.4f) b=(%.4f,%.4f) c=(%.4f,%.4f) d=(%.4f,%.4f)" % [prev.frame.a_re, prev.frame.a_im, prev.frame.b_re, prev.frame.b_im, prev.frame.c_re, prev.frame.c_im, prev.frame.d_re, prev.frame.d_im])
				gut.p("    curr_frame: a=(%.4f,%.4f) b=(%.4f,%.4f) c=(%.4f,%.4f) d=(%.4f,%.4f)" % [curr.frame.a_re, curr.frame.a_im, curr.frame.b_re, curr.frame.b_im, curr.frame.c_re, curr.frame.c_im, curr.frame.d_re, curr.frame.d_im])


func test_trace_ends_on_wall_carrier() -> void:
	var scene := _build_scene()
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var surfaces: Array[Surface] = scene.surfaces
	var rendered_path := _trace_via_renderer(scene)

	var last: Tracer.Step = rendered_path.steps[rendered_path.steps.size() - 1]
	assert_not_null(last.hit, "Last step should have a hit")

	var hit_surf_id := last.surface_id
	for surf in surfaces:
		var s: Surface = surf
		if s.id == hit_surf_id:
			var phys_carrier := s.segment.get_carrier()
			var dist := InvariantChecker._geometric_carrier_dist(last.end, phys_carrier)
			gut.p("Last step end=%s, dist to wall carrier (surf %d)=%.4f" % [last.end, s.id, dist])
			assert_lt(dist, 2.0, "Visual end should be within 2px of wall carrier")
			break


func test_investigate_violations() -> void:
	var scene := _build_scene()
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var surfaces: Array[Surface] = scene.surfaces
	var renderer := scene.get_node("PathRenderer")
	var player := scene.get_node("Player")
	var cursor := scene.get_node("Cursor")
	player.global_position = PLAYER
	cursor.global_position = CURSOR
	renderer.use_beam_tracer = false     # inspects legacy frames (Stage 84)
	renderer._compute_trace()

	var physical_path: Tracer.TracedPath = renderer.get_traced_path()
	var planned_path: Tracer.TracedPath = renderer.get_planned_path()

	var room_rect: Rect2 = scene.room_rect
	var default_bounds := VisualConverter.DEFAULT_BOUNDS

	gut.p("=== VIOLATION INVESTIGATION ===")
	gut.p("Player: %s  Cursor: %s" % [PLAYER, CURSOR])
	gut.p("Room rect: %s (end: %s)" % [room_rect, room_rect.end])
	gut.p("Default bounds: %s (end: %s)" % [default_bounds, default_bounds.end])
	gut.p("Surfaces: %d" % surfaces.size())
	for surf in surfaces:
		var s: Surface = surf
		var c := s.segment.get_carrier()
		var kind := "line" if c.is_line() else "circle(r=%.1f)" % c.radius()
		gut.p("  Surface %d: %s  carrier=%s  start=%s end=%s" % [
			s.id, kind, c, s.segment.start.coords, s.segment.end.coords])
	gut.p("")

	for trace_name in ["physical", "planned"]:
		var path: Tracer.TracedPath = physical_path if trace_name == "physical" else planned_path
		if path == null:
			gut.p("  %s path is null" % trace_name)
			continue
		gut.p("=== %s trace: %d steps ===" % [trace_name.to_upper(), path.steps.size()])

		for i in path.steps.size():
			var step: Tracer.Step = path.steps[i]
			var flag := ""
			if i == 5 or i == 7:
				flag = " <<<< VISUAL-ON-CARRIER VIOLATION"
			if i == path.steps.size() - 1:
				flag += " <<<< LAST STEP (TRACE-ENDS check)"

			var hit_info := "no-hit"
			if step.hit:
				hit_info = "hit surf=%d at=%s side=%d" % [step.surface_id, step.hit.point.coords, step.hit_side]

			gut.p("  [%02d] start=%s end=%s frame=%d conj=%s portal=%s %s%s" % [
				i, step.start, step.end, step.frame.id, step.frame.conjugating,
				step.after_portal, hit_info, flag])

			if step.hit:
				var mapped := step.frame.apply(step.hit.point.coords)
				var map_diff := step.end.distance_to(mapped)
				gut.p("       frame.apply(hit)=%s  diff_from_end=%.4f" % [mapped, map_diff])

			# Distance to every physical carrier
			var min_carrier_dist := INF
			var closest_surf_id := -1
			for surf in surfaces:
				var s: Surface = surf
				var carrier := s.segment.get_carrier()
				var f := carrier.evaluate(step.end)
				var gx := 2.0 * carrier.a * step.end.x + carrier.b
				var gy := 2.0 * carrier.a * step.end.y + carrier.c
				var grad := sqrt(gx * gx + gy * gy)
				var dist := absf(f) / maxf(grad, 1e-10)
				if dist < min_carrier_dist:
					min_carrier_dist = dist
					closest_surf_id = s.id
			gut.p("       closest_carrier: surf=%d dist=%.4f (threshold=5.0)" % [closest_surf_id, min_carrier_dist])

			if i == 5 or i == 7:
				gut.p("       --- DETAILED CARRIER DISTANCES FOR STEP %d ---" % i)
				for surf in surfaces:
					var s: Surface = surf
					var carrier := s.segment.get_carrier()
					var f := carrier.evaluate(step.end)
					var gx := 2.0 * carrier.a * step.end.x + carrier.b
					var gy := 2.0 * carrier.a * step.end.y + carrier.c
					var grad := sqrt(gx * gx + gy * gy)
					var dist := absf(f) / maxf(grad, 1e-10)
					var kind := "line" if carrier.is_line() else "circle"
					gut.p("         surf=%d (%s): dist=%.4f  f=%.6f  grad=%.6f" % [s.id, kind, dist, f, grad])

		# TRACE-ENDS analysis for last step
		var last: Tracer.Step = path.steps[path.steps.size() - 1]
		var end_pos := last.end
		gut.p("")
		gut.p("  TRACE-ENDS analysis: end=%s" % end_pos)
		gut.p("    dist_to_player=%.4f (threshold=2.0)" % end_pos.distance_to(PLAYER))
		gut.p("    bounds check (DEFAULT_BOUNDS): x_left=%.1f x_right=%.1f y_top=%.1f y_bottom=%.1f" % [
			end_pos.x - default_bounds.position.x,
			default_bounds.end.x - end_pos.x,
			end_pos.y - default_bounds.position.y,
			default_bounds.end.y - end_pos.y])
		gut.p("    bounds check (room_rect): x_left=%.1f x_right=%.1f y_top=%.1f y_bottom=%.1f" % [
			end_pos.x - room_rect.position.x,
			room_rect.end.x - end_pos.x,
			end_pos.y - room_rect.position.y,
			room_rect.end.y - end_pos.y])
		var at_default_bounds := (end_pos.x <= default_bounds.position.x + 2.0 or
			end_pos.x >= default_bounds.end.x - 2.0 or
			end_pos.y <= default_bounds.position.y + 2.0 or
			end_pos.y >= default_bounds.end.y - 2.0)
		var at_room_bounds := (end_pos.x <= room_rect.position.x + 2.0 or
			end_pos.x >= room_rect.end.x - 2.0 or
			end_pos.y <= room_rect.position.y + 2.0 or
			end_pos.y >= room_rect.end.y - 2.0)
		gut.p("    at_default_bounds=%s  at_room_bounds=%s" % [at_default_bounds, at_room_bounds])
		gut.p("")

	# Verify VISUAL-ON-CARRIER root cause
	gut.p("=== VISUAL-ON-CARRIER ROOT CAUSE VERIFICATION ===")
	for step_idx in [5, 7]:
		if step_idx >= physical_path.steps.size():
			continue
		var step: Tracer.Step = physical_path.steps[step_idx]
		if step.hit == null:
			continue
		var hit_surf_id := step.surface_id
		var hp := step.hit.point.coords
		gut.p("Step %d: hit surf=%d, hitpoint=%s, end=%s, frame=%d, conj=%s, portal=%s" % [
			step_idx, hit_surf_id, hp, step.end, step.frame.id, step.frame.conjugating, step.after_portal])

		# Check: hitpoint should be on the hit segment's carrier (the carrier used for intersection)
		var hit_seg: Segment = step.hit.segment
		if hit_seg:
			var hit_carrier := hit_seg.get_carrier()
			var hp_on_hit_carrier := InvariantChecker._geometric_carrier_dist(hp, hit_carrier)
			gut.p("  hitpoint dist to HIT SEGMENT carrier: %.6f (should be ~0)" % hp_on_hit_carrier)
			gut.p("  hit segment carrier: a=%.6f b=%.6f c=%.6f d=%.6f is_line=%s" % [
				hit_carrier.a, hit_carrier.b, hit_carrier.c, hit_carrier.d, hit_carrier.is_line()])
			gut.p("  hit segment: start=%s end=%s via=%s" % [
				hit_seg.start.coords, hit_seg.end.coords, hit_seg.via.coords])

		# Show the frame details
		gut.p("  frame: a=(%.4f,%.4f) b=(%.4f,%.4f) c=(%.4f,%.4f) d=(%.4f,%.4f)" % [step.frame.a_re, step.frame.a_im, step.frame.b_re, step.frame.b_im, step.frame.c_re, step.frame.c_im, step.frame.d_re, step.frame.d_im])

		# Check visual end against ALL physical carriers
		gut.p("  visual end dist to each physical carrier:")
		for surf in surfaces:
			var s: Surface = surf
			var d := InvariantChecker._geometric_carrier_dist(step.end, s.segment.get_carrier())
			if d < 100.0:
				gut.p("    surf=%d: %.4f" % [s.id, d])

		# The KEY question: frame.apply maps hitpoint → visual end.
		# If frame preserves carrier membership, visual end should be on physical carrier of hit surface.
		# But portal frames DON'T preserve carrier membership for surfaces on the OTHER side.
		for surf in surfaces:
			var s: Surface = surf
			if s.id != hit_surf_id:
				continue
			var phys_carrier := s.segment.get_carrier()
			var end_on_phys := InvariantChecker._geometric_carrier_dist(step.end, phys_carrier)
			gut.p("  visual end dist to HIT SURFACE's physical carrier (surf %d): %.4f" % [hit_surf_id, end_on_phys])
			break
		gut.p("")

	# Verify TRACE-ENDS root cause
	gut.p("=== TRACE-ENDS ROOT CAUSE VERIFICATION ===")
	var last_phys: Tracer.Step = physical_path.steps[physical_path.steps.size() - 1]
	if last_phys.hit:
		var hit_surf_id := last_phys.surface_id
		var hp := last_phys.hit.point.coords
		gut.p("Last step: hit surf=%d, frame=%d, conj=%s, after_portal=%s" % [
			hit_surf_id, last_phys.frame.id, last_phys.frame.conjugating, last_phys.after_portal])
		gut.p("  frame: a=(%.4f,%.4f) b=(%.4f,%.4f) c=(%.4f,%.4f) d=(%.4f,%.4f)" % [
			last_phys.frame.a_re, last_phys.frame.a_im, last_phys.frame.b_re, last_phys.frame.b_im, last_phys.frame.c_re, last_phys.frame.c_im, last_phys.frame.d_re, last_phys.frame.d_im])
		gut.p("  Hit point (normalized): %s" % hp)
		gut.p("  frame.apply(hit): %s" % last_phys.end)

		if last_phys.hit.segment:
			var hit_carrier := last_phys.hit.segment.get_carrier()
			gut.p("  hitpoint dist to hit segment carrier: %.6f" % InvariantChecker._geometric_carrier_dist(hp, hit_carrier))
			gut.p("  hit segment carrier: a=%.6f b=%.6f c=%.6f d=%.6f" % [
				hit_carrier.a, hit_carrier.b, hit_carrier.c, hit_carrier.d])

		for surf in surfaces:
			var s: Surface = surf
			if s.id == hit_surf_id:
				var phys_carrier := s.segment.get_carrier()
				var end_on_phys := InvariantChecker._geometric_carrier_dist(last_phys.end, phys_carrier)
				gut.p("  visual end dist to physical carrier (surf %d): %.4f (threshold=2.0)" % [hit_surf_id, end_on_phys])
				gut.p("  phys carrier: a=%.6f b=%.6f c=%.6f d=%.6f" % [
					phys_carrier.a, phys_carrier.b, phys_carrier.c, phys_carrier.d])
				break
	gut.p("")

	pass_test("Investigation diagnostic complete")
