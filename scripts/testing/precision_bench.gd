class_name PrecisionBench
extends RefCounted

# Benchmark of trajectory strategies against the mpmath oracle corpus
# (bench/scenarios + bench/expected: scenarios from docs/research/oracle/gen_corpus.py,
# expected traces under the v2 rules from regen_bench_v2.py). Scenario
# "surfaces" are scene defs (BeamScene); "circles" / "walls" build the same
# scene as game surfaces (build_surfaces).
#
# Strategies:
#   beam_f64         BeamSolver at float64 (anchored planned + forward physical)
#   beam_big<bits>   BeamSolver at BigFloat precision
#   ladder           PrecisionLadder (f64 -> BigFloat tiers until converged)

const TOL := 0.01
static var SCN_DIR := "res://bench/scenarios"
static var EXP_DIR := "res://bench/expected"

static func use_corpus(subdir: String) -> void:
	var base := "res://bench" if subdir == "" else "res://bench/" + subdir
	SCN_DIR = base + "/scenarios"
	EXP_DIR = base + "/expected"

class Ev extends RefCounted:
	var kind: String
	var surface_id: int
	var left: bool
	var applied: bool = true
	var has_applied: bool = false
	var px: float  # float64 (Vector2 is float32 in this Godot build)
	var py: float
	var est_err: float = -1.0

static func load_json(path: String):
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return null
	return JSON.parse_string(f.get_as_text())

static func list_scenarios(filter: String = "") -> Array:
	var out: Array = []
	var dir := DirAccess.open(SCN_DIR)
	if dir == null:
		return out
	for fn in dir.get_files():
		if fn.ends_with(".json") and (filter == "" or fn.contains(filter)):
			out.append(fn.get_basename())
	out.sort()
	return out

# ---------------------------------------------------------------- scenes

static func build_surfaces(scn: Dictionary) -> Array:
	Surface.reset_id_counter()
	var surfaces: Array = []
	for c in scn["circles"]:
		var carrier := GeneralizedCircle.from_circle(Vector2(c["center"][0], c["center"][1]), c["radius"])
		var seg := Segment.full_from_carrier(carrier)
		var inv := CircleInversionEffect.new(carrier)
		var surf := Surface.new(seg, SideConfig.new(inv, true), SideConfig.new(inv, true), false, true)
		assert(surf.id == int(c["id"]), "surface ids must match scenario ids")
		surfaces.append(surf)
	if scn.get("walls", true):
		for w in [Vector4(0, 0, 1920, 0), Vector4(1920, 0, 1920, 1080), Vector4(1920, 1080, 0, 1080), Vector4(0, 1080, 0, 0)]:
			var seg := Segment.from_coords(Vector2(w.x, w.y), Vector2(w.z, w.w), Vector2((w.x + w.z) / 2.0, (w.y + w.w) / 2.0))
			var cfg := SideConfig.new(null, false)
			surfaces.append(Surface.new(seg, cfg, cfg, false, false))
	return surfaces

static func plan_entries(scn: Dictionary) -> Array:
	var out: Array = []
	for e in scn["plan"]:
		out.append(PlanManager.PlanEntry.new(int(e["surface_id"]), Side.Value.LEFT if e["side"] == "LEFT" else Side.Value.RIGHT))
	return out

static func plan_items(scn: Dictionary) -> Array:
	var out: Array = []
	for e in scn["plan"]:
		out.append(BeamSolver.PlanItem.make(int(e["surface_id"]), e["side"] == "LEFT"))
	return out

# Scene defs (BeamScene format).
static func scene_defs(scn: Dictionary) -> Array:
	return scn["surfaces"]

# ---------------------------------------------------------------- strategies

static func run_beam(scn: Dictionary, ctx: NumCtx, n_planned: int, n_physical: int) -> Dictionary:
	var scene := BeamScene.from_defs(ctx, scene_defs(scn))
	var player := ctx.cn(scn["player"][0], scn["player"][1])
	var cursor := ctx.cn(scn["cursor"][0], scn["cursor"][1])
	var plan := plan_items(scn)
	var planned := BeamSolver.planned(ctx, scene, player, cursor, plan, n_planned)
	var phys := BeamSolver.physical(ctx, scene, player, cursor, plan, n_physical)
	return {"planned": beam_events(planned), "physical": beam_events(phys)}

static func run_ladder(scn: Dictionary, n_physical: int, verify: bool = false) -> Dictionary:
	var r := PrecisionLadder.solve(scene_defs(scn), [float(scn["player"][0]), float(scn["player"][1])],
		[float(scn["cursor"][0]), float(scn["cursor"][1])], plan_items(scn), n_physical, n_physical, TOL, verify)
	return {"planned": beam_events(r.planned, r.planned_err), "physical": beam_events(r.physical, r.physical_err),
		"tier": r.physical_tier, "converged": r.converged, "usec_planned": r.usec_planned,
		"usec_physical": r.usec_physical, "tiers_tried": r.tiers_tried}

# float64 BeamSolver with the a-priori (tangent-linear) error estimate attached.
static func run_estimated_f64(scn: Dictionary, n_physical: int) -> Dictionary:
	var defs := scene_defs(scn)
	var pl := [float(scn["player"][0]), float(scn["player"][1])]
	var cu := [float(scn["cursor"][0]), float(scn["cursor"][1])]
	var plan := plan_items(scn)
	var sp := ErrorEstimate.physical_sensitivity(defs, pl, cu, plan, n_physical)
	var sq := ErrorEstimate.planned_sensitivity(defs, pl, cu, plan, n_physical)
	var eps := NumCtx.f64().eps()
	# planned: the ladder's estimate (a-priori envelope and a-posteriori residuals)
	var est_q := ErrorEstimate.estimate(sq, eps)
	var res_q := PrecisionLadder.planned_residuals(sq.events, BeamScene.from_defs(NumCtx.f64(), defs))
	for i in est_q.size():
		est_q[i] = maxf(est_q[i], res_q[i])
	return {"planned": beam_events(sq.events, est_q),
		"physical": beam_events(sp.events, ErrorEstimate.estimate(sp, eps)),
		"max_gain": sp.max_gain}

static func beam_events(events: Array, errs: Array = []) -> Array:
	var out: Array = []
	for i in events.size():
		var be: BeamSolver.Event = events[i]
		var e := Ev.new()
		e.kind = be.kind
		e.surface_id = be.surface_id
		e.left = be.left
		e.applied = be.applied
		e.has_applied = true
		e.px = INF if be.point == null else be.point.fx()
		e.py = INF if be.point == null else be.point.fy()
		if i < errs.size():
			e.est_err = errs[i]
		out.append(e)
	return out

# ---------------------------------------------------------------- scoring

# Returns {matched, total, max_err, mismatch(-1 = none), ok, calib: [[est, actual], ...]}
static func compare(events: Array, expected: Array) -> Dictionary:
	var cut := expected.size()
	for i in expected.size():
		if expected[i]["kind"] in ["origin", "escape", "max"]:
			cut = i
			break
	var worst := 0.0
	var calib: Array = []
	for i in cut:
		var ex: Dictionary = expected[i]
		if i >= events.size():
			return {"matched": i, "total": cut, "max_err": worst, "mismatch": i, "ok": false, "calib": calib}
		var e: Ev = events[i]
		var same: bool = e.kind == ex["kind"]
		if same and ex["kind"] == "hit":
			same = e.surface_id == int(ex["surface_id"]) and e.left == (ex["side"] == "LEFT")
			if e.has_applied:
				same = same and e.applied == bool(ex["applied"])
		if not same:
			return {"matched": i, "total": cut, "max_err": worst, "mismatch": i, "ok": false, "calib": calib}
		if ex.has("x"):
			var dx: float = e.px - String(ex["x"]).to_float()
			var dy: float = e.py - String(ex["y"]).to_float()
			var err: float = sqrt(dx * dx + dy * dy)
			worst = maxf(worst, err)
			if e.est_err >= 0.0:
				calib.append([e.est_err, err])
	return {"matched": cut, "total": cut, "max_err": worst, "mismatch": -1, "ok": worst <= TOL, "calib": calib}
