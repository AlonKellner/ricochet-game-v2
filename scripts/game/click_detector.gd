class_name ClickDetector
extends RefCounted

const CLICK_TOLERANCE := 24.0

var _last_cycle_position := Vector2.ZERO
var _last_cycle_index := -1

# game_state: the configs of categorical surfaces depend on it.
# accept(surface, side) -> bool: sides the plan refuses are skipped.
func detect_click(cursor_pos: Vector2, surfaces: Array, game_state: GameState = null, accept: Callable = Callable()) -> Dictionary:
	var state := game_state if game_state != null else GameState.new()
	var best_surface: Surface = null
	var best_side: Side.Value = Side.Value.LEFT
	var best_dist := INF

	for surf in surfaces:
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

	if best_surface == null:
		return {}

	return {"surface": best_surface, "side": best_side, "distance": best_dist}


static func _distance_to_segment(point: Vector2, segment: Segment) -> float:
	return segment.distance_to(point)
