class_name SweepPlans
extends RefCounted

# The scenes, plans and positions the invariant sweeps check, in one place:
# tests/test_invariant_sweep.gd, tests/test_invariant_combos.gd,
# tests/test_precision_invariant_sweep.gd, scripts/testing/violation_viewer.gd
# and tools/dump_goldens.gd all read the same definitions, so the goldens pin
# exactly what the suite sweeps.

const TEST_LEVELS_DIR := "res://scenes/test_levels/"
const PLAYGROUND_DIR := "res://scenes/playground/"
const COMBO_BASE := "res://scenes/test_levels/combo_base.tscn"

# ---------------------------------------------------------------- scenes

static func discover_scenes(dir_path: String, exclude: Array = []) -> Array[String]:
	var scenes: Array[String] = []
	var dir := DirAccess.open(dir_path)
	if dir == null:
		return scenes
	for fn in dir.get_files():
		if fn.ends_with(".tscn") and not fn in exclude:
			scenes.append(dir_path + fn)
	scenes.sort()
	return scenes

# test_sweep_with_plans: the scenes swept with mirror / inversion plans
static func plan_scenes() -> Array[String]:
	var out: Array[String] = []
	for n in ["three_mirrors", "mirror_and_wall", "parallel_mirrors", "divergence_room", "room_with_mirror",
			"two_mirrors", "room_with_inversion", "full_circle_mirror"]:
		out.append(TEST_LEVELS_DIR + n + ".tscn")
	return out

# test_sweep_projective_plans: the projective playgrounds
static func projective_scenes() -> Array[String]:
	var out: Array[String] = []
	for n in ["projective", "projective_lines", "projective_arcs", "directional_lines", "directional_arcs"]:
		out.append(PLAYGROUND_DIR + n + ".tscn")
	return out

const PRECISION_SCENE := "res://scenes/playground/inversion_grid.tscn"

# ---------------------------------------------------------------- plans

static func _sides(scene: Node, effect_test: Callable) -> Array:
	var out: Array = []
	if not "surfaces" in scene:
		return out
	for surf in scene.surfaces:
		var s: Surface = surf
		var left: SideConfig = s.active_side_config(Side.Value.LEFT, GameState.new())
		var right: SideConfig = s.active_side_config(Side.Value.RIGHT, GameState.new())
		var l: bool = left != null and effect_test.call(left.effect)
		var r: bool = right != null and effect_test.call(right.effect)
		if l or r:
			out.append({"surface": s, "left": l, "right": r})
	return out

static func _entry(m: Dictionary) -> PlanManager.PlanEntry:
	var s: Surface = m.surface
	return PlanManager.PlanEntry.new(s.id, Side.Value.LEFT if m.left else Side.Value.RIGHT)

# Mirror plans of a scene: each reflective side alone; the first two mirrors;
# the first mirror twice; the first three; A-B-A.
static func reflective_plans(scene: Node) -> Array:
	var mirrors := _sides(scene, func(e): return e is ReflectionEffect)
	var plans: Array = []
	for m in mirrors:
		var s: Surface = m.surface
		if m.left:
			plans.append([PlanManager.PlanEntry.new(s.id, Side.Value.LEFT)])
		if m.right:
			plans.append([PlanManager.PlanEntry.new(s.id, Side.Value.RIGHT)])
	if mirrors.size() >= 2:
		plans.append([_entry(mirrors[0]), _entry(mirrors[1])])
	if mirrors.size() >= 1:
		plans.append([_entry(mirrors[0]), _entry(mirrors[0])])
	if mirrors.size() >= 3:
		plans.append([_entry(mirrors[0]), _entry(mirrors[1]), _entry(mirrors[2])])
	if mirrors.size() >= 2:
		plans.append([_entry(mirrors[0]), _entry(mirrors[1]), _entry(mirrors[0])])
	return plans

# Inversion plans: each inverting side alone, and the first inversion twice.
static func inversion_plans(scene: Node) -> Array:
	var inversions := _sides(scene, func(e): return e is CircleInversionEffect)
	var plans: Array = []
	for inv in inversions:
		var s: Surface = inv.surface
		if inv.left:
			plans.append([PlanManager.PlanEntry.new(s.id, Side.Value.LEFT)])
		if inv.right:
			plans.append([PlanManager.PlanEntry.new(s.id, Side.Value.RIGHT)])
	if inversions.size() >= 1:
		plans.append([_entry(inversions[0]), _entry(inversions[0])])
	return plans

# Projective plans (Stage 87): each projective side as a one-entry plan, and
# the first two projectives as a two-entry plan.
static func projective_surfaces(scene: Node) -> Array:
	var projs: Array = []
	for s: Surface in scene.surfaces:
		var c: SideConfig = s.active_side_config(Side.Value.LEFT, GameState.new())
		if c != null and c.effect != null and c.effect.kind() == Effect.Kind.PROJECTIVE:
			projs.append(s)
	return projs

static func projective_plans(scene: Node) -> Array:
	var projs := projective_surfaces(scene)
	var plans: Array = []
	for s: Surface in projs:
		plans.append([PlanManager.PlanEntry.new(s.id, Side.Value.LEFT)])
		plans.append([PlanManager.PlanEntry.new(s.id, Side.Value.RIGHT)])
	if projs.size() >= 2:
		plans.append([PlanManager.PlanEntry.new(projs[0].id, Side.Value.LEFT), PlanManager.PlanEntry.new(projs[1].id, Side.Value.LEFT)])
	return plans

# The precision sweep's plans on the inversion grid (surface ids, LEFT sides)
static func precision_plan_ids() -> Array:
	return [[7], [4, 5], [7, 4, 1], [5, 5], [1, 2, 3, 6], [7, 4, 1, 3, 6, 6, 6]]

static func precision_plans() -> Array:
	var out: Array = []
	for ids in precision_plan_ids():
		out.append(ids.map(func(i): return PlanManager.PlanEntry.new(i, Side.Value.LEFT)))
	return out

# The precision sweep's 10 seeded positions outside the grid's circles
static func precision_positions() -> Array[Vector2]:
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	var positions: Array[Vector2] = []
	while positions.size() < 10:
		var p := Vector2(rng.randf_range(40, 1880), rng.randf_range(40, 1040))
		var inside := false
		for cx in [760, 960, 1160]:
			for cy in [380, 540, 700]:
				if p.distance_to(Vector2(cx, cy)) < 62:
					inside = true
		if not inside:
			positions.append(p)
	return positions

# Plan sweep bounds: inside the room rect by 10 px (the scenes with a room)
static func bounded_runner(scene: Node, grid: int, fuzz: int, seed: int) -> SweepRunner:
	var runner := SweepRunner.new().configure(grid, fuzz, seed)
	if "room_rect" in scene:
		var rect: Rect2 = scene.room_rect
		runner.set_bounds(rect.position + Vector2(10, 10), rect.position + rect.size - Vector2(10, 10))
	return runner

static func plan_to_data(plan: Array) -> Array:
	var result: Array = []
	for entry in plan:
		var e: PlanManager.PlanEntry = entry
		result.append({"surface_id": e.surface_id, "side": e.side})
	return result

# ---------------------------------------------------------------- combos

const LINE1 := Vector4(600, 300, 600, 700)
const LINE2 := Vector4(1300, 300, 1300, 700)
const ARC1_START := Vector2(400, 250)
const ARC1_END := Vector2(300, 250)
const ARC1_VIA := Vector2(350, 200)
const ARC2_START := Vector2(1550, 750)
const ARC2_END := Vector2(1450, 750)
const ARC2_VIA := Vector2(1500, 700)

enum PairType { REFL_REFL, REFL_SEMI, REFL_PROJ, REFL_DIR, SEMI_SEMI, SEMI_PROJ, SEMI_DIR, PROJ_PROJ, PROJ_DIR, DIR_DIR, PORTAL }

const NON_PORTAL_PAIRS := [
	PairType.REFL_REFL, PairType.REFL_SEMI, PairType.REFL_PROJ, PairType.REFL_DIR,
	PairType.SEMI_SEMI, PairType.SEMI_PROJ, PairType.SEMI_DIR,
	PairType.PROJ_PROJ, PairType.PROJ_DIR, PairType.DIR_DIR,
]

const PAIR_LABELS := {
	PairType.REFL_REFL: "Refl+Refl", PairType.REFL_SEMI: "Refl+Semi",
	PairType.REFL_PROJ: "Refl+Proj", PairType.REFL_DIR: "Refl+Dir",
	PairType.SEMI_SEMI: "Semi+Semi", PairType.SEMI_PROJ: "Semi+Proj",
	PairType.SEMI_DIR: "Semi+Dir", PairType.PROJ_PROJ: "Proj+Proj",
	PairType.PROJ_DIR: "Proj+Dir", PairType.DIR_DIR: "Dir+Dir",
	PairType.PORTAL: "Portal",
}

# The 30 combo configurations: every non-portal pair on lines and circles,
# then portal lines with every circle pair, then every line pair with portal arcs.
static func combo_configs() -> Array[Dictionary]:
	var combos: Array[Dictionary] = []
	for pair in NON_PORTAL_PAIRS:
		combos.append({"lines": pair, "circles": pair, "label": "%s / %s" % [PAIR_LABELS[pair], PAIR_LABELS[pair]]})
	for pair in NON_PORTAL_PAIRS:
		combos.append({"lines": PairType.PORTAL, "circles": pair, "label": "Portal / %s" % PAIR_LABELS[pair]})
	for pair in NON_PORTAL_PAIRS:
		combos.append({"lines": pair, "circles": PairType.PORTAL, "label": "%s / Portal" % PAIR_LABELS[pair]})
	return combos

static func apply_line_pair(scene: Node, pair_type: int) -> void:
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
			scene.directional_projection_lines = PackedFloat64Array([LINE2.x, LINE2.y, LINE2.z, LINE2.w, 1, 0])
		PairType.SEMI_SEMI:
			scene.mirror_lines = Array([LINE1, LINE2], TYPE_VECTOR4, &"", null)
		PairType.SEMI_PROJ:
			scene.mirror_lines = Array([LINE1], TYPE_VECTOR4, &"", null)
			scene.normal_projection_lines = Array([LINE2], TYPE_VECTOR4, &"", null)
		PairType.SEMI_DIR:
			scene.mirror_lines = Array([LINE1], TYPE_VECTOR4, &"", null)
			scene.directional_projection_lines = PackedFloat64Array([LINE2.x, LINE2.y, LINE2.z, LINE2.w, 1, 0])
		PairType.PROJ_PROJ:
			scene.normal_projection_lines = Array([LINE1, LINE2], TYPE_VECTOR4, &"", null)
		PairType.PROJ_DIR:
			scene.normal_projection_lines = Array([LINE1], TYPE_VECTOR4, &"", null)
			scene.directional_projection_lines = PackedFloat64Array([LINE2.x, LINE2.y, LINE2.z, LINE2.w, 1, 0])
		PairType.DIR_DIR:
			scene.directional_projection_lines = PackedFloat64Array([
				LINE1.x, LINE1.y, LINE1.z, LINE1.w, 1, 0,
				LINE2.x, LINE2.y, LINE2.z, LINE2.w, 1, 0])
		PairType.PORTAL:
			scene.portal_lines = PackedFloat64Array([LINE1.x, LINE1.y, LINE1.z, LINE1.w, 0, 1000, 0])

static func apply_circle_pair(scene: Node, pair_type: int) -> void:
	var arc1 := PackedFloat64Array([ARC1_START.x, ARC1_START.y, ARC1_END.x, ARC1_END.y, ARC1_VIA.x, ARC1_VIA.y])
	var arc2 := PackedFloat64Array([ARC2_START.x, ARC2_START.y, ARC2_END.x, ARC2_END.y, ARC2_VIA.x, ARC2_VIA.y])
	var both := PackedFloat64Array(arc1)
	both.append_array(arc2)
	var dir2 := PackedFloat64Array([ARC2_START.x, ARC2_START.y, ARC2_END.x, ARC2_END.y, ARC2_VIA.x, ARC2_VIA.y, 1, 0])
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
			scene.circle_directional_arcs = dir2
		PairType.SEMI_SEMI:
			scene.semi_reflective_arcs = both
		PairType.SEMI_PROJ:
			scene.semi_reflective_arcs = arc1
			scene.normal_projection_arcs = arc2
		PairType.SEMI_DIR:
			scene.semi_reflective_arcs = arc1
			scene.circle_directional_arcs = dir2
		PairType.PROJ_PROJ:
			scene.normal_projection_arcs = both
		PairType.PROJ_DIR:
			scene.normal_projection_arcs = arc1
			scene.circle_directional_arcs = dir2
		PairType.DIR_DIR:
			scene.circle_directional_arcs = PackedFloat64Array([
				ARC1_START.x, ARC1_START.y, ARC1_END.x, ARC1_END.y, ARC1_VIA.x, ARC1_VIA.y, 1, 0,
				ARC2_START.x, ARC2_START.y, ARC2_END.x, ARC2_END.y, ARC2_VIA.x, ARC2_VIA.y, 1, 0])
		PairType.PORTAL:
			scene.portal_arcs = PackedFloat64Array([ARC1_START.x, ARC1_START.y, ARC1_END.x, ARC1_END.y, ARC1_VIA.x, ARC1_VIA.y, 0, 500, 0])

# A combo_base scene configured for `combo` (not yet in the tree: the
# surfaces are built by LevelSettings._ready).
static func build_combo_scene(combo: Dictionary) -> Node:
	Surface.reset_id_counter()
	var scene: Node = load(COMBO_BASE).instantiate()
	scene.gravity = Vector2.ZERO
	apply_line_pair(scene, combo.lines)
	apply_circle_pair(scene, combo.circles)
	return scene
