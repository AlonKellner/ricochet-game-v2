class_name EdgeOverlayNode
extends Node2D

# Infinity as a plannable "surface" (Stages 88d and 101, GAME_SPEC 4.2). The
# frame's image of infinity is either the real infinity -- the level bounds'
# edge: hovering the band inside it highlights it, planned ∞ entries are marked
# on the edge at the point they were clicked (a chevron pointing out through
# the edge, and the entry's number) -- or, after a pre-transform or an
# inversion, a point on screen, drawn as a circled cross (the "infinity point"),
# highlighted when hovered, and marked with the entry's number when planned.
# Colours: the plan's valid / invalid ones, as the planned sides use.

const BAND_WIDTH := ClickDetector.CLICK_TOLERANCE
const HOVER_COLOR := ChevronOverlayBuilder.HOVER_COLOR
const PLAN_VALID_COLOR := ChevronOverlayBuilder.PLAN_VALID_COLOR
const PLAN_INVALID_COLOR := ChevronOverlayBuilder.PLAN_INVALID_COLOR
const CHEVRON_SIZE := 10.0
const CHEVRON_DIST := 8.0
const MARK_HALF_LENGTH := 60.0

const POINT_RADIUS := 9.0
const POINT_COLOR := Color(0.75, 0.85, 1.0, 0.9)

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

# One mark per ∞ entry: its 1-based plan index, where it was clicked (snapped
# to the edge), and whether the physical trace wraps there.
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

func _draw() -> void:
	var b := bounds
	if _point is Vector2:
		_draw_point_marker(_point, POINT_COLOR, _hovered)
	elif _hovered:
		var w := BAND_WIDTH
		draw_rect(Rect2(b.position, Vector2(b.size.x, w)), HOVER_COLOR)
		draw_rect(Rect2(Vector2(b.position.x, b.end.y - w), Vector2(b.size.x, w)), HOVER_COLOR)
		draw_rect(Rect2(Vector2(b.position.x, b.position.y + w), Vector2(w, b.size.y - 2.0 * w)), HOVER_COLOR)
		draw_rect(Rect2(Vector2(b.end.x - w, b.position.y + w), Vector2(w, b.size.y - 2.0 * w)), HOVER_COLOR)
	for m in _marks:
		var color: Color = PLAN_VALID_COLOR if m["valid"] else PLAN_INVALID_COLOR
		if m.get("on_point", false):
			# the entry's frame moved infinity on screen: the mark is at that point
			var q: Vector2 = m["point"]
			_draw_point_marker(q, Color(color, 0.9), false)
			draw_string(ThemeDB.fallback_font, q + Vector2(POINT_RADIUS + 4.0, -POINT_RADIUS), str(m["index"]), HORIZONTAL_ALIGNMENT_LEFT, -1, 14, Color.WHITE)
			continue
		var snapped := snap_to_edge(m["point"], b)
		var pt: Vector2 = snapped["point"]
		var out: Vector2 = snapped["outward"]
		var along := Vector2(-out.y, out.x)
		# a stretch of the band around the click, the chevron through the edge, the number
		var a := pt - along * MARK_HALF_LENGTH - out * BAND_WIDTH
		var c := pt + along * MARK_HALF_LENGTH
		draw_rect(Rect2(Vector2(minf(a.x, c.x), minf(a.y, c.y)), (c - a).abs()), color)
		var solid := Color(color, 0.9)
		var tip := pt - out * CHEVRON_DIST
		draw_polygon(SurfaceGeom.chevron_vertices(tip, out, CHEVRON_SIZE), PackedColorArray([solid, solid, solid]))
		var label_pos := pt - out * (BAND_WIDTH + 14.0) + Vector2(-5, 5)
		draw_string(ThemeDB.fallback_font, label_pos, str(m["index"]), HORIZONTAL_ALIGNMENT_CENTER, -1, 14, Color.WHITE)

# A circled cross: the image of infinity (a point unlike any surface).
func _draw_point_marker(q: Vector2, color: Color, hovered: bool) -> void:
	var r := POINT_RADIUS * (1.6 if hovered else 1.0)
	draw_arc(q, r, 0, TAU, 32, color, 2.0)
	draw_line(q + Vector2(-r, -r) * 0.7, q + Vector2(r, r) * 0.7, color, 2.0)
	draw_line(q + Vector2(-r, r) * 0.7, q + Vector2(r, -r) * 0.7, color, 2.0)
	if hovered:
		draw_circle(q, BAND_WIDTH, HOVER_COLOR)
