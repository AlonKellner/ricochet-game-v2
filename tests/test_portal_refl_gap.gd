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
	var scene: Node = load(COMBO_BASE).instantiate()
	scene.gravity = Vector2.ZERO
	scene.portal_lines = PackedFloat64Array([
		LINE1.x, LINE1.y, LINE1.z, LINE1.w, 0, 1000, 0])
	scene.reflective_arcs = PackedFloat64Array([
		ARC1_START.x, ARC1_START.y, ARC1_END.x, ARC1_END.y, ARC1_VIA.x, ARC1_VIA.y,
		ARC2_START.x, ARC2_START.y, ARC2_END.x, ARC2_END.y, ARC2_VIA.x, ARC2_VIA.y])
	return scene

func _trace_via_renderer(scene: Node) -> TracePath:
	var player := scene.get_node("Player")
	var cursor := scene.get_node("Cursor")
	var renderer := scene.get_node("PathRenderer")
	player.global_position = PLAYER
	cursor.global_position = CURSOR
	renderer._compute_trace()
	return renderer.get_traced_path()

func test_no_non_portal_large_gaps() -> void:
	var scene := _build_scene()
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var rendered_path := _trace_via_renderer(scene)
	assert_gt(rendered_path.steps.size(), 0, "Rendered trace has steps")

	var rendered_gaps := _find_large_gaps(rendered_path, 5.0)
	var non_portal_rendered := rendered_gaps.filter(func(g): return not g["after_portal"])

	assert_eq(non_portal_rendered.size(), 0,
		"Rendered trace should have no non-portal gaps > 5px")

func _find_large_gaps(path: TracePath, threshold: float) -> Array[Dictionary]:
	var gaps: Array[Dictionary] = []
	for i in range(1, path.steps.size()):
		var prev: TraceStep = path.steps[i - 1]
		var curr: TraceStep = path.steps[i]
		# beam: steps through infinity join at Vector2(INF, INF) (continuous)
		if is_inf(prev.end.x) and is_inf(curr.start.x):
			continue
		var gap := prev.end.distance_to(curr.start)
		if gap > threshold:
			gaps.append({
				"index": i,
				"gap": gap,
				"prev_end": prev.end,
				"curr_start": curr.start,
				"after_portal": curr.after_portal,
			})
	return gaps


func test_trace_ends_on_wall_carrier() -> void:
	var scene := _build_scene()
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var surfaces: Array[Surface] = scene.surfaces
	var rendered_path := _trace_via_renderer(scene)

	var last: TraceStep = rendered_path.steps[rendered_path.steps.size() - 1]
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
