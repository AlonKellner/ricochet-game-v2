class_name GdClickGeom
extends RefCounted

# Test-support copy of ClickDetector._distance_to_segment (the GDScript click
# distance, float32 Vector2 math) for tests/native/test_diff_visual.gd; the
# game uses Segment.distance_to (native). Deleted in Stage 99.

static func distance_to_segment(point: Vector2, segment) -> float:
	if segment.is_line():
		var a: Vector2 = segment.start.coords
		var b: Vector2 = segment.end.coords
		var ab: Vector2 = b - a
		var ap: Vector2 = point - a
		var ab_len_sq: float = ab.length_squared()
		if ab_len_sq == 0.0:
			return point.distance_to(a)
		var t := clampf(ab.dot(ap) / ab_len_sq, 0.0, 1.0)
		var nearest: Vector2 = a + ab * t
		return point.distance_to(nearest)

	var carrier = segment.get_carrier()
	var ctr: Vector2 = carrier.center()
	var r: float = carrier.radius()

	if segment.full:
		return absf(point.distance_to(ctr) - r)

	var s: Vector2 = segment.start.coords
	var e: Vector2 = segment.end.coords
	var v: Vector2 = segment.via.coords

	var sa: float = (s - ctr).angle()
	var pa: float = (point - ctr).angle()

	var cross: float = (s - ctr).cross(v - ctr)
	var clockwise: bool = cross < 0.0

	var ea: float = (e - ctr).angle()
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
