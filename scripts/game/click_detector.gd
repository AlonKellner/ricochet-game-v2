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
	if segment.is_line():
		var a := segment.start.coords
		var b := segment.end.coords
		var ab := b - a
		var ap := point - a
		var ab_len_sq := ab.length_squared()
		if ab_len_sq == 0.0:
			return point.distance_to(a)
		var t := clampf(ab.dot(ap) / ab_len_sq, 0.0, 1.0)
		var nearest := a + ab * t
		return point.distance_to(nearest)

	var carrier := segment.get_carrier()
	var ctr := carrier.center()
	var r := carrier.radius()

	if segment.full:
		return absf(point.distance_to(ctr) - r)

	var s := segment.start.coords
	var e := segment.end.coords
	var v := segment.via.coords

	var sa := (s - ctr).angle()
	var pa := (point - ctr).angle()

	var cross := (s - ctr).cross(v - ctr)
	var clockwise := cross < 0.0

	var ea := (e - ctr).angle()
	var span: float
	if clockwise:
		span = fposmod(sa - ea, TAU)
	else:
		span = fposmod(ea - sa, TAU)

	var via_diff: float
	if clockwise:
		via_diff = fposmod(sa - (v - ctr).angle(), TAU)
	else:
		via_diff = fposmod((v - ctr).angle() - sa, TAU)
	if via_diff > span:
		clockwise = not clockwise
		span = TAU - span

	var point_diff: float
	if clockwise:
		point_diff = fposmod(sa - pa, TAU)
	else:
		point_diff = fposmod(pa - sa, TAU)

	if point_diff <= span:
		return absf(point.distance_to(ctr) - r)
	else:
		return minf(point.distance_to(s), point.distance_to(e))
