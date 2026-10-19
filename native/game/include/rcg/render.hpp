// Stage 118: what the game draws, natively -- the draw list. One node per
// thing the GDScript level drew (a surface piece, the edge, the visibility
// region, the spawn preview, the path, the arrow, the cursor, the player, the
// background, the HUD), each with its z-index, position, modulate and the
// calls it makes, in CanvasItem terms: a host maps every call onto its own
// 2D drawing (Godot: the same draw_* call). The port of every _draw; checked
// call for call against the GDScript nodes (DrawRec, the session digest's
// `draw`).
#pragma once

#include "rc/trace_types.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <string>
#include <vector>

namespace rcg::render {

using rc::V2;
using Color = std::array<float, 4>;

enum class Kind {
	Line,           // pts {from, to}, color, width
	Arc,            // pts {center}, radius, start, end, point_count, color, width
	Polygon,        // pts, colors (one per point)
	ColoredPolygon, // pts, color
	Circle,         // pts {center}, radius, color
	Rect,           // pts {position, size}, color
	Polyline,       // pts, color, width
	Multiline,      // pts (pairs), color, width
	DashedLine,     // pts {from, to}, color, width, dash
	Text,           // pts {position}, text, alignment (0 left, 1 center), font_size, color
	Triangles,      // pts, indices, color
	Polygon2D,      // pts (the polygon, in the node's frame), color, rotation
	Label,          // text (screen space, top left)
};

struct Call {
	Kind kind = Kind::Line;
	std::vector<V2> pts;
	std::vector<Color> colors;     // the colour (one), or one per point (Polygon)
	std::vector<int> indices;      // Triangles
	double radius = 0, start = 0, end = 0, width = -1.0, dash = 0, rotation = 0;
	int point_count = 0, alignment = 0, font_size = 0;
	std::string text;
};

struct Node {
	nlohmann::json key;  // ["surface", id, piece], ["path"], ...
	int z = 0;
	V2 position;
	Color modulate{1, 1, 1, 1};
	std::vector<Call> calls;
};

// A call / the draw list as the session digest records them (DrawRec's logs).
nlohmann::json call_json(const Call &c);
nlohmann::json list_json(const std::vector<Node> &nodes);

// Godot's named colours (Color::hex) and helpers
Color hex(std::uint32_t rgba);
Color with_alpha(const Color &c, double a); // Color(c, a)

} // namespace rcg::render
