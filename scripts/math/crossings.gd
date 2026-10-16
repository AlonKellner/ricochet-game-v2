class_name Crossings
extends RefCounted

# Proper crossings of two segments (GAME_SPEC 9.7, Stage 108): points interior
# to both where they cross transversally. A touch (tangent carriers), an
# endpoint on the other segment (a T-junction) and coinciding endpoints (a
# corner) are not crossings: an endpoint exactly on the other carrier is
# decided by the exact predicates (Exact), never by a tolerance.
#
# A crossing is identified by the two carriers and the *enters* bit: along A's
# orientation (start -> via -> end) A enters B (its tangent opposes B's
# gradient) at one of their two points and exits at the other.
#
# of(a, b) -> [{"point": Vector2 (rounded once, float32), "ta": position along
# a, "tb": along b, "enters": A enters B there}], positions as param() gives.

# The carrier's coefficients a(x^2+y^2) + bx + cy + d as float64.
static func _coef(seg: Segment) -> Array[float]:
	var g := seg.get_carrier()
	return [g.a, g.b, g.c, g.d]

static func _center(k: Array[float]) -> Vector2:
	return Vector2(-k[1] / (2.0 * k[0]), -k[2] / (2.0 * k[0]))

static func _center_xy(k: Array[float]) -> PackedFloat64Array:
	return PackedFloat64Array([-k[1] / (2.0 * k[0]), -k[2] / (2.0 * k[0])])

static func _r2(k: Array[float]) -> float:
	return (k[1] * k[1] + k[2] * k[2] - 4.0 * k[0] * k[3]) / (4.0 * k[0] * k[0])

# +1: the segment turns toward +90 degrees (start -> via -> end), -1 the other way.
static func _turn(seg: Segment) -> float:
	var s := seg.start.coords
	var v := seg.via.coords
	var e := seg.end.coords
	return 1.0 if (v.x - s.x) * (e.y - v.y) - (v.y - s.y) * (e.x - v.x) > 0.0 else -1.0

# Position of a point on the segment's carrier along its orientation: a line's
# fraction from start (0) to end (1); a circle's angle from start in the
# travel direction, in [0, TAU).
static func param(seg: Segment, p: Vector2) -> float:
	var k := _coef(seg)
	var s := seg.start.coords
	if k[0] == 0.0:
		var d := seg.end.coords - s
		return ((p.x - s.x) * d.x + (p.y - s.y) * d.y) / (d.x * d.x + d.y * d.y)
	return _angle_param(seg, k, float(p.x), float(p.y))

static func _angle_param(seg: Segment, k: Array[float], px: float, py: float) -> float:
	var c := _center_xy(k)
	var s := seg.start.coords
	var a0 := atan2(s.y - c[1], s.x - c[0])
	var a := atan2(py - c[1], px - c[0])
	return fposmod((a - a0) * _turn(seg), TAU)

# The span of an arc (its end's position); a full circle's is TAU.
static func span(seg: Segment) -> float:
	if seg.full:
		return TAU
	var k := _coef(seg)
	if k[0] == 0.0:
		return 1.0
	var e := seg.end.coords
	return _angle_param(seg, k, e.x, e.y)

# Exactly on the segment's carrier (the exact predicates on its defining data).
static func incident(seg: Segment, k_pt: Vector2) -> bool:
	var s := seg.start.coords
	var e := seg.end.coords
	var g := seg.get_carrier()
	if g.is_line():
		return Exact.orient_zero(s.x, s.y, e.x, e.y, k_pt.x, k_pt.y)
	if seg.full:
		var k := _coef(seg)
		var c := _center_xy(k)
		return Exact.on_circle_zero(c[0], c[1], sqrt(_r2(k)), k_pt.x, k_pt.y)
	var v := seg.via.coords
	return Exact.incircle_zero(s.x, s.y, v.x, v.y, e.x, e.y, k_pt.x, k_pt.y)

# The carrier intersection points as float64 [x, y] pairs (none for parallel
# lines, disjoint or tangent circles).
static func _carrier_points(ka: Array[float], kb: Array[float], a: Segment, b: Segment) -> Array:
	if ka[0] == 0.0 and kb[0] == 0.0:
		var det := ka[1] * kb[2] - kb[1] * ka[2]
		if det == 0.0:
			return []
		return [[(ka[2] * kb[3] - kb[2] * ka[3]) / det, (kb[1] * ka[3] - ka[1] * kb[3]) / det]]
	if ka[0] == 0.0 or kb[0] == 0.0:
		var line_seg: Segment = a if ka[0] == 0.0 else b
		var kc: Array[float] = kb if ka[0] == 0.0 else ka
		var c := _center_xy(kc)
		var r2 := _r2(kc)
		var s := line_seg.start.coords
		var dx := float(line_seg.end.coords.x) - s.x
		var dy := float(line_seg.end.coords.y) - s.y
		var fx := float(s.x) - c[0]
		var fy := float(s.y) - c[1]
		var qa := dx * dx + dy * dy
		var qb := 2.0 * (dx * fx + dy * fy)
		var qc := fx * fx + fy * fy - r2
		var disc := qb * qb - 4.0 * qa * qc
		if disc <= 0.0:
			return []   # disjoint, or a touch
		var sq := sqrt(disc)
		var out := []
		for t in [(-qb - sq) / (2.0 * qa), (-qb + sq) / (2.0 * qa)]:
			out.append([s.x + t * dx, s.y + t * dy])
		return out
	var c1 := _center_xy(ka)
	var c2 := _center_xy(kb)
	var r1s := _r2(ka)
	var r2s := _r2(kb)
	var ux := c2[0] - c1[0]
	var uy := c2[1] - c1[1]
	var d2 := ux * ux + uy * uy
	if d2 == 0.0:
		return []   # concentric
	var d := sqrt(d2)
	var r1 := sqrt(r1s)
	var r2 := sqrt(r2s)
	if d >= r1 + r2 or d <= absf(r1 - r2):
		return []   # disjoint, nested, or a touch
	var m := (r1s - r2s + d2) / (2.0 * d)
	var h := sqrt(maxf(r1s - m * m, 0.0))
	var bx := c1[0] + m * ux / d
	var by := c1[1] + m * uy / d
	return [[bx - h * uy / d, by + h * ux / d], [bx + h * uy / d, by - h * ux / d]]

# Strictly inside the segment (positions as param()).
static func _interior(seg: Segment, k: Array[float], px: float, py: float) -> Dictionary:
	if k[0] == 0.0:
		var s := seg.start.coords
		var dx := float(seg.end.coords.x) - s.x
		var dy := float(seg.end.coords.y) - s.y
		var t := ((px - s.x) * dx + (py - s.y) * dy) / (dx * dx + dy * dy)
		return {"in": t > 0.0 and t < 1.0, "t": t}
	var t2 := _angle_param(seg, k, px, py)
	if seg.full:
		return {"in": true, "t": t2}
	return {"in": t2 > 0.0 and t2 < span(seg), "t": t2}

# The carrier's unit travel tangent at a point on it (a's orientation).
static func _tangent(seg: Segment, k: Array[float], px: float, py: float) -> Vector2:
	if k[0] == 0.0:
		return (seg.end.coords - seg.start.coords).normalized()
	var c := _center_xy(k)
	return Vector2(-(py - c[1]), px - c[0]).normalized() * _turn(seg)

static func of(a: Segment, b: Segment) -> Array:
	var ka := _coef(a)
	var kb := _coef(b)
	# endpoints exactly on the other carrier: their crossings are corners or
	# T-junctions (not proper), whatever rounding says about their positions
	var touching: Array[Vector2] = []
	for pair in [[a, b], [b, a]]:
		var own: Segment = pair[0]
		if own.full:
			continue
		for ep in [own.start.coords, own.end.coords]:
			if incident(pair[1], ep):
				touching.append(ep)
	var out := []
	for p in _carrier_points(ka, kb, a, b):
		var px: float = p[0]
		var py: float = p[1]
		var ia := _interior(a, ka, px, py)
		var ib := _interior(b, kb, px, py)
		if not (ia["in"] and ib["in"]):
			continue
		var point := Vector2(px, py)
		if touching.any(func(ep: Vector2) -> bool: return ep.distance_to(point) <= 1e-6 * maxf(1.0, ep.length())):
			continue
		var grad_b := Vector2(2.0 * kb[0] * px + kb[1], 2.0 * kb[0] * py + kb[2])
		out.append({"point": point, "ta": ia["t"], "tb": ib["t"], "enters": _tangent(a, ka, px, py).dot(grad_b) < 0.0})
	return out
