class_name TestTrace
extends RefCounted

# Test helper with the old Tracer.trace signature, running the beam solver
# (BeamTracer.trace_pair). The cursor is cursor_pos when given, otherwise the
# aim direction's end point (no plan: the aim point is the cursor). The beam
# solver computes its own aim from (player, cursor, plan): `direction` only
# supplies the cursor; shared_ray, target_dist and post_cursor_mode are unused.

static func trace(origin: Vector2, direction: Direction, surfaces: Array, _game_state: GameState = null,
		_shared_ray: Ray = null, _target_dist: float = -1.0, mode: int = TraceMode.PHYSICAL,
		_post_cursor_mode: int = TraceMode.PHYSICAL, plan_entries: Array = [], _cache = null,
		cursor_pos: Vector2 = Vector2(INF, INF)) -> TracePath:
	var cursor := cursor_pos if cursor_pos.x != INF else direction.end.coords
	assert(plan_entries.is_empty() or cursor_pos.x != INF, "TestTrace: a plan needs cursor_pos")
	var pair := pair_of(origin, cursor, surfaces, plan_entries)
	if pair == null:
		return TracePath.new()
	return pair.planned if mode == TraceMode.PLANNED else pair.physical

static func trace_ray(ray: Ray, surfaces: Array, game_state: GameState = null) -> TracePath:
	return trace(ray.origin.coords, ray.direction, surfaces, game_state)

static func pair_of(player: Vector2, cursor: Vector2, surfaces: Array, plan_entries: Array = []) -> BeamTracer.Pair:
	BeamTracer.clear_cache()
	return BeamTracer.trace_pair(surfaces, player, cursor, plan_entries)
