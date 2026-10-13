extends GutTest

const TEST_LEVELS_DIR := "res://scenes/test_levels/"
const PLAYGROUND_DIR := "res://scenes/playground/"
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

# Stage 83: every effect pair is beam-solved and checked
const _CHECKER_SUPPORTED := [PairType.REFL_REFL, PairType.REFL_SEMI, PairType.REFL_PROJ, PairType.REFL_DIR,
	PairType.SEMI_SEMI, PairType.SEMI_PROJ, PairType.SEMI_DIR, PairType.PROJ_PROJ, PairType.PROJ_DIR,
	PairType.DIR_DIR, PairType.PORTAL]

const _NON_PORTAL_PAIRS := [
	PairType.REFL_REFL, PairType.REFL_SEMI, PairType.REFL_PROJ, PairType.REFL_DIR,
	PairType.SEMI_SEMI, PairType.SEMI_PROJ, PairType.SEMI_DIR,
	PairType.PROJ_PROJ, PairType.PROJ_DIR, PairType.DIR_DIR,
]

const _PAIR_LABELS := {
	PairType.REFL_REFL: "Refl+Refl", PairType.REFL_SEMI: "Refl+Semi",
	PairType.REFL_PROJ: "Refl+Proj", PairType.REFL_DIR: "Refl+Dir",
	PairType.SEMI_SEMI: "Semi+Semi", PairType.SEMI_PROJ: "Semi+Proj",
	PairType.SEMI_DIR: "Semi+Dir", PairType.PROJ_PROJ: "Proj+Proj",
	PairType.PROJ_DIR: "Proj+Dir", PairType.DIR_DIR: "Dir+Dir",
	PairType.PORTAL: "Portal",
}

static func _build_combos() -> Array[Dictionary]:
	var combos: Array[Dictionary] = []
	for pair in _NON_PORTAL_PAIRS:
		combos.append({"lines": pair, "circles": pair,
			"label": "%s / %s" % [_PAIR_LABELS[pair], _PAIR_LABELS[pair]]})
	for pair in _NON_PORTAL_PAIRS:
		combos.append({"lines": PairType.PORTAL, "circles": pair,
			"label": "Portal / %s" % _PAIR_LABELS[pair]})
	for pair in _NON_PORTAL_PAIRS:
		combos.append({"lines": pair, "circles": PairType.PORTAL,
			"label": "%s / Portal" % _PAIR_LABELS[pair]})
	return combos

static var COMBOS := _build_combos()

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
		line_carrier = GeneralizedCircle.new(0,
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

# --- Scene discovery ---

func _discover_scenes(dir_path: String) -> Array[String]:
	var scenes: Array[String] = []
	var dir := DirAccess.open(dir_path)
	if dir == null:
		return scenes
	dir.list_dir_begin()
	var file_name := dir.get_next()
	while file_name != "":
		if file_name.ends_with(".tscn") and file_name != "combo_base.tscn":
			scenes.append(dir_path + file_name)
		file_name = dir.get_next()
	dir.list_dir_end()
	scenes.sort()
	return scenes

# --- No-intersection guard test ---

func test_no_intersecting_segments() -> void:
	var all_scenes: Array[String] = []
	all_scenes.append_array(_discover_scenes(TEST_LEVELS_DIR))
	all_scenes.append_array(_discover_scenes(PLAYGROUND_DIR))
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

func _apply_line_pair(scene: Node, pair_type: int) -> void:
	match pair_type:
		PairType.REFL_REFL:
			scene.mirror_both_lines = Array([LINE1, LINE2], TYPE_VECTOR4, &"", null)
		PairType.REFL_SEMI:
			scene.mirror_both_lines = Array([LINE1], TYPE_VECTOR4, &"", null)
			scene.mirror_lines = Array([LINE2], TYPE_VECTOR4, &"", null)
		PairType.REFL_PROJ:
			scene.mirror_both_lines = Array([LINE1], TYPE_VECTOR4, &"", null)
			scene.normal_projection_lines = Array([LINE2], TYPE_VECTOR4, &"", null)
		PairType.REFL_DIR:
			scene.mirror_both_lines = Array([LINE1], TYPE_VECTOR4, &"", null)
			scene.directional_projection_lines = PackedFloat64Array([
				LINE2.x, LINE2.y, LINE2.z, LINE2.w, 1, 0])
		PairType.SEMI_SEMI:
			scene.mirror_lines = Array([LINE1, LINE2], TYPE_VECTOR4, &"", null)
		PairType.SEMI_PROJ:
			scene.mirror_lines = Array([LINE1], TYPE_VECTOR4, &"", null)
			scene.normal_projection_lines = Array([LINE2], TYPE_VECTOR4, &"", null)
		PairType.SEMI_DIR:
			scene.mirror_lines = Array([LINE1], TYPE_VECTOR4, &"", null)
			scene.directional_projection_lines = PackedFloat64Array([
				LINE2.x, LINE2.y, LINE2.z, LINE2.w, 1, 0])
		PairType.PROJ_PROJ:
			scene.normal_projection_lines = Array([LINE1, LINE2], TYPE_VECTOR4, &"", null)
		PairType.PROJ_DIR:
			scene.normal_projection_lines = Array([LINE1], TYPE_VECTOR4, &"", null)
			scene.directional_projection_lines = PackedFloat64Array([
				LINE2.x, LINE2.y, LINE2.z, LINE2.w, 1, 0])
		PairType.DIR_DIR:
			scene.directional_projection_lines = PackedFloat64Array([
				LINE1.x, LINE1.y, LINE1.z, LINE1.w, 1, 0,
				LINE2.x, LINE2.y, LINE2.z, LINE2.w, 1, 0])
		PairType.PORTAL:
			scene.portal_lines = PackedFloat64Array([
				LINE1.x, LINE1.y, LINE1.z, LINE1.w, 0, 1000, 0])

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
		PairType.REFL_PROJ:
			scene.reflective_arcs = arc1
			scene.normal_projection_arcs = arc2
		PairType.REFL_DIR:
			scene.reflective_arcs = arc1
			scene.circle_directional_arcs = PackedFloat64Array([
				ARC2_START.x, ARC2_START.y, ARC2_END.x, ARC2_END.y, ARC2_VIA.x, ARC2_VIA.y, 1, 0])
		PairType.SEMI_SEMI:
			scene.semi_reflective_arcs = both
		PairType.SEMI_PROJ:
			scene.semi_reflective_arcs = arc1
			scene.normal_projection_arcs = arc2
		PairType.SEMI_DIR:
			scene.semi_reflective_arcs = arc1
			scene.circle_directional_arcs = PackedFloat64Array([
				ARC2_START.x, ARC2_START.y, ARC2_END.x, ARC2_END.y, ARC2_VIA.x, ARC2_VIA.y, 1, 0])
		PairType.PROJ_PROJ:
			scene.normal_projection_arcs = both
		PairType.PROJ_DIR:
			scene.normal_projection_arcs = arc1
			scene.circle_directional_arcs = PackedFloat64Array([
				ARC2_START.x, ARC2_START.y, ARC2_END.x, ARC2_END.y, ARC2_VIA.x, ARC2_VIA.y, 1, 0])
		PairType.DIR_DIR:
			scene.circle_directional_arcs = PackedFloat64Array([
				ARC1_START.x, ARC1_START.y, ARC1_END.x, ARC1_END.y, ARC1_VIA.x, ARC1_VIA.y, 1, 0,
				ARC2_START.x, ARC2_START.y, ARC2_END.x, ARC2_END.y, ARC2_VIA.x, ARC2_VIA.y, 1, 0])
		PairType.PORTAL:
			scene.portal_arcs = PackedFloat64Array([
				ARC1_START.x, ARC1_START.y, ARC1_END.x, ARC1_END.y, ARC1_VIA.x, ARC1_VIA.y, 0, 500, 0])

func _build_combo_scene(combo: Dictionary) -> Node:
	Surface.reset_id_counter()
	var scene: Node = load(COMBO_BASE).instantiate()
	scene.gravity = Vector2.ZERO
	_apply_line_pair(scene, combo.lines)
	_apply_circle_pair(scene, combo.circles)
	return scene

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

		var checker := InvariantChecker.new()
		checker.setup(scene)

		var runner := SweepRunner.new().configure(5, 10, 42)
		if "room_rect" in scene:
			var rect: Rect2 = scene.room_rect
			runner.set_bounds(rect.position + Vector2(10, 10), rect.position + rect.size - Vector2(10, 10))
		var poi := runner._extract_points_of_interest(scene)
		var positions := runner.build_positions(poi)

		var plans := _generate_combo_plans(scene, combo)

		for plan in plans:
			for player_pos in positions:
				for cursor_pos in positions:
					if player_pos == cursor_pos:
						continue
					var violations := checker.check_all(player_pos, cursor_pos, plan)
					total_combos += 1
					if violations.size() > 0:
						for v: String in violations:
							total_failures.append({
								"scene": COMBO_BASE,
								"combo": {"label": label, "lines": combo.lines, "circles": combo.circles},
								"player_pos": player_pos,
								"cursor_pos": cursor_pos,
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
