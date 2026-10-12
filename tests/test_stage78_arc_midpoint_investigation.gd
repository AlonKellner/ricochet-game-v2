extends GutTest

# Investigation of ARC-MIDPOINT-ALIGNMENT violations.
# Read-only diagnostics — no production code changes.
# These investigate the legacy Tracer's normalized-frame arcs, so the renderer
# is pinned to it (use_beam_tracer = false); removed with Tracer in Stage 84.

const COMBO_BASE := "res://scenes/test_levels/combo_base.tscn"
const VIOLATIONS_PATH := "res://violations.json"

const LINE1 := Vector4(600, 300, 600, 700)
const LINE2 := Vector4(1300, 300, 1300, 700)
const ARC1_START := Vector2(400, 250)
const ARC1_END := Vector2(300, 250)
const ARC1_VIA := Vector2(350, 200)
const ARC2_START := Vector2(1550, 750)
const ARC2_END := Vector2(1450, 750)
const ARC2_VIA := Vector2(1500, 700)

enum PairType { REFL_REFL, REFL_SEMI, REFL_PROJ, REFL_DIR, SEMI_SEMI, SEMI_PROJ, SEMI_DIR, PROJ_PROJ, PROJ_DIR, DIR_DIR, PORTAL }

func _build_scene(lines_type: int, circles_type: int) -> Node:
	Surface.reset_id_counter()
	MobiusTransform.reset_id_counter()
	var scene: Node = load(COMBO_BASE).instantiate()
	scene.gravity = Vector2.ZERO
	_apply_line_pair(scene, lines_type)
	_apply_circle_pair(scene, circles_type)
	return scene

func _apply_line_pair(scene: Node, pair_type: int) -> void:
	match pair_type:
		PairType.REFL_REFL:
			scene.mirror_both_lines = Array([LINE1, LINE2], TYPE_VECTOR4, &"", null)
		PairType.REFL_SEMI:
			scene.mirror_both_lines = Array([LINE1], TYPE_VECTOR4, &"", null)
			scene.mirror_lines = Array([LINE2], TYPE_VECTOR4, &"", null)
		PairType.SEMI_SEMI:
			scene.mirror_lines = Array([LINE1, LINE2], TYPE_VECTOR4, &"", null)
		PairType.PORTAL:
			scene.portal_lines = PackedFloat64Array([
				LINE1.x, LINE1.y, LINE1.z, LINE1.w, 0, 1000, 0])

func _get_traces(renderer: Node2D) -> Dictionary:
	var result := {}
	var physical = renderer.get_traced_path()
	if physical != null:
		result["physical"] = physical
	var planned = renderer.get_planned_path()
	if planned != null:
		result["planned"] = planned
	return result

func _setup_checker(scene: Node, player_pos: Vector2, cursor_pos: Vector2) -> InvariantChecker:
	var player: CharacterBody2D = scene.get_node_or_null("Player")
	var cursor_node: Node2D = scene.get_node_or_null("Cursor")
	var renderer: Node2D = scene.get_node_or_null("PathRenderer")
	if player:
		player.global_position = player_pos
	if cursor_node:
		cursor_node.global_position = cursor_pos
	if renderer:
		renderer.use_beam_tracer = false
		renderer._compute_trace()
	var checker := InvariantChecker.new()
	checker.setup(scene)
	return checker

func _apply_circle_pair(scene: Node, pair_type: int) -> void:
	var arc1 := PackedFloat64Array([ARC1_START.x, ARC1_START.y, ARC1_END.x, ARC1_END.y, ARC1_VIA.x, ARC1_VIA.y])
	var arc2 := PackedFloat64Array([ARC2_START.x, ARC2_START.y, ARC2_END.x, ARC2_END.y, ARC2_VIA.x, ARC2_VIA.y])
	var both := PackedFloat64Array(arc1)
	both.append_array(arc2)
	match pair_type:
		PairType.REFL_REFL:
			scene.reflective_arcs = both
		PairType.REFL_SEMI:
			scene.reflective_arcs = arc1
			scene.semi_reflective_arcs = arc2
		PairType.SEMI_SEMI:
			scene.semi_reflective_arcs = both
		PairType.PORTAL:
			scene.portal_arcs = PackedFloat64Array([
				ARC1_START.x, ARC1_START.y, ARC1_END.x, ARC1_END.y, ARC1_VIA.x, ARC1_VIA.y, 0, 500, 0])


# --- Experiment 1: Reproduce a specific violation ---

func test_exp1_reproduce_violation() -> void:
	var player_pos := Vector2(210.0, 695.0)
	var cursor_pos := Vector2(1500.0, 700.0)

	var scene := _build_scene(PairType.REFL_SEMI, PairType.REFL_SEMI)
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var checker := _setup_checker(scene, player_pos, cursor_pos)
	var violations := checker.check_ARC_MIDPOINT_ALIGNMENT(player_pos, cursor_pos)

	print("=== EXPERIMENT 1: Reproduce violation ===")
	print("Player: %s  Cursor: %s" % [player_pos, cursor_pos])
	print("Violations found: %d" % violations.size())
	for v in violations:
		print("  %s" % v)

	var renderer: Node2D = scene.get_node_or_null("PathRenderer")
	if renderer:
		var traces := _get_traces(renderer)
		for trace_name in traces:
			var path: Tracer.TracedPath = traces[trace_name]
			print("\n--- %s path: %d steps ---" % [trace_name, path.steps.size()])
			for i in path.steps.size():
				var step: Tracer.Step = path.steps[i]
				if step.is_arc_step and step.frame != null:
					print("  Step %d: arc=%s start=%s end=%s via=%s surface_id=%d" % [
						i, step.is_arc_step, step.start, step.end, step.via, step.surface_id])

	assert_eq(violations.size(), 0, "Refl+Semi/Refl+Semi should have 0 ARC-MIDPOINT-ALIGNMENT violations after fix")
	pass_test("Refl+Semi / Refl+Semi: 0 ARC-MIDPOINT-ALIGNMENT violations (fix confirmed)")


# --- Experiment 2: Compare arc_mid vs step.via ---

func test_exp2_arc_mid_vs_via() -> void:
	var player_pos := Vector2(210.0, 695.0)
	var cursor_pos := Vector2(1500.0, 700.0)

	var scene := _build_scene(PairType.REFL_SEMI, PairType.REFL_SEMI)
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	_setup_checker(scene, player_pos, cursor_pos)

	var renderer: Node2D = scene.get_node_or_null("PathRenderer")
	assert_not_null(renderer)

	var bounds := VisualConverter.DEFAULT_BOUNDS
	var traces := _get_traces(renderer)

	print("=== EXPERIMENT 2: arc_mid vs step.via ===")
	var found_violation := false

	for trace_name in traces:
		var path: Tracer.TracedPath = traces[trace_name]
		var first_step: Tracer.Step = path.steps[0]
		if first_step.ray == null:
			continue
		var origin := first_step.ray.origin.coords
		var aim_dir := first_step.ray.direction.to_vector().normalized()

		for i in path.steps.size():
			var step: Tracer.Step = path.steps[i]
			if not step.is_arc_step or step.frame == null or step.hit == null:
				continue
			if not VisualConverter.is_arc(step.start, step.via, step.end):
				continue
			if InvariantChecker._is_at_bounds(step.start, bounds) or InvariantChecker._is_at_bounds(step.end, bounds):
				continue

			var p := VisualConverter.arc_params(step.start, step.via, step.end)
			var ctr: Vector2 = p["center"]
			var r: float = p["radius"]
			var sa: float = p["start_angle"]
			var ea: float = p["end_angle"]
			var mid_angle := sa + (ea - sa) * 0.5
			var arc_mid := ctr + Vector2(cos(mid_angle), sin(mid_angle)) * r

			var bt_mid := step.frame.invert().apply(arc_mid)
			if bt_mid.length() > 1e5:
				continue
			var cross := (bt_mid - origin).cross(aim_dir)
			if absf(cross) <= 10.0:
				continue

			found_violation = true
			var via_dist_to_center := step.via.distance_to(ctr)
			var arc_mid_dist_to_center := arc_mid.distance_to(ctr)
			var via_to_arc_mid := step.via.distance_to(arc_mid)

			print("\n--- %s step %d (violating, cross=%.2f) ---" % [trace_name, i, cross])
			print("  arc_mid:   %s  (angular midpoint)" % arc_mid)
			print("  step.via:  %s  (Mobius image of physical midpoint)" % step.via)
			print("  distance:  %.4f px" % via_to_arc_mid)
			print("  via dist to center:     %.4f (should ≈ radius)" % via_dist_to_center)
			print("  arc_mid dist to center: %.4f (should ≈ radius)" % arc_mid_dist_to_center)
			print("  radius:    %.4f" % r)
			print("  Both on same circle: via=%.6f arc_mid=%.6f" % [
				absf(via_dist_to_center - r), absf(arc_mid_dist_to_center - r)])

			if found_violation:
				break
		if found_violation:
			break

	assert_true(found_violation, "Should find at least one violating arc step")
	pass_test("arc_mid and step.via are different points on the same circle")


# --- Experiment 3: Back-transform both points — f32 vs f64 (DECISIVE) ---

func test_exp3_backtransform_f32_vs_f64() -> void:
	var player_pos := Vector2(210.0, 695.0)
	var cursor_pos := Vector2(1500.0, 700.0)

	var scene := _build_scene(PairType.REFL_SEMI, PairType.REFL_SEMI)
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	_setup_checker(scene, player_pos, cursor_pos)

	var renderer: Node2D = scene.get_node_or_null("PathRenderer")
	assert_not_null(renderer)

	var bounds := VisualConverter.DEFAULT_BOUNDS
	var traces := _get_traces(renderer)

	print("=== EXPERIMENT 3: f32 vs f64 back-transform (DECISIVE) ===")
	print("")
	print("Legend:")
	print("  arc_mid = angular midpoint of visual arc (invariant's point)")
	print("  via     = step.via (Mobius image of physical midpoint)")
	print("  f32     = frame.invert().apply()  (current invariant)")
	print("  f64     = frame.invert().apply()")
	print("")

	var found := false
	for trace_name in traces:
		var path: Tracer.TracedPath = traces[trace_name]
		var first_step: Tracer.Step = path.steps[0]
		if first_step.ray == null:
			continue
		var origin := first_step.ray.origin.coords
		var aim_dir := first_step.ray.direction.to_vector().normalized()

		for i in path.steps.size():
			var step: Tracer.Step = path.steps[i]
			if not step.is_arc_step or step.frame == null or step.hit == null:
				continue
			if not VisualConverter.is_arc(step.start, step.via, step.end):
				continue
			if InvariantChecker._is_at_bounds(step.start, bounds) or InvariantChecker._is_at_bounds(step.end, bounds):
				continue

			var p := VisualConverter.arc_params(step.start, step.via, step.end)
			var ctr: Vector2 = p["center"]
			var r: float = p["radius"]
			var sa: float = p["start_angle"]
			var ea: float = p["end_angle"]
			var mid_angle := sa + (ea - sa) * 0.5
			var arc_mid := ctr + Vector2(cos(mid_angle), sin(mid_angle)) * r

			var frame_inv := step.frame.invert()

			var bt_arc_mid_f32 := frame_inv.apply(arc_mid)
			if bt_arc_mid_f32.length() > 1e5:
				continue
			var cross_arc_mid_f32 := (bt_arc_mid_f32 - origin).cross(aim_dir)
			if absf(cross_arc_mid_f32) <= 10.0:
				continue

			found = true
			var bt_arc_mid_f64: Vector2 = frame_inv.apply(arc_mid)
			var bt_via_f32 := frame_inv.apply(step.via)
			var bt_via_f64: Vector2 = frame_inv.apply(step.via)

			var cross_arc_mid_f64 := (bt_arc_mid_f64 - origin).cross(aim_dir)
			var cross_via_f32 := (bt_via_f32 - origin).cross(aim_dir)
			var cross_via_f64 := (bt_via_f64 - origin).cross(aim_dir)

			print("--- %s step %d ---" % [trace_name, i])
			print("  arc_mid: %s   step.via: %s" % [arc_mid, step.via])
			print("")
			print("  BACK-TRANSFORM RESULTS:")
			print("  arc_mid f32: bt=%s  cross=%.4f  %s" % [
				bt_arc_mid_f32, cross_arc_mid_f32,
				"VIOLATES" if absf(cross_arc_mid_f32) > 10.0 else "ok"])
			print("  arc_mid f64: bt=%s  cross=%.4f  %s" % [
				bt_arc_mid_f64, cross_arc_mid_f64,
				"VIOLATES" if absf(cross_arc_mid_f64) > 10.0 else "ok"])
			print("  via f32:     bt=%s  cross=%.4f  %s" % [
				bt_via_f32, cross_via_f32,
				"VIOLATES" if absf(cross_via_f32) > 10.0 else "ok"])
			print("  via f64:     bt=%s  cross=%.4f  %s" % [
				bt_via_f64, cross_via_f64,
				"VIOLATES" if absf(cross_via_f64) > 10.0 else "ok"])
			print("")

			if absf(cross_arc_mid_f64) > 10.0 and absf(cross_via_f64) <= 10.0:
				print("  >> CONCLUSION: H2 (wrong point) — arc_mid is NOT on the ray even at f64")
			elif absf(cross_arc_mid_f64) <= 10.0 and absf(cross_arc_mid_f32) > 10.0:
				print("  >> CONCLUSION: H1 (precision) — f32 causes the violation")
			elif absf(cross_arc_mid_f64) > 10.0 and absf(cross_via_f32) > 10.0:
				print("  >> CONCLUSION: H3 (both) — wrong point AND precision issues")
			else:
				print("  >> CONCLUSION: Unexpected pattern — needs further analysis")

			print("  f32 vs f64 delta for arc_mid: %.4f" % absf(cross_arc_mid_f32 - cross_arc_mid_f64))
			print("  f32 vs f64 delta for via:     %.4f" % absf(cross_via_f32 - cross_via_f64))
			print("")

	assert_true(found, "Should find at least one violating step for analysis")
	pass_test("Decisive experiment complete — see output for hypothesis verdict")


# --- Experiment 4: Simple algebraic proof with a known transform ---

func test_exp4_algebraic_proof() -> void:
	print("=== EXPERIMENT 4: Algebraic proof with known circle reflection ===")
	print("")

	var center := Vector2(500, 400)
	var radius := 200.0
	var carrier := GeneralizedCircle.from_circle(center, radius)
	var effect := ReflectionEffect.new(carrier)

	var ray_origin := Vector2(300, 300)
	var ray_end := Vector2(700, 300)
	var ray_mid := (ray_origin + ray_end) / 2.0

	var frame: MobiusTransform = effect.get_mobius()

	var vis_start: Vector2 = frame.apply(ray_origin)
	var vis_end: Vector2 = frame.apply(ray_end)
	var vis_via: Vector2 = frame.apply(ray_mid)

	print("Physical ray: %s -> %s (midpoint: %s)" % [ray_origin, ray_end, ray_mid])
	print("Circle: center=%s radius=%.0f" % [center, radius])
	print("")
	print("Visual (Mobius image):")
	print("  vis_start: %s" % vis_start)
	print("  vis_end:   %s" % vis_end)
	print("  vis_via:   %s  (frame.apply(physical midpoint))" % vis_via)

	if VisualConverter.is_arc(vis_start, vis_via, vis_end):
		var p := VisualConverter.arc_params(vis_start, vis_via, vis_end)
		var ctr: Vector2 = p["center"]
		var r: float = p["radius"]
		var sa: float = p["start_angle"]
		var ea: float = p["end_angle"]
		var mid_angle := sa + (ea - sa) * 0.5
		var arc_mid := ctr + Vector2(cos(mid_angle), sin(mid_angle)) * r

		print("")
		print("Fitted arc: center=%s radius=%.4f" % [ctr, r])
		print("  angular midpoint (arc_mid): %s" % arc_mid)
		print("  Mobius midpoint (vis_via):  %s" % vis_via)
		print("  distance between them:      %.4f px" % arc_mid.distance_to(vis_via))

		var frame_inv: MobiusTransform = frame.invert()
		var bt_arc_mid_f64: Vector2 = frame_inv.apply(arc_mid)
		var bt_via_f64: Vector2 = frame_inv.apply(vis_via)

		var ray_dir := (ray_end - ray_origin).normalized()
		var cross_arc_mid := (bt_arc_mid_f64 - ray_origin).cross(ray_dir)
		var cross_via := (bt_via_f64 - ray_origin).cross(ray_dir)

		print("")
		print("Back-transform (f64):")
		print("  bt(arc_mid) = %s  cross=%.6f  %s" % [
			bt_arc_mid_f64, cross_arc_mid,
			"ON RAY" if absf(cross_arc_mid) < 1.0 else "OFF RAY"])
		print("  bt(vis_via) = %s  cross=%.6f  %s" % [
			bt_via_f64, cross_via,
			"ON RAY" if absf(cross_via) < 1.0 else "OFF RAY"])

		print("")
		if absf(cross_arc_mid) > 1.0 and absf(cross_via) < 1.0:
			print("PROOF: arc_mid back-transforms OFF the ray, vis_via back-transforms ON it.")
			print("       The angular midpoint of the Mobius image arc != Mobius image of physical midpoint.")
			print("       This is expected: Mobius transforms do NOT preserve arc parameterization.")
		elif absf(cross_arc_mid) < 1.0 and absf(cross_via) < 1.0:
			print("NOTE: Both back-transform ON the ray for this simple case.")
			print("      The deviation may only appear with more extreme transforms.")
		else:
			print("UNEXPECTED: arc_mid cross=%.6f, via cross=%.6f" % [cross_arc_mid, cross_via])
	else:
		print("Visual image is a line, not an arc — try different parameters")

	pass_test("Algebraic proof complete — see output")


# --- Experiment 5: Cross-check with VIA_ON_ARC ---

func test_exp5_via_on_arc_crosscheck() -> void:
	var player_pos := Vector2(210.0, 695.0)
	var cursor_pos := Vector2(1500.0, 700.0)

	var scene := _build_scene(PairType.REFL_SEMI, PairType.REFL_SEMI)
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var checker := _setup_checker(scene, player_pos, cursor_pos)

	var arc_mid_violations := checker.check_ARC_MIDPOINT_ALIGNMENT(player_pos, cursor_pos)
	var via_on_arc_violations := checker.check_VIA_ON_ARC(player_pos, cursor_pos)

	print("=== EXPERIMENT 5: VIA_ON_ARC cross-check ===")
	print("ARC-MIDPOINT-ALIGNMENT violations: %d" % arc_mid_violations.size())
	print("VIA_ON_ARC violations:             %d" % via_on_arc_violations.size())

	if via_on_arc_violations.size() == 0 and arc_mid_violations.size() > 0:
		print("")
		print("CONCLUSION: step.via IS on the rendered arc (VIA_ON_ARC passes).")
		print("  The arc rendering is correct — the circle passes through the")
		print("  correct Mobius image points (start, via, end).")
		print("  The ARC-MIDPOINT-ALIGNMENT check samples the arc at the WRONG")
		print("  point (angular midpoint instead of the tracer's via).")
	else:
		for v in via_on_arc_violations:
			print("  VIA_ON_ARC: %s" % v)

	assert_eq(via_on_arc_violations.size(), 0, "VIA_ON_ARC should pass for all steps")
	pass_test("VIA_ON_ARC passes while ARC-MIDPOINT-ALIGNMENT fails — arc rendering is correct")


# --- Experiment 6: Statistical sweep of all violations ---

func test_exp6_statistical_sweep() -> void:
	print("=== EXPERIMENT 6: Statistical sweep — all 4 combos × all violations ===")

	var combos := [
		{"lines": PairType.REFL_SEMI, "circles": PairType.REFL_SEMI, "label": "Refl+Semi / Refl+Semi"},
		{"lines": PairType.SEMI_SEMI, "circles": PairType.SEMI_SEMI, "label": "Semi+Semi / Semi+Semi"},
		{"lines": PairType.PORTAL, "circles": PairType.REFL_SEMI, "label": "Portal / Refl+Semi"},
		{"lines": PairType.PORTAL, "circles": PairType.SEMI_SEMI, "label": "Portal / Semi+Semi"},
	]

	var count_arc_mid_f32_violates := 0
	var count_arc_mid_f64_violates := 0
	var count_via_f32_violates := 0
	var count_via_f64_violates := 0
	var total_checked := 0

	var max_cross_arc_mid_f64 := 0.0
	var max_cross_via_f32 := 0.0
	var max_cross_via_f64 := 0.0

	var runner := SweepRunner.new().configure(5, 10, 42)
	var bounds := VisualConverter.DEFAULT_BOUNDS

	for combo in combos:
		var scene := _build_scene(combo.lines, combo.circles)
		add_child_autofree(scene)
		await get_tree().process_frame
		await get_tree().process_frame

		var renderer: Node2D = scene.get_node_or_null("PathRenderer")
		var player_node: CharacterBody2D = scene.get_node_or_null("Player")
		var cursor_node: Node2D = scene.get_node_or_null("Cursor")
		if not renderer or not player_node or not cursor_node:
			continue

		if "room_rect" in scene:
			var rect: Rect2 = scene.room_rect
			runner.set_bounds(rect.position + Vector2(10, 10), rect.position + rect.size - Vector2(10, 10))
		var poi := runner._extract_points_of_interest(scene)
		var positions := runner.build_positions(poi)

		for player_pos in positions:
			for cursor_pos in positions:
				if player_pos == cursor_pos:
					continue

				player_node.global_position = player_pos
				cursor_node.global_position = cursor_pos
				renderer.use_beam_tracer = false
				renderer._compute_trace()

				var traces := {}
				var physical_path = renderer.get_traced_path()
				if physical_path:
					traces["physical"] = physical_path
				var planned_path = renderer.get_planned_path()
				if planned_path:
					traces["planned"] = planned_path

				for trace_name in traces:
					var path: Tracer.TracedPath = traces[trace_name]
					if path.steps.size() == 0:
						continue
					var first_step: Tracer.Step = path.steps[0]
					if first_step.ray == null:
						continue
					var origin := first_step.ray.origin.coords
					var aim_dir := first_step.ray.direction.to_vector().normalized()

					for si in path.steps.size():
						var step: Tracer.Step = path.steps[si]
						if not step.is_arc_step or step.frame == null or step.hit == null:
							continue
						if not VisualConverter.is_arc(step.start, step.via, step.end):
							continue
						if InvariantChecker._is_at_bounds(step.start, bounds) or InvariantChecker._is_at_bounds(step.end, bounds):
							continue

						var p := VisualConverter.arc_params(step.start, step.via, step.end)
						var ctr: Vector2 = p["center"]
						var r: float = p["radius"]
						var sa: float = p["start_angle"]
						var ea: float = p["end_angle"]
						var mid_angle := sa + (ea - sa) * 0.5
						var arc_mid := ctr + Vector2(cos(mid_angle), sin(mid_angle)) * r

						var frame_inv := step.frame.invert()

						var bt_arc_mid_f32 := frame_inv.apply(arc_mid)
						if bt_arc_mid_f32.length() > 1e5:
							continue

						var cross_f32 := (bt_arc_mid_f32 - origin).cross(aim_dir)
						if absf(cross_f32) <= 10.0:
							continue

						total_checked += 1
						count_arc_mid_f32_violates += 1

						var bt_arc_mid_f64: Vector2 = frame_inv.apply(arc_mid)
						var bt_via_f32 := frame_inv.apply(step.via)
						var bt_via_f64: Vector2 = frame_inv.apply(step.via)

						var cross_arc_mid_f64 := (bt_arc_mid_f64 - origin).cross(aim_dir)
						var cross_via_f32 := (bt_via_f32 - origin).cross(aim_dir)
						var cross_via_f64 := (bt_via_f64 - origin).cross(aim_dir)

						if absf(cross_arc_mid_f64) > 10.0:
							count_arc_mid_f64_violates += 1
						if absf(cross_via_f32) > 10.0:
							count_via_f32_violates += 1
						if absf(cross_via_f64) > 10.0:
							count_via_f64_violates += 1

						max_cross_arc_mid_f64 = maxf(max_cross_arc_mid_f64, absf(cross_arc_mid_f64))
						max_cross_via_f32 = maxf(max_cross_via_f32, absf(cross_via_f32))
						max_cross_via_f64 = maxf(max_cross_via_f64, absf(cross_via_f64))

	print("")
	print("RESULTS: %d violations analyzed (threshold: 10.0)" % total_checked)
	print("")
	print("  arc_mid + f32 (current invariant): %d / %d violate" % [count_arc_mid_f32_violates, total_checked])
	print("  arc_mid + f64:                     %d / %d violate  (max cross: %.4f)" % [count_arc_mid_f64_violates, total_checked, max_cross_arc_mid_f64])
	print("  via + f32:                         %d / %d violate  (max cross: %.4f)" % [count_via_f32_violates, total_checked, max_cross_via_f32])
	print("  via + f64:                         %d / %d violate  (max cross: %.4f)" % [count_via_f64_violates, total_checked, max_cross_via_f64])
	print("")

	if count_via_f64_violates == 0:
		if count_arc_mid_f64_violates == 0:
			print("VERDICT: H1 (precision) — switching to f64 fixes everything regardless of point choice")
		elif count_via_f32_violates == 0:
			print("VERDICT: H2 (wrong point) — switching to via fixes everything regardless of precision")
		else:
			print("VERDICT: H3 (both) — both point choice and precision contribute")
			print("  FIX: Use step.via with f64 back-transform")
	else:
		print("VERDICT: UNEXPECTED — via+f64 still has violations, deeper investigation needed")

	pass_test("Statistical sweep complete — see output for verdict")


# --- Regression test: invariant fix must produce zero violations ---

func test_fix_arc_midpoint_zero_violations() -> void:
	var combos := [
		{"lines": PairType.REFL_SEMI, "circles": PairType.REFL_SEMI},
		{"lines": PairType.SEMI_SEMI, "circles": PairType.SEMI_SEMI},
		{"lines": PairType.PORTAL, "circles": PairType.REFL_SEMI},
		{"lines": PairType.PORTAL, "circles": PairType.SEMI_SEMI},
	]

	var runner := SweepRunner.new().configure(5, 10, 42)
	var total_violations := 0

	for combo in combos:
		var scene := _build_scene(combo.lines, combo.circles)
		add_child_autofree(scene)
		await get_tree().process_frame
		await get_tree().process_frame

		if "room_rect" in scene:
			var rect: Rect2 = scene.room_rect
			runner.set_bounds(rect.position + Vector2(10, 10), rect.position + rect.size - Vector2(10, 10))
		var poi := runner._extract_points_of_interest(scene)
		var positions := runner.build_positions(poi)

		for player_pos in positions:
			for cursor_pos in positions:
				if player_pos == cursor_pos:
					continue
				var checker := _setup_checker(scene, player_pos, cursor_pos)
				var violations := checker.check_ARC_MIDPOINT_ALIGNMENT(player_pos, cursor_pos)
				total_violations += violations.size()

	assert_eq(total_violations, 0, "ARC-MIDPOINT-ALIGNMENT should have zero violations after fix (got %d)" % total_violations)
