extends GutTest

const TEST_LEVELS_DIR := "res://scenes/test_levels/"
const PLAYGROUND_DIR := "res://scenes/playground/"
const COMBO_BASE := "res://scenes/test_levels/combo_base.tscn"
var VIOLATIONS_PATH := SweepRunner.violations_path("combos")

# Stage 83: every effect pair is beam-solved and checked
const _CHECKER_SUPPORTED := [SweepPlans.PairType.REFL_REFL, SweepPlans.PairType.REFL_SEMI, SweepPlans.PairType.REFL_PROJ,
	SweepPlans.PairType.REFL_DIR, SweepPlans.PairType.SEMI_SEMI, SweepPlans.PairType.SEMI_PROJ, SweepPlans.PairType.SEMI_DIR,
	SweepPlans.PairType.PROJ_PROJ, SweepPlans.PairType.PROJ_DIR, SweepPlans.PairType.DIR_DIR, SweepPlans.PairType.PORTAL]

# the 30 combo configurations (SweepPlans.combo_configs)
static var COMBOS := SweepPlans.combo_configs()

static func _is_checker_supported(combo: Dictionary) -> bool:
	return combo.lines in _CHECKER_SUPPORTED and combo.circles in _CHECKER_SUPPORTED

# --- Segment intersection detection ---

static func _carrier_intersect_points(c1: GeneralizedCircle, c2: GeneralizedCircle) -> Array[Vector2]:
	var results: Array[Vector2] = []

	if c1.is_line() and c2.is_line():
		var det := c1.b * c2.c - c2.b * c1.c
		if absf(det) < 1e-10:
			return results
		var x := (c1.c * c2.d - c2.c * c1.d) / det
		var y := (c2.b * c1.d - c1.b * c2.d) / det
		results.append(Vector2(x, y))
		return results

	var line_carrier: GeneralizedCircle
	var other_carrier: GeneralizedCircle

	if c1.is_line() or c2.is_line():
		line_carrier = c1 if c1.is_line() else c2
		other_carrier = c2 if c1.is_line() else c1
	else:
		line_carrier = GeneralizedCircle.make(0,
			c1.b - c2.b, c1.c - c2.c, c1.d - c2.d)
		other_carrier = c1

	var dir := Vector2(-line_carrier.c, line_carrier.b)
	if dir.length_squared() < 1e-20:
		return results
	var origin: Vector2
	if absf(line_carrier.c) > absf(line_carrier.b):
		origin = Vector2(0, -line_carrier.d / line_carrier.c)
	else:
		origin = Vector2(-line_carrier.d / line_carrier.b, 0)

	# z = origin + t*dir substituted into a|z|^2 + b x + c y + d = 0
	var a := other_carrier.a
	var qa := a * dir.length_squared()
	var qb := 2.0 * a * origin.dot(dir) + other_carrier.b * dir.x + other_carrier.c * dir.y
	var qc := a * origin.length_squared() + other_carrier.b * origin.x + other_carrier.c * origin.y + other_carrier.d
	var ts: Array[float] = []
	if qa == 0.0:
		if qb != 0.0:
			ts.append(-qc / qb)
	else:
		var disc := qb * qb - 4.0 * qa * qc
		if disc == 0.0:
			ts.append(-qb / (2.0 * qa))
		elif disc > 0.0:
			ts.append((-qb - sqrt(disc)) / (2.0 * qa))
			ts.append((-qb + sqrt(disc)) / (2.0 * qa))
	for t in ts:
		results.append(origin + t * dir)
	return results

static func _is_endpoint(point: Vector2, seg: Segment) -> bool:
	if seg.full:
		return false
	return point.distance_to(seg.start.coords) < 1.0 or point.distance_to(seg.end.coords) < 1.0

static func _on_segment(p: Vector2, seg: Segment) -> bool:
	var ctx := NumCtx.f64()
	return BeamGeom.on_segment(BeamGeom.seg_from_segment(ctx, seg, 0), ctx.cn(p.x, p.y))

static func _segments_intersect(seg1: Segment, seg2: Segment) -> bool:
	var c1 := seg1.get_carrier()
	var c2 := seg2.get_carrier()
	if c1.same_circle(c2):
		return false
	var points := _carrier_intersect_points(c1, c2)
	for p in points:
		if _is_endpoint(p, seg1) or _is_endpoint(p, seg2):
			continue
		if _on_segment(p, seg1) and _on_segment(p, seg2):
			return true
	return false

static func _check_no_intersections(surfaces: Array, verbose: bool = false) -> Array[String]:
	var violations: Array[String] = []
	for i in range(surfaces.size()):
		for j in range(i + 1, surfaces.size()):
			var s1: Surface = surfaces[i]
			var s2: Surface = surfaces[j]
			if _segments_intersect(s1.segment, s2.segment):
				var msg := "Segments %d and %d intersect" % [s1.id, s2.id]
				if verbose:
					msg += " [s%d: %s->%s via %s carrier_a=%.4f | s%d: %s->%s via %s carrier_a=%.4f]" % [
						s1.id, s1.segment.start.coords, s1.segment.end.coords, s1.segment.via.coords, s1.segment.get_carrier().a,
						s2.id, s2.segment.start.coords, s2.segment.end.coords, s2.segment.via.coords, s2.segment.get_carrier().a]
				violations.append(msg)
	return violations

# --- No-intersection guard test ---

func test_no_intersecting_segments() -> void:
	var all_scenes: Array[String] = []
	all_scenes.append_array(SweepPlans.discover_scenes(TEST_LEVELS_DIR, ["combo_base.tscn"]))
	all_scenes.append_array(SweepPlans.discover_scenes(PLAYGROUND_DIR, ["combo_base.tscn"]))
	assert_gt(all_scenes.size(), 0, "Should find scenes")

	var all_violations: Array[String] = []

	for scene_path in all_scenes:
		Surface.reset_id_counter()
		var scene: Node = load(scene_path).instantiate()
		scene.gravity = Vector2.ZERO
		add_child_autofree(scene)

		if "surfaces" in scene:
			var violations := _check_no_intersections(scene.surfaces)
			for v in violations:
				all_violations.append("[%s] %s" % [scene_path.get_file(), v])

	if all_violations.size() > 0:
		var report := "Intersecting segments found (%d):\n" % all_violations.size()
		for v in all_violations:
			report += "  %s\n" % v
		fail_test(report)
	else:
		pass_test("No intersecting segments in %d scenes" % all_scenes.size())

# --- Combo scene building ---

func _build_combo_scene(combo: Dictionary) -> Node:
	return SweepPlans.build_combo_scene(combo)

func _generate_combo_plans(_scene: Node, _combo: Dictionary) -> Array:
	return [[]]

# --- 18-combination sweep test ---

func test_sweep_surface_combos() -> void:
	var total_failures: Array[Dictionary] = []
	var total_combos := 0
	for combo in COMBOS:
		var label: String = combo.label
		var supported := _is_checker_supported(combo)
		var scene := _build_combo_scene(combo)
		add_child_autofree(scene)
		await get_tree().process_frame
		await get_tree().process_frame

		if "surfaces" in scene:
			var seg_violations := _check_no_intersections(scene.surfaces)
			for v in seg_violations:
				total_failures.append({
					"scene": COMBO_BASE,
					"combo": {"label": label, "lines": combo.lines, "circles": combo.circles},
					"player_pos": Vector2.ZERO,
					"cursor_pos": Vector2.ZERO,
					"plan": [],
					"violation": "SEGMENT_INTERSECTION: " + v,
				})

		if not supported:
			continue

		var runner := SweepRunner.new().configure(5, 10, 42)
		if "room_rect" in scene:
			var rect: Rect2 = scene.room_rect
			runner.set_bounds(rect.position + Vector2(10, 10), rect.position + rect.size - Vector2(10, 10))
		var poi := runner._extract_points_of_interest(scene)
		var positions := runner.build_positions(poi)

		var plans := _generate_combo_plans(scene, combo)
		total_combos += plans.size() * SweepRunner.combo_count(positions, true)
		var failing := SweepRunner.sweep_native(scene, positions, plans, true)
		# one combo through the live PathRenderer: the driver path the game runs
		var checker := InvariantChecker.new()
		checker.setup(scene)
		if plans.size() > 0 and positions.size() > 1:
			var live := checker.check_all(positions[0], positions[1], plans[0])
			if live.size() > 0:
				failing.append({"player_pos": positions[0], "cursor_pos": positions[1], "plan_index": 0,
					"violations": live.map(func(v): return "LIVE " + v)})
		for f in failing:
			for v: String in f.violations:
				total_failures.append({
					"scene": COMBO_BASE,
					"combo": {"label": label, "lines": combo.lines, "circles": combo.circles},
					"player_pos": f.player_pos,
					"cursor_pos": f.cursor_pos,
					"plan": [],
					"violation": v,
				})

	_save_violations(total_failures)
	if total_failures.size() > 0:
		var report := "Combo sweep violations (%d):\n" % total_failures.size()
		for f in total_failures.slice(0, 10):
			var display_name: String = f.combo.label if "combo" in f else f.scene
			report += "  [%s] player=%s cursor=%s: %s\n" % [
				display_name, f.player_pos, f.cursor_pos, f.violation]
		if total_failures.size() > 10:
			report += "  ... and %d more\n" % (total_failures.size() - 10)
		fail_test(report)
	else:
		var skipped := COMBOS.size() - COMBOS.filter(_is_checker_supported).size()
		pass_test("Combo sweep passed: %d combos across %d combinations (%d skipped: checker lacks proj/dir support)" % [total_combos, COMBOS.size(), skipped])

func _save_violations(failures: Array) -> void:
	var entries: Array = []
	for f in failures:
		var entry := {
			"scene": f.scene,
			"player_pos": [f.player_pos.x, f.player_pos.y],
			"cursor_pos": [f.cursor_pos.x, f.cursor_pos.y],
			"plan": f.plan,
			"violation": f.violation,
		}
		if "combo" in f:
			entry["combo"] = f.combo
		entries.append(entry)
	var file := FileAccess.open(VIOLATIONS_PATH, FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(entries, "  "))
		file.close()
		print("[Sweep] Saved %d violations to %s" % [entries.size(), VIOLATIONS_PATH])
