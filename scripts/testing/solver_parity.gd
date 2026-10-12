class_name SolverParity
extends RefCounted

# Legacy Tracer vs beam solver at shallow depth (Stages 81-83, removed with
# the legacy solver in Stage 84). A difference is classified against the
# oracle: "legacy_wrong" when the beam solver agrees with the oracle (legacy
# precision loss or a documented legacy quirk); "unclassified" otherwise.

const DEPTH := 8
const TOL := 0.01

# Hit/cursor events of a legacy trace: [{kind, surface_id, left, x, y}].
static func legacy_events(surfaces: Array, player: Vector2, cursor: Vector2, plan_entries: Array) -> Dictionary:
	var cache := TransformCache.new()
	var aim := Planner.compute_aim_direction(player, cursor, plan_entries, surfaces, GameState.new(), cache)
	var ray := Ray.from_coords(player, aim)
	var res := {}
	for mode in [Tracer.TraceMode.PLANNED, Tracer.TraceMode.PHYSICAL]:
		var path := Tracer.trace(player, aim, surfaces, GameState.new(), ray, -1.0, mode, Tracer.TraceMode.PHYSICAL,
			plan_entries, cache, cursor)
		var evs: Array = []
		for i in path.steps.size():
			var s: Tracer.Step = path.steps[i]
			if s.surface_id >= 0 and s.hit != null:
				evs.append({"kind": "hit", "surface_id": s.surface_id, "left": s.hit_side == Side.Value.LEFT, "x": s.end.x, "y": s.end.y})
			if path.cursor_index == i + 1:
				evs.append({"kind": "cursor", "surface_id": -1, "left": false, "x": s.end.x, "y": s.end.y})
		res["planned" if mode == Tracer.TraceMode.PLANNED else "physical"] = evs
	return res

# BeamSolver events -> the same shape (hits and cursor only).
static func beam_events(events: Array) -> Array:
	var out: Array = []
	for e in events:
		if e.kind == "hit" or e.kind == "cursor":
			out.append({"kind": e.kind, "surface_id": e.surface_id if e.kind == "hit" else -1,
				"left": e.left and e.kind == "hit", "x": e.point.fx(), "y": e.point.fy()})
	return out

# Oracle expected events -> the same shape.
static func oracle_events(expected: Array) -> Array:
	var out: Array = []
	for x in expected:
		if x["kind"] == "hit" or x["kind"] == "cursor":
			out.append({"kind": x["kind"], "surface_id": int(x["surface_id"]) if x["kind"] == "hit" else -1,
				"left": x["side"] == "LEFT", "x": String(x["x"]).to_float(), "y": String(x["y"]).to_float()})
	return out

static func same_prefix(a: Array, b: Array, n: int, tol: float) -> bool:
	for i in n:
		if i >= a.size() or i >= b.size():
			return a.size() == b.size() or mini(a.size(), b.size()) >= n
		var x: Dictionary = a[i]
		var y: Dictionary = b[i]
		if x["kind"] != y["kind"] or x["surface_id"] != y["surface_id"] or x["left"] != y["left"]:
			return false
		if Vector2(x["x"], x["y"]).distance_to(Vector2(y["x"], y["y"])) > tol:
			return false
	return true

# "same" | "legacy_wrong" | "unclassified"
static func classify(legacy: Array, beam: Array, oracle: Array, depth: int = DEPTH) -> String:
	if not same_prefix(beam, oracle, depth, TOL):
		return "unclassified"
	if same_prefix(legacy, beam, depth, TOL):
		return "same"
	return "legacy_wrong"
