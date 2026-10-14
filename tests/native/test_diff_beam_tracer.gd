extends GutTest
## Stage 96 differential test: NBeamTracer against BeamTracer on the sweep
## scenes, positions and plans (SweepPlans), every DIFF_STRIDE-th combo
## (default 97; DIFF_STRIDE=1 is the full sweep, minutes). Planned and
## physical TracePaths, the ladder result and the final flags must be
## identical step for step, bit for bit.

const Diff := preload("res://tests/native/diff_helpers.gd")

var _stride := 97
var _mismatches: Array = []
var _combos := 0


func before_all() -> void:
	var s := OS.get_environment("DIFF_STRIDE")
	if s.is_valid_int() and int(s) > 0:
		_stride = int(s)


func _instantiate(path: String) -> Node:
	Surface.reset_id_counter()
	BeamTracer.clear_cache()
	var scene: Node = load(path).instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	return scene


func _sweep(label: String, scene: Node, positions: Array, plans: Array, skip_equal: bool) -> void:
	var defs: Array = BeamScene.defs_from_surfaces(scene.surfaces)
	var tracer := NBeamTracer.new()
	for plan in plans:
		var idx := 0
		for p in positions:
			for c in positions:
				if skip_equal and p == c:
					continue
				var i := idx
				idx += 1
				if i % _stride != 0:
					continue
				var a := BeamTracer.trace_pair(scene.surfaces, p, c, plan, {})
				var b := tracer.trace_pair(defs, p, c, plan, {})
				_combos += 1
				var r := Diff.pair_diff(a, b, "%s plan=%s p=%s c=%s" % [label, SweepPlans.plan_to_data(plan), p, c])
				if r != "":
					_mismatches.append(r)


func _report(what: String) -> void:
	assert_gt(_combos, 0, "%s: combos compared" % what)
	assert_eq(_mismatches.size(), 0, "%s: %d of %d combos differ\n%s" % [what, _mismatches.size(), _combos, "\n".join(_mismatches.slice(0, 20))])
	if _mismatches.is_empty():
		pass_test("%s: %d combos identical" % [what, _combos])
	_mismatches.clear()
	_combos = 0


func test_all_scenes_no_plan() -> void:
	for path in SweepPlans.discover_scenes(SweepPlans.TEST_LEVELS_DIR):
		var scene := _instantiate(path)
		await get_tree().process_frame
		var runner := SweepRunner.new().configure(5, 10, 42)
		var has_room: bool = "room_rect" in scene and "build_room" in scene and scene.build_room
		var has_blocks: bool = "block_lines" in scene and scene.block_lines.size() > 0
		if has_room or has_blocks:
			var rect: Rect2 = scene.room_rect if "room_rect" in scene else Rect2(560, 240, 800, 600)
			runner.set_bounds(rect.position + Vector2(10, 10), rect.position + rect.size - Vector2(10, 10))
		var positions := runner.build_positions(runner._extract_points_of_interest(scene))
		_sweep(path.get_file().get_basename(), scene, positions, [[]], false)
	_report("all scenes")


func test_with_plans() -> void:
	for path in SweepPlans.plan_scenes():
		if not FileAccess.file_exists(path):
			continue
		var scene := _instantiate(path)
		await get_tree().process_frame
		var plans := SweepPlans.reflective_plans(scene)
		plans.append_array(SweepPlans.inversion_plans(scene))
		if plans.is_empty():
			continue
		var runner := SweepPlans.bounded_runner(scene, 5, 10, 42)
		var positions := runner.build_positions(runner._extract_points_of_interest(scene))
		_sweep(path.get_file().get_basename(), scene, positions, plans, true)
	_report("with plans")


func test_projective_plans() -> void:
	for path in SweepPlans.projective_scenes():
		var scene := _instantiate(path)
		await get_tree().process_frame
		var runner := SweepPlans.bounded_runner(scene, 4, 6, 87)
		_sweep(path.get_file().get_basename(), scene, runner.build_positions(), SweepPlans.projective_plans(scene), true)
	_report("projective plans")


func test_precision_scene() -> void:
	var grid := _instantiate(SweepPlans.PRECISION_SCENE)
	await get_tree().process_frame
	var runner := SweepRunner.new().configure(5, 10, 42)
	_sweep("precision/no_plan", grid, runner.build_positions(runner._extract_points_of_interest(grid)), [[]], true)
	_sweep("precision/plans", grid, SweepPlans.precision_positions(), SweepPlans.precision_plans(), true)
	_report("precision scene")


func test_surface_combos() -> void:
	for combo in SweepPlans.combo_configs():
		var scene := SweepPlans.build_combo_scene(combo)
		BeamTracer.clear_cache()
		add_child_autofree(scene)
		await get_tree().process_frame
		var runner := SweepRunner.new().configure(4, 6, 42)
		_sweep("combo %s" % combo["label"], scene, runner.build_positions(), [[]], true)
	_report("surface combos")


func test_plan_refuses_and_to_path() -> void:
	# ghost_room: inside a ghost stretch only ghost togglers can be planned
	var scenes := SweepPlans.plan_scenes()
	var checked := 0
	for path in scenes:
		var scene := _instantiate(path)
		await get_tree().process_frame
		var defs: Array = BeamScene.defs_from_surfaces(scene.surfaces)
		var tracer := NBeamTracer.new()
		for plan in SweepPlans.reflective_plans(scene).slice(0, 3):
			for s in scene.surfaces:
				for left in [true, false]:
					assert_eq(tracer.plan_refuses(defs, plan, s.id, left, {}), BeamTracer.plan_refuses(scene.surfaces, plan, s.id, left, {}),
						"%s plan_refuses %d %s" % [path.get_file(), s.id, left])
					checked += 1
	assert_gt(checked, 0)
