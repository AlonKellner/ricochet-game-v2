class_name DrawRec
extends RefCounted

# Stage 118: every draw call of the game's nodes goes through here -- it draws,
# and while `enabled` (the session recorder) also logs the call on the node
# (meta "draw_log"), so the native painters (rcg/render.hpp) can be checked
# call for call against the GDScript ones. A node's _draw starts with begin().
# The logged arguments are the calls' own: points and colours as Godot takes them.

static var enabled := false

static func begin(ci: CanvasItem) -> void:
	if enabled:
		ci.set_meta("draw_log", [])

static func _log(ci: CanvasItem, entry: Array) -> void:
	if enabled:
		if not ci.has_meta("draw_log"):
			ci.set_meta("draw_log", [])
		ci.get_meta("draw_log").append(entry)

static func line(ci: CanvasItem, from: Vector2, to: Vector2, color: Color, width := -1.0) -> void:
	ci.draw_line(from, to, color, width)
	_log(ci, ["line", from, to, color, width])

static func arc(ci: CanvasItem, center: Vector2, radius: float, start_angle: float, end_angle: float, point_count: int, color: Color,
		width := -1.0) -> void:
	ci.draw_arc(center, radius, start_angle, end_angle, point_count, color, width)
	_log(ci, ["arc", center, radius, start_angle, end_angle, point_count, color, width])

static func polygon(ci: CanvasItem, points: PackedVector2Array, colors: PackedColorArray) -> void:
	ci.draw_polygon(points, colors)
	_log(ci, ["polygon", points, colors])

static func colored_polygon(ci: CanvasItem, points: PackedVector2Array, color: Color) -> void:
	ci.draw_colored_polygon(points, color)
	_log(ci, ["colored_polygon", points, color])

static func circle(ci: CanvasItem, center: Vector2, radius: float, color: Color) -> void:
	ci.draw_circle(center, radius, color)
	_log(ci, ["circle", center, radius, color])

static func rect(ci: CanvasItem, r: Rect2, color: Color) -> void:
	ci.draw_rect(r, color)
	_log(ci, ["rect", r.position, r.size, color])

static func polyline(ci: CanvasItem, points: PackedVector2Array, color: Color, width := -1.0) -> void:
	ci.draw_polyline(points, color, width)
	_log(ci, ["polyline", points, color, width])

static func multiline(ci: CanvasItem, points: PackedVector2Array, color: Color, width := -1.0) -> void:
	ci.draw_multiline(points, color, width)
	_log(ci, ["multiline", points, color, width])

static func dashed_line(ci: CanvasItem, from: Vector2, to: Vector2, color: Color, width: float, dash: float) -> void:
	ci.draw_dashed_line(from, to, color, width, dash)
	_log(ci, ["dashed_line", from, to, color, width, dash])

# draw_string with the theme's fallback font: alignment 0 left, 1 center
static func text(ci: CanvasItem, pos: Vector2, s: String, alignment: int, font_size: int, color: Color) -> void:
	ci.draw_string(ThemeDB.fallback_font, pos, s, alignment, -1, font_size, color)
	_log(ci, ["text", pos, s, alignment, font_size, color])

static func triangles(ci: CanvasItem, indices: PackedInt32Array, points: PackedVector2Array, color: Color) -> void:
	RenderingServer.canvas_item_add_triangle_array(ci.get_canvas_item(), indices, points, PackedColorArray([color]))
	_log(ci, ["triangles", points, indices, color])
