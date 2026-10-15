class_name EdgeOverlayNode
extends Node2D

# The level bounds' edge as a plannable "surface" (Stage 88d, GAME_SPEC 4.2):
# hovering the band inside the edge highlights it; planned ∞ entries are marked
# on the edge at the point they were clicked (a chevron pointing out through
# the edge, and the entry's number), in the plan's valid or invalid colour as
# the planned sides are.

const BAND_WIDTH := ClickDetector.CLICK_TOLERANCE
const HOVER_COLOR := ChevronOverlayBuilder.HOVER_COLOR
const PLAN_VALID_COLOR := ChevronOverlayBuilder.PLAN_VALID_COLOR
const PLAN_INVALID_COLOR := ChevronOverlayBuilder.PLAN_INVALID_COLOR
const CHEVRON_SIZE := 10.0
const CHEVRON_DIST := 8.0
const MARK_HALF_LENGTH := 60.0

var bounds := Rect2(0, 0, 1920, 1080)
var _hovered := false
var _marks: Array = []   # [{index: int, point: Vector2, valid: bool}]

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
	if _hovered:
		var w := BAND_WIDTH
		draw_rect(Rect2(b.position, Vector2(b.size.x, w)), HOVER_COLOR)
		draw_rect(Rect2(Vector2(b.position.x, b.end.y - w), Vector2(b.size.x, w)), HOVER_COLOR)
		draw_rect(Rect2(Vector2(b.position.x, b.position.y + w), Vector2(w, b.size.y - 2.0 * w)), HOVER_COLOR)
		draw_rect(Rect2(Vector2(b.end.x - w, b.position.y + w), Vector2(w, b.size.y - 2.0 * w)), HOVER_COLOR)
	for m in _marks:
		var color: Color = PLAN_VALID_COLOR if m["valid"] else PLAN_INVALID_COLOR
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
