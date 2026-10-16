class_name Pieces
extends RefCounted

# The plannable units of a world (GAME_SPEC 9.7, Stage 108): every design
# surface split at its proper crossings with the others (Crossings), so that
# no two pieces cross. The solver keeps tracing the whole design surfaces;
# clicks, hover, plan entries and their overlays address pieces. A surface
# with no crossings is one piece holding its own segment (index -1, no span):
# crossing-free levels are exactly as before. Splitting ignores presence, and
# portals and pre-transform circles are neither split nor cut.

class Piece extends RefCounted:
	var design: Surface
	var index := -1                  # -1: the whole surface (not split)
	var segment: Segment             # the span on the design surface's carrier
	var span = null                  # PackedFloat64Array [sx, sy, ex, ey, vx, vy] (null: the whole surface)

	# Whether a point on the carrier lies on this piece (its span).
	func contains(p: Vector2) -> bool:
		if span == null:
			return true
		var t := Crossings.param(segment, p)
		return t >= 0.0 and t <= Crossings.span(segment)

var _by_id := {}                     # design surface id -> Array[Piece]

# Whether a point lies in a piece's span (a plan entry's) on its surface's carrier.
static func span_contains(surface: Surface, span: PackedFloat64Array, p: Vector2) -> bool:
	if surface == null or not p.is_finite():
		return false
	var seg := Segment.with_carrier(Vector2(span[0], span[1]), Vector2(span[2], span[3]), Vector2(span[4], span[5]),
		surface.segment.get_carrier())
	var t := Crossings.param(seg, p)
	return t >= 0.0 and t <= Crossings.span(seg)
var crossings: Array[Vector2] = []   # every crossing point (drawn as ticks)
var portal_crossings: Array[String] = []   # validation: a design surface crossing a portal

func of(surface: Surface) -> Array:
	return _by_id.get(surface.id, [])

func of_id(id: int) -> Array:
	return _by_id.get(id, [])

func piece(id: int, index: int) -> Piece:
	for p: Piece in of_id(id):
		if p.index == index:
			return p
	return null

static func _cuttable(s: Surface) -> bool:
	return not s.pre_transform and not _is_portal(s)

static func _is_portal(s: Surface) -> bool:
	for cfg in s.all_side_configs():
		if cfg != null and cfg.effect is RigidMotionEffect:
			return true
	return false

static func build(surfaces: Array) -> Pieces:
	var out := Pieces.new()
	var cuts := {}   # id -> [[t, point], ...]
	for i in surfaces.size():
		var a: Surface = surfaces[i]
		if a.pre_transform:
			continue
		for j in range(i + 1, surfaces.size()):
			var b: Surface = surfaces[j]
			if b.pre_transform:
				continue
			var xs := Crossings.of(a.segment, b.segment)
			if xs.is_empty():
				continue
			if _is_portal(a) or _is_portal(b):
				out.portal_crossings.append("surface %d crosses portal %d" % [b.id if _is_portal(a) else a.id, a.id if _is_portal(a) else b.id])
				continue
			for x in xs:
				cuts.get_or_add(a.id, []).append([x["ta"], x["point"]])
				cuts.get_or_add(b.id, []).append([x["tb"], x["point"]])
				out.crossings.append(x["point"])
	for s: Surface in surfaces:
		out._by_id[s.id] = _split(s, cuts.get(s.id, []))
	return out

static func _whole(s: Surface) -> Array:
	var p := Piece.new()
	p.design = s
	p.segment = s.segment
	return [p]

# A design surface's pieces from its cuts ([position, point], unordered).
static func _split(s: Surface, c: Array) -> Array:
	if c.is_empty() or not _cuttable(s):
		return _whole(s)
	c.sort_custom(func(x, y): return x[0] < y[0])
	var seg := s.segment
	var carrier := seg.get_carrier()
	var pts: Array[Vector2] = []
	for x in c:
		if pts.is_empty() or pts[-1] != x[1]:
			pts.append(x[1])   # concurrent crossings share one point
	var ends: Array = []        # [start, end] of each piece, in order
	if seg.full:
		if pts.size() < 2:
			return _whole(s)    # one cut leaves the circle whole
		for i in pts.size():
			ends.append([pts[i], pts[(i + 1) % pts.size()]])
	else:
		var all: Array[Vector2] = [seg.start.coords]
		all.append_array(pts)
		all.append(seg.end.coords)
		for i in all.size() - 1:
			ends.append([all[i], all[i + 1]])
	var out := []
	for i in ends.size():
		var p := Piece.new()
		p.design = s
		p.index = i
		var a: Vector2 = ends[i][0]
		var b: Vector2 = ends[i][1]
		var v := _between(seg, carrier, a, b)
		p.segment = Segment.with_carrier(a, b, v, carrier)
		p.span = PackedFloat64Array([a.x, a.y, b.x, b.y, v.x, v.y])
		out.append(p)
	return out

# A point on the carrier between a and b (along the segment's orientation).
static func _between(seg: Segment, carrier: GeneralizedCircle, a: Vector2, b: Vector2) -> Vector2:
	if carrier.is_line():
		return Vector2((float(a.x) + b.x) / 2.0, (float(a.y) + b.y) / 2.0)
	var k: Array[float] = [carrier.a, carrier.b, carrier.c, carrier.d]
	var cx := -k[1] / (2.0 * k[0])
	var cy := -k[2] / (2.0 * k[0])
	var r := sqrt((k[1] * k[1] + k[2] * k[2] - 4.0 * k[0] * k[3]) / (4.0 * k[0] * k[0]))
	var ta := Crossings.param(seg, a)
	var tb := Crossings.param(seg, b)
	if tb <= ta:
		tb += TAU
	var s0 := seg.start.coords
	var a0 := atan2(s0.y - cy, s0.x - cx)
	var turn := 1.0 if (seg.via.coords.x - s0.x) * (seg.end.coords.y - seg.via.coords.y) \
		- (seg.via.coords.y - s0.y) * (seg.end.coords.x - seg.via.coords.x) > 0.0 else -1.0
	var ang := a0 + turn * (ta + tb) / 2.0
	return Vector2(cx + r * cos(ang), cy + r * sin(ang))
