class_name EdgeOverlayNode
extends Node2D

# Infinity as a plannable "surface" (Stages 88d and 101, GAME_SPEC 4.2),
# drawn like the surfaces' hover and plan overlays (SurfaceNode): a gradient
# band fading away from the "surface", incoming chevrons near it pointing at
# it, outgoing chevrons farther out pointing away. The frame's image of
# infinity is either the real infinity -- the level bounds' edge: the band
# runs inside the four sides, one chevron pair per side -- or, after a
# pre-transform or an inversion, a point on screen: a circled cross (the
# "infinity point") with a ring gradient and four radial chevron pairs.
# Hover uses the hover colour; planned entries the plan's valid / invalid
# colours, each mark at its own frame's point or on the edge where clicked,
# with the entry's number.

const BAND_WIDTH := ClickDetector.CLICK_TOLERANCE
const HOVER_COLOR := ChevronOverlayBuilder.HOVER_COLOR
const PLAN_VALID_COLOR := ChevronOverlayBuilder.PLAN_VALID_COLOR
const PLAN_INVALID_COLOR := ChevronOverlayBuilder.PLAN_INVALID_COLOR
# the surfaces' overlay geometry (SurfaceNode)
const GRADIENT_STRIPS := 6
const CHEVRON_SIZE := 8.0
const INNER_CHEVRON_DIST := 2.0
const OUTER_CHEVRON_DIST := 18.0
const MARK_HALF_LENGTH := 60.0
const POINT_RADIUS := 9.0
const POINT_COLOR := Color(0.75, 0.85, 1.0, 0.9)
const RADIAL_DIRS: Array[Vector2] = [Vector2.RIGHT, Vector2.DOWN, Vector2.LEFT, Vector2.UP]

var bounds := Rect2(0, 0, 1920, 1080)
var _hovered := false
var _point = null        # the frame's image of infinity when on screen (Vector2), else null
var _marks: Array = []   # [{index: int, point: Vector2, on_point: bool, valid: bool}]

func setup(p_bounds: Rect2) -> void:
	bounds = p_bounds
	z_index = 5
	queue_redraw()

func set_hovered(on: bool) -> void:
	if on == _hovered:
		return
	_hovered = on
	queue_redraw()

func is_hovered() -> bool:
	return _hovered

# The frame's image of infinity after the current plan: a Vector2 inside the
# bounds draws the point marker (and the edge band stands down), else null.
func set_point(q, p_bounds: Rect2) -> void:
	var on_screen = q if (q is Vector2 and p_bounds.has_point(q)) else null
	if on_screen == _point:
		return
	_point = on_screen
	queue_redraw()

func get_point():
	return _point

# One mark per ∞ entry: its 1-based plan index, its point (the frame's
# infinity point, or where the edge was clicked), and whether it is met.
func set_marks(marks: Array) -> void:
	if marks == _marks:
		return
	_marks = marks
	queue_redraw()

func get_marks() -> Array:
	return _marks

# The point of the edge nearest `p` and the outward direction there.
static func snap_to_edge(p: Vector2, b: Rect2) -> Dictionary:
	var candidates := [
		{"point": Vector2(p.x, b.position.y), "outward": Vector2.UP, "dist": absf(p.y - b.position.y)},
		{"point": Vector2(p.x, b.end.y), "outward": Vector2.DOWN, "dist": absf(b.end.y - p.y)},
		{"point": Vector2(b.position.x, p.y), "outward": Vector2.LEFT, "dist": absf(p.x - b.position.x)},
		{"point": Vector2(b.end.x, p.y), "outward": Vector2.RIGHT, "dist": absf(b.end.x - p.x)},
	]
	var best: Dictionary = candidates[0]
	for c in candidates:
		if c["dist"] < best["dist"]:
			best = c
	var pt: Vector2 = best["point"]
	pt.x = clampf(pt.x, b.position.x, b.end.x)
	pt.y = clampf(pt.y, b.position.y, b.end.y)
	return {"point": pt, "outward": best["outward"]}

# The four sides of the bounds: [start, end, outward normal].
static func sides_of(b: Rect2) -> Array:
	return [
		[b.position, Vector2(b.end.x, b.position.y), Vector2.UP],
		[Vector2(b.end.x, b.position.y), b.end, Vector2.RIGHT],
		[b.end, Vector2(b.position.x, b.end.y), Vector2.DOWN],
		[Vector2(b.position.x, b.end.y), b.position, Vector2.LEFT],
	]

func _draw() -> void:
	for m in _marks:
		var color: Color = PLAN_VALID_COLOR if m["valid"] else PLAN_INVALID_COLOR
		if m.get("on_point", false):
			var q: Vector2 = m["point"]
			_draw_point_overlay(q, color)
			_draw_point_marker(q, Color(color, 0.9), false)
			draw_string(ThemeDB.fallback_font, q + Vector2(POINT_RADIUS + OUTER_CHEVRON_DIST, -POINT_RADIUS - 4.0), str(m["index"]),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 14, Color.WHITE)
			continue
		var snapped := snap_to_edge(m["point"], bounds)
		var pt: Vector2 = snapped["point"]
		var out: Vector2 = snapped["outward"]
		var along := Vector2(-out.y, out.x)
		_draw_edge_gradient(pt - along * MARK_HALF_LENGTH, pt + along * MARK_HALF_LENGTH, out, color)
		_draw_edge_chevrons(pt, out, color)
		draw_string(ThemeDB.fallback_font, pt - out * (BAND_WIDTH + 14.0) + Vector2(-5, 5), str(m["index"]),
			HORIZONTAL_ALIGNMENT_CENTER, -1, 14, Color.WHITE)
	if _point is Vector2:
		if _hovered:
			_draw_point_overlay(_point, HOVER_COLOR)
		_draw_point_marker(_point, POINT_COLOR, _hovered)
	elif _hovered:
		# the real infinity: the band inside the four sides, one chevron pair per side
		for side in sides_of(bounds):
			_draw_edge_gradient(side[0], side[1], side[2], HOVER_COLOR)
			_draw_edge_chevrons((side[0] + side[1]) * 0.5, side[2], HOVER_COLOR)

# A gradient fading inward from the edge between a and b (outward normal `out`).
func _draw_edge_gradient(a: Vector2, b: Vector2, out: Vector2, base_color: Color) -> void:
	var strip := BAND_WIDTH / GRADIENT_STRIPS
	for i in GRADIENT_STRIPS:
		var frac := (float(i) + 0.5) / GRADIENT_STRIPS
		var alpha := base_color.a * (1.0 - frac)
		var off := -out * (frac * BAND_WIDTH)
		var p0 := a + off - out * (strip * 0.5)
		var p1 := b + off + out * (strip * 0.5)
		var lo := Vector2(minf(p0.x, p1.x), minf(p0.y, p1.y))
		var hi := Vector2(maxf(p0.x, p1.x), maxf(p0.y, p1.y))
		# a horizontal side has zero height before the strip offset: give the rect the strip's thickness
		if hi.x - lo.x < strip:
			lo.x -= strip * 0.5
			hi.x += strip * 0.5
		if hi.y - lo.y < strip:
			lo.y -= strip * 0.5
			hi.y += strip * 0.5
		draw_rect(Rect2(lo, hi - lo), Color(base_color, alpha))

# The chevron pair at a point of the edge: incoming (toward the edge, close),
# outgoing (back inward, farther).
func _draw_edge_chevrons(pt: Vector2, out: Vector2, base_color: Color) -> void:
	var colors := PackedColorArray([base_color, base_color, base_color])
	draw_polygon(SurfaceGeom.chevron_vertices(pt - out * INNER_CHEVRON_DIST, out, CHEVRON_SIZE), colors)
	draw_polygon(SurfaceGeom.chevron_vertices(pt - out * OUTER_CHEVRON_DIST, -out, CHEVRON_SIZE), colors)

# The point's overlay: a ring gradient fading outward and four radial chevron
# pairs (incoming toward the point, outgoing away from it).
func _draw_point_overlay(q: Vector2, base_color: Color) -> void:
	var strip := BAND_WIDTH / GRADIENT_STRIPS
	for i in GRADIENT_STRIPS:
		var frac := (float(i) + 0.5) / GRADIENT_STRIPS
		var alpha := base_color.a * (1.0 - frac)
		draw_arc(q, POINT_RADIUS + frac * BAND_WIDTH, 0, TAU, 48, Color(base_color, alpha), strip)
	var colors := PackedColorArray([base_color, base_color, base_color])
	for dir in RADIAL_DIRS:
		draw_polygon(SurfaceGeom.chevron_vertices(q + dir * (POINT_RADIUS + INNER_CHEVRON_DIST), -dir, CHEVRON_SIZE), colors)
		draw_polygon(SurfaceGeom.chevron_vertices(q + dir * (POINT_RADIUS + OUTER_CHEVRON_DIST), dir, CHEVRON_SIZE), colors)

# A circled cross: the image of infinity (a point unlike any surface).
func _draw_point_marker(q: Vector2, color: Color, hovered: bool) -> void:
	var r := POINT_RADIUS * (1.3 if hovered else 1.0)
	draw_arc(q, r, 0, TAU, 32, color, 2.0)
	draw_line(q + Vector2(-r, -r) * 0.7, q + Vector2(r, r) * 0.7, color, 2.0)
	draw_line(q + Vector2(-r, r) * 0.7, q + Vector2(r, -r) * 0.7, color, 2.0)
