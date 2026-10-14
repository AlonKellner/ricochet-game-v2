extends GutTest
## Stages 90-94 differential test: the native solver stack (NNumCtx, NBeamScene,
## NBeamSolver, NFastSolver) against the GDScript stack on every solver fixture,
## bit-identical at every tier: F64, 4 limbs, and the float64 walk with gains
## (FastSolver = the Dual tier). The GDScript side is the reference (TDD_10, 2.2).

const Diff := preload("res://tests/native/diff_helpers.gd")
const FIXTURES := ["stage81_rules", "stage82_portals", "stage83_projective", "stage85_flags",
	"stage86_ghost", "stage87_click_forks", "stage87_projective_plans"]
const LIMBS := 4

var _fixtures: Array = [] # Dictionary fixtures, each with "file"


func before_all() -> void:
	for name in FIXTURES:
		var f := FileAccess.open("res://bench/fixtures/%s.json" % name, FileAccess.READ)
		for fx in JSON.parse_string(f.get_as_text())["fixtures"]:
			fx["file"] = name
			_fixtures.append(fx)


static func plan_of(j: Array) -> Array:
	var out: Array = []
	for e in j:
		var c = e.get("click")
		out.append(BeamSolver.PlanItem.make(int(e["surface_id"]), e["side"] == "LEFT",
			null if c == null else Vector2(c[0], c[1])))
	return out


static func flags_of(fx: Dictionary) -> Dictionary:
	var f = fx.get("flags")
	return {} if f == null else f


static func label(fx: Dictionary) -> String:
	return "%s/%s" % [fx["file"], fx["name"]]


# ---------------------------------------------------------------- walks

func _gd_walk(fx: Dictionary, ctx: NumCtx) -> Array:
	var scene := BeamScene.from_defs(ctx, fx["surfaces"])
	var player := ctx.cn(fx["player"][0], fx["player"][1])
	var cursor = null if fx["cursor"] == null else ctx.cn(fx["cursor"][0], fx["cursor"][1])
	var plan := plan_of(fx["plan"])
	var flags := flags_of(fx)
	var mode: int = BeamSolver.Mode.PHYSICAL if fx["mode"] == "physical" else BeamSolver.Mode.PLANNED
	if fx["direction"] != null:
		var d := ctx.cn(fx["direction"][0], fx["direction"][1])
		return BeamSolver.trace_direction(ctx, scene, player, d, plan, cursor, mode, BeamSolver.MAX_HITS, flags)
	if mode == BeamSolver.Mode.PLANNED:
		return BeamSolver.planned(ctx, scene, player, cursor, plan, BeamSolver.MAX_HITS, flags)
	return BeamSolver.physical(ctx, scene, player, cursor, plan, BeamSolver.MAX_HITS, flags)


func _n_walk(fx: Dictionary, ctx: NNumCtx) -> Array:
	var scene := NBeamScene.from_defs(ctx, fx["surfaces"])
	var player := ctx.cn(fx["player"][0], fx["player"][1])
	var cursor = null if fx["cursor"] == null else ctx.cn(fx["cursor"][0], fx["cursor"][1])
	var plan := plan_of(fx["plan"])
	var flags := flags_of(fx)
	var mode: int = NBeamSolver.PHYSICAL if fx["mode"] == "physical" else NBeamSolver.PLANNED
	if fx["direction"] != null:
		var d := ctx.cn(fx["direction"][0], fx["direction"][1])
		return NBeamSolver.trace_direction(ctx, scene, player, d, plan, cursor, mode, NBeamSolver.MAX_HITS, flags)
	if mode == NBeamSolver.PLANNED:
		return NBeamSolver.planned(ctx, scene, player, cursor, plan, NBeamSolver.MAX_HITS, flags)
	return NBeamSolver.physical(ctx, scene, player, cursor, plan, NBeamSolver.MAX_HITS, flags)


static func _pair(a) -> PackedFloat64Array:
	return PackedFloat64Array([float(a[0]), float(a[1])])


func _gd_fast(fx: Dictionary) -> FastSolver.Out:
	var sc := FastSolver.Scene.from_defs(fx["surfaces"])
	var plan := plan_of(fx["plan"])
	var flags := flags_of(fx)
	var cursor = null if fx["cursor"] == null else _pair(fx["cursor"])
	var mode: int = BeamSolver.Mode.PHYSICAL if fx["mode"] == "physical" else BeamSolver.Mode.PLANNED
	if fx["direction"] != null:
		return FastSolver.trace_direction(sc, _pair(fx["player"]), _pair(fx["direction"]), plan, cursor, mode, BeamSolver.MAX_HITS, flags)
	if mode == BeamSolver.Mode.PLANNED:
		return FastSolver.planned(sc, _pair(fx["player"]), cursor, plan, BeamSolver.MAX_HITS, flags)
	return FastSolver.physical(sc, _pair(fx["player"]), cursor, plan, BeamSolver.MAX_HITS, flags)


func _n_fast(fx: Dictionary) -> NFastSolverOut:
	var sc := NFastSolverScene.from_defs(fx["surfaces"])
	var plan := plan_of(fx["plan"])
	var flags := flags_of(fx)
	var cursor = null if fx["cursor"] == null else _pair(fx["cursor"])
	var mode: int = NBeamSolver.PHYSICAL if fx["mode"] == "physical" else NBeamSolver.PLANNED
	if fx["direction"] != null:
		return NFastSolver.trace_direction(sc, _pair(fx["player"]), _pair(fx["direction"]), plan, cursor, mode, NBeamSolver.MAX_HITS, flags)
	if mode == NBeamSolver.PLANNED:
		return NFastSolver.planned(sc, _pair(fx["player"]), cursor, plan, NBeamSolver.MAX_HITS, flags)
	return NFastSolver.physical(sc, _pair(fx["player"]), cursor, plan, NBeamSolver.MAX_HITS, flags)


# ---------------------------------------------------------------- tests

func test_fixtures_loaded() -> void:
	assert_gt(_fixtures.size(), 100, "fixtures found")


func test_native_classes_registered() -> void:
	for c in ["NNumCtx", "NNum", "NCN", "NBigFloat", "NBeamScene", "NBeamSceneSurf", "NHPBeam",
			"NBeamSolverPlanItem", "NBeamSolverEvent", "NBeamSolver", "NFastSolverScene", "NFastSolverOut", "NFastSolver"]:
		assert_true(ClassDB.class_exists(c), "%s registered" % c)
	assert_eq(NBeamSolver.MAX_HITS, BeamSolver.MAX_HITS)
	assert_eq(NBeamSolver.PLANNED, BeamSolver.Mode.PLANNED)
	assert_eq(NBeamSolver.PHYSICAL, BeamSolver.Mode.PHYSICAL)


func test_scene_surfaces_identical() -> void:
	var n := 0
	for fx in _fixtures:
		var gd := BeamScene.from_defs(NumCtx.f64(), fx["surfaces"])
		var nt := NBeamScene.from_defs(NNumCtx.f64(), fx["surfaces"])
		assert_eq(nt.surfaces.size(), gd.surfaces.size(), "%s: surface count" % label(fx))
		for i in mini(gd.surfaces.size(), nt.surfaces.size()):
			var a: BeamScene.Surf = gd.surfaces[i]
			var b: NBeamSceneSurf = nt.surfaces[i]
			for f in ["id", "line", "full", "portal", "pair", "has_v", "is_target", "left_type", "right_type", "left_proj", "right_proj"]:
				if a.get(f) != b.get(f):
					fail_test("%s surf %d.%s: %s vs %s" % [label(fx), a.id, f, a.get(f), b.get(f)])
			for f in ["sx", "sy", "ex", "ey", "cx", "cy", "r", "r2", "theta", "dx", "dy", "pa", "pb", "vx", "vy"]:
				if not Diff.same_float(a.get(f), b.get(f)):
					fail_test("%s surf %d.%s: %s vs %s" % [label(fx), a.id, f, Diff.hex(a.get(f)), Diff.hex(b.get(f))])
			var r := Diff.cn_diff(a.alpha, b.alpha, "%s surf %d.alpha" % [label(fx), a.id])
			if r == "":
				r = Diff.cn_diff(a.dvec, b.dvec, "%s surf %d.dvec" % [label(fx), a.id])
			if r != "":
				fail_test(r)
			var flags := flags_of(fx)
			for left in [true, false]:
				assert_eq(b.effect(left, flags), a.effect(left, flags), "%s surf %d effect" % [label(fx), a.id])
				assert_eq(b.ghosted(left, flags), a.ghosted(left, flags), "%s surf %d ghosted" % [label(fx), a.id])
			n += 1
	assert_gt(n, 300, "surfaces compared")


func _compare_tier(gd_ctx: NumCtx, n_ctx: NNumCtx) -> void:
	var mismatches: Array = []
	for fx in _fixtures:
		var r := Diff.events_diff(_gd_walk(fx, gd_ctx), _n_walk(fx, n_ctx), label(fx))
		if r != "":
			mismatches.append(r)
	assert_eq(mismatches.size(), 0, "%s: %d fixtures differ\n%s" % [gd_ctx.name(), mismatches.size(), "\n".join(mismatches)])


func test_fixtures_f64() -> void:
	_compare_tier(NumCtx.f64(), NNumCtx.f64())


func test_fixtures_4_limbs() -> void:
	_compare_tier(NumCtx.with_limbs(LIMBS), NNumCtx.with_limbs(LIMBS))


func test_fixtures_fast_with_gains() -> void:
	var mismatches: Array = []
	for fx in _fixtures:
		var a := _gd_fast(fx)
		var b := _n_fast(fx)
		var r := Diff.events_diff(a.events, b.events, label(fx))
		if r == "":
			r = Diff.floats_diff(a.gain, b.gain, label(fx) + ".gain")
		if r != "":
			mismatches.append(r)
	assert_eq(mismatches.size(), 0, "fast: %d fixtures differ\n%s" % [mismatches.size(), "\n".join(mismatches)])


func test_plan_helpers_identical() -> void:
	for fx in _fixtures:
		if fx["plan"].is_empty():
			continue
		var gd := BeamScene.from_defs(NumCtx.f64(), fx["surfaces"])
		var nt := NBeamScene.from_defs(NNumCtx.f64(), fx["surfaces"])
		var plan := plan_of(fx["plan"])
		var flags := flags_of(fx)
		assert_eq(NBeamSolver.plan_effects(nt, plan, flags), BeamSolver.plan_effects(gd, plan, flags), "%s plan_effects" % label(fx))
		var pf_gd: Dictionary = BeamSolver.plan_flags(gd, plan, flags)
		var pf_n: Dictionary = NBeamSolver.plan_flags(nt, plan, flags)
		assert_true(pf_gd == pf_n and pf_gd.keys() == pf_n.keys(), "%s plan_flags %s vs %s" % [label(fx), pf_gd, pf_n])
		if fx["cursor"] != null:
			var c_gd := NumCtx.f64().cn(fx["cursor"][0], fx["cursor"][1])
			var c_n := NNumCtx.f64().cn(fx["cursor"][0], fx["cursor"][1])
			assert_eq(NBeamSolver.fixes_cursor(nt, c_n, plan, flags), BeamSolver.fixes_cursor(gd, c_gd, plan, flags), "%s fixes_cursor" % label(fx))


func test_targets_of_identical() -> void:
	for fx in _fixtures:
		var gd := BeamScene.from_defs(NumCtx.f64(), fx["surfaces"])
		var nt := NBeamScene.from_defs(NNumCtx.f64(), fx["surfaces"])
		var ev_gd := _gd_walk(fx, NumCtx.f64())
		var ev_n := _n_walk(fx, NNumCtx.f64())
		assert_eq(NBeamSolver.targets_of(nt, ev_n), BeamSolver.targets_of(gd, ev_gd), "%s targets" % label(fx))
