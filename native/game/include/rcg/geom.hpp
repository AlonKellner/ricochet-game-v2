// Stage 116.4: crossings and pieces (GAME_SPEC 9.7; the port of Crossings and
// Pieces). Proper crossings of two segments -- interior to both, transversal;
// an endpoint exactly on the other carrier (a corner, a T-junction) is decided
// by the exact predicates, never a tolerance -- and the plannable pieces of a
// world's surfaces split at them. GDScript's precision is kept expression by
// expression: a Vector2 component read is a float64, Vector2 arithmetic is
// float32, fposmod as the engine computes it.
#pragma once

#include "rc/visual.hpp"
#include "rcg/build.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace rcg {

using rc::arc::SegGeom;

constexpr double TAU = 6.283185307179586; // GDScript's TAU

// Math::fposmod (float64): the result in [0, y) for y > 0, never -0.0.
double fposmod(double x, double y);

// Crossings.param: a point's position along the segment's carrier (a line's
// fraction from start to end; a circle's angle from start in the travel direction).
double param(const SegGeom &seg, const V2 &p);
// Crossings.span: the end's position (a full circle: TAU; a line: 1).
double span(const SegGeom &seg);
// Crossings.incident: exactly on the segment's carrier.
bool incident(const SegGeom &seg, const V2 &p);

// Pieces._between: a point on the carrier between a and b (along its orientation).
V2 between(const SegGeom &seg, const V2 &a, const V2 &b);

struct Crossing {
	V2 point;          // rounded once to float32
	double ta = 0, tb = 0;
	bool enters = false; // along a's orientation, a enters b here
};
// Crossings.of
std::vector<Crossing> crossings_of(const SegGeom &a, const SegGeom &b);

// A plannable piece of a design surface (index -1: the whole surface, no span).
struct Piece {
	int design = 0;
	int index = -1;
	SegGeom segment;
	std::optional<std::array<double, 6>> span; // [sx, sy, ex, ey, vx, vy]
	// Piece.contains: a point on the carrier lies on the piece
	bool contains(const V2 &p) const;
};

// Pieces.build over a world's surfaces (presence ignored; portals and
// pre-transform circles are neither split nor cut).
struct Pieces {
	std::vector<std::pair<int, std::vector<Piece>>> by_id; // in the surfaces' order
	std::vector<V2> crossings;
	std::vector<std::string> portal_crossings;

	const std::vector<Piece> &of(int id) const;
	const Piece *piece(int id, int index) const;
};
Pieces build_pieces(const std::vector<BuiltSurface> &surfaces);

// Pieces.span_contains: a point lies in a plan entry's span on its surface's carrier.
bool span_contains(const BuiltSurface &s, const std::array<double, 6> &span, const V2 &p);

} // namespace rcg
