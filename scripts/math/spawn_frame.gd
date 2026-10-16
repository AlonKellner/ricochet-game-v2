class_name SpawnFrame
extends RefCounted

# The placement frame of a spawn (GAME_SPEC 10.6.1): origin at the hit point,
# first axis u by the mode, second axis v = u rotated by +90 degrees
# ((x, y) -> (-y, x)), flipped on a RIGHT-side hit when mirrored. A template's
# local coordinates map through it. Inputs are the solver's own (the hit
# point and the beam's unit travel tangent, TracePath.hits) and the host's
# segment; nothing is re-derived from drawn steps.

enum Mode { BEAM_TANGENT, BEAM_NORMAL, CARRIER_TANGENT, CARRIER_NORMAL, BEAM_REFLECTED, WORLD }

static func rot90(v: Vector2) -> Vector2:
	return Vector2(-v.y, v.x)

# The unit tangent of a segment's carrier at a point on it, oriented along the
# segment (start -> via -> end; a full circle by the same three points).
static func carrier_tangent(start: Vector2, via: Vector2, end: Vector2, is_line: bool, center: Vector2, point: Vector2) -> Vector2:
	if is_line:
		return (end - start).normalized()
	var turn := (via - start).cross(end - via)   # > 0: turning toward +90 degrees
	var t := rot90(point - center).normalized()
	return t if turn > 0.0 else -t

# u for a mode. beam: the incoming unit travel tangent; tangent: the carrier's.
static func axis(mode: Mode, beam: Vector2, tangent: Vector2) -> Vector2:
	match mode:
		Mode.BEAM_TANGENT:
			return beam
		Mode.BEAM_NORMAL:
			return rot90(beam)
		Mode.CARRIER_TANGENT:
			return tangent
		Mode.CARRIER_NORMAL:
			# toward the side the beam came from
			var n := rot90(tangent)
			return -n if n.dot(beam) > 0.0 else n
		Mode.BEAM_REFLECTED:
			return 2.0 * beam.dot(tangent) * tangent - beam
	return Vector2.RIGHT

# The frame at a hit: x axis u, y axis v, origin the hit point.
static func frame(mode: Mode, point: Vector2, beam: Vector2, tangent: Vector2, from_right: bool, mirror_with_side: bool) -> Transform2D:
	var u := axis(mode, beam, tangent)
	var v := rot90(u)
	if mirror_with_side and from_right:
		v = -v
	return Transform2D(u, v, point)
