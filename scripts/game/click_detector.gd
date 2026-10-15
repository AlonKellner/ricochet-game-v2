class_name ClickDetector
extends RefCounted

const CLICK_TOLERANCE := 24.0

var _last_cycle_position := Vector2.ZERO
var _last_cycle_index := -1

# game_state: the configs of categorical surfaces depend on it.
# accept(surface, side) -> bool: sides the plan refuses are skipped.
# level_bounds + allow_infinity (Stage 88, GAME_SPEC 4.2): the infinity entry,
# {"infinity": true, "distance": d}. When the frame's image of infinity is a
# point on screen (`infinity_point`, Stage 101) it is clickable like a surface
# (the nearest wins: {"infinity": true, "point": q, ...}); otherwise a cursor
# within the tolerance inside the bounds' edge where no surface wins is it.
func detect_click(cursor_pos: Vector2, surfaces: Array, game_state: GameState = null, accept: Callable = Callable(),
		level_bounds: Rect2 = Rect2(), allow_infinity: bool = false, infinity_point = null) -> Dictionary:
	var state := game_state if game_state != null else GameState.new()
	var best_surface: Surface = null
	var best_side: Side.Value = Side.Value.LEFT
	var best_dist := INF

	for surf in surfaces:
		if surf.pre_transform:
			continue
		var dist := _distance_to_segment(cursor_pos, surf.segment)
		if dist > CLICK_TOLERANCE:
			continue

		var side: Side.Value = surf.segment.determine_side(cursor_pos)
		var config: SideConfig = surf.active_side_config(side, state)
		if config == null or not config.interactive:
			continue
		if accept.is_valid() and not accept.call(surf, side):
			continue

		if dist < best_dist:
			best_dist = dist
			best_surface = surf
			best_side = side

	if allow_infinity:
		var on_screen := infinity_point is Vector2 and (not level_bounds.has_area() or level_bounds.has_point(infinity_point))
		if on_screen:
			var dq: float = cursor_pos.distance_to(infinity_point)
			if dq <= CLICK_TOLERANCE and dq < best_dist:
				return {"infinity": true, "point": infinity_point, "distance": dq}
		elif best_surface == null and level_bounds.has_area():
			var d := edge_distance(cursor_pos, level_bounds)
			if d >= 0.0 and d <= CLICK_TOLERANCE:
				return {"infinity": true, "distance": d}
	if best_surface == null:
		return {}

	return {"surface": best_surface, "side": best_side, "distance": best_dist}

# Distance from `p` to the nearest edge of `b`, negative outside it.
static func edge_distance(p: Vector2, b: Rect2) -> float:
	return minf(minf(p.x - b.position.x, b.end.x - p.x), minf(p.y - b.position.y, b.end.y - p.y))


static func _distance_to_segment(point: Vector2, segment: Segment) -> float:
	return segment.distance_to(point)
