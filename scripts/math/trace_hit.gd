class_name TraceHit
extends RefCounted

# A hit of a beam on a surface's segment. Formerly TraceHit
# (still available under that name as an alias).

var t: float
var point: Point
var segment: Segment
var side: Side.Value
var on_segment: bool
var at_endpoint: int
var blocked_left: bool
var blocked_right: bool

func _init(p_t: float, p_point: Vector2, p_segment: Segment, p_side: Side.Value, p_on_segment: bool, p_at_endpoint: int = 0, p_blocked_left: bool = false, p_blocked_right: bool = false) -> void:
	t = p_t
	point = Point.at(p_point)
	segment = p_segment
	side = p_side
	on_segment = p_on_segment
	at_endpoint = p_at_endpoint
	blocked_left = p_blocked_left
	blocked_right = p_blocked_right

func is_fully_blocked() -> bool:
	return blocked_left and blocked_right
