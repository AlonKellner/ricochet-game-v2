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
#include <cstdint>
#include <memory>
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
	Label,          // text: the HUD, a Label at (10, 10) (the default theme font, white)
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

// Stage 120: a node's identity without JSON -- its kind and ids -- and a node
// of a frame whose calls are shared (a painter's cached output) with the
// content version a host compares to skip what did not change.
enum class NodeKind : uint8_t { Background, Player, Cursor, Path, Arrow, Hud, Surface, Edge, Visibility, Spawn };
struct NodeKey {
	NodeKind kind = NodeKind::Background;
	int a = 0, b = 0; // a surface's id and piece
	uint64_t id() const;              // stable while the node lives
	nlohmann::json json() const;      // ["surface", 3, -1], ["path"], ... (the digest's key)
	bool operator==(const NodeKey &) const = default;
};
using Calls = std::shared_ptr<const std::vector<Call>>;
struct FrameNode {
	NodeKey key;
	int z = 0;
	V2 position;
	Color modulate{1, 1, 1, 1};
	Calls calls;
	uint64_t version = 0; // a hash of the calls (equal calls, equal version)
};
uint64_t content_hash(const std::vector<Call> &calls);

// The draw order: by z, the game's order (Session::draw: the level's tree
// order) within a z (stable).
void sort(std::vector<Node> &nodes);

// A call / the draw list as the session digest records them (DrawRec's logs;
// sorted by z, then the key's encoding).
nlohmann::json call_json(const Call &c);
nlohmann::json list_json(const std::vector<Node> &nodes);

// An arc as Godot's CanvasItem::draw_arc strokes it: point_count points from
// the start angle to the end (the span clamped to a full turn), float32
// arithmetic (a host without an arc primitive draws this polyline).
std::vector<V2> tessellate_arc(const V2 &center, double radius, double start, double end, int point_count);

// Godot's named colours (Color::hex) and helpers
Color hex(std::uint32_t rgba);
Color with_alpha(const Color &c, double a); // Color(c, a)

} // namespace rcg::render
