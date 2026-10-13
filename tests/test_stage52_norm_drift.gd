extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

# Visual hitpoints of the renderer path lie on original carriers (beam solver).
# --- Gradient-based carrier distance (numerically stable for all carrier types) ---

static func _carrier_dist(point: Vector2, carrier: GeneralizedCircle) -> float:
	var f := carrier.evaluate(point)
	var gx := 2.0 * carrier.a * point.x + carrier.b
	var gy := 2.0 * carrier.a * point.y + carrier.c
	var grad := sqrt(gx * gx + gy * gy)
	if grad < 1e-10:
		return INF
	return absf(f) / grad

static func _min_carrier_dist(point: Vector2, surfaces: Array) -> float:
	var min_d := INF
	for surf in surfaces:
		var s: Surface = surf
		var d := _carrier_dist(point, s.segment.get_carrier())
		if d < min_d:
			min_d = d
	return min_d

static func _is_at_bounds(p: Vector2) -> bool:
	var bounds := Rect2(0, 0, 1920, 1080)
	return (p.x <= bounds.position.x + 2.0 or p.x >= bounds.end.x - 2.0 or
		p.y <= bounds.position.y + 2.0 or p.y >= bounds.end.y - 2.0)

func test_visual_endpoints_on_original_carriers() -> void:
	var scene: Node = load("res://scenes/test_levels/three_mirrors.tscn").instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	await get_tree().process_frame
	await get_tree().process_frame

	var surfaces: Array = scene.surfaces
	var renderer := scene.get_node("PathRenderer")
	var player := scene.get_node("Player")
	var cursor := scene.get_node("Cursor")

	player.position = Vector2(1229.4, 516.7)
	cursor.position = Vector2(1000.0, 550.0)

	renderer._compute_trace()

	var path: TracePath = renderer.get_planned_path()
	assert_not_null(path, "Should have a planned path")
	assert_gt(path.steps.size(), 4, "Should have enough steps to trigger deep frames")

	var max_dist := 0.0
	var worst_step := -1
	for i in path.steps.size():
		var step: TraceStep = path.steps[i]
		if step.hit == null or step.hit.segment == null:
			continue
		var end_pos := step.end
		if is_inf(end_pos.x) or is_inf(end_pos.y):
			continue
		if _is_at_bounds(end_pos):
			continue
		var dist := _min_carrier_dist(end_pos, surfaces)
		if dist > max_dist:
			max_dist = dist
			worst_step = i
		assert_lt(dist, 5.0,
			"Step %d visual endpoint %s should be within 5px of an original carrier (got %.2f)" % [i, end_pos, dist])

	gut.p("Max carrier distance: %.4f at step %d" % [max_dist, worst_step])
