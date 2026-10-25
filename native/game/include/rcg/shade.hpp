// Stage 153 (GAME_SPEC 9.9): light and shadow. A shade element bounds a region --
// a side of its line, the inside or the outside of its circle -- dark or light,
// active while its flag holds. A point is in the shadow when it is in an active
// dark region and in no active light one. The shadow's boundary stops the arrow
// coming from the light (its lit side terminal, its shadowed side a pass), and a
// surface lying wholly in the shadow is darkened: a pass for the arrow, still
// solid for the player. The game's layer computes both from the regions; the
// solver sees ordinary surfaces.
#pragma once

#include "rc/visibility.hpp"
#include "rcg/build.hpp"
#include "rcg/level.hpp"

#include <string>
#include <utility>
#include <vector>

namespace rcg::shade {

// A shade's region: a half-plane or a disk (or its outside).
struct Region {
	bool dark = true;
	bool line = true;
	double sx = 0, sy = 0, ex = 0, ey = 0; // a line: the element's two points (float32 values)
	double nx = 0, ny = 0;                 // ... the unit normal into the region
	double cx = 0, cy = 0, r = 0;          // a circle
	bool inside = true;                    // ... the region: the disk (else its outside)
	bool contains(double x, double y) const; // strictly (the boundary is in neither)
};

// The region an element's geometry bounds on `side` (auto: left for a line, inside
// for a circle; a circle's left and right are its surface's sides).
Region region_of(const rc::arc::SegGeom &g, const std::string &side, bool dark);

// In the shadow: in an active dark region and in no active light one.
bool shadowed(const std::vector<Region> &regions, const std::vector<bool> &active, double x, double y);

// The regions' boundaries -- the lines clipped to the bounds, the circles whole --
// split where they cross: the pieces the shadow's boundary is made of, whichever
// regions are active (so their ids are fixed). Regions on one carrier share it.
struct Piece {
	SurfaceSpec spec; // its geometry
	double mx = 0, my = 0; // a point of it, off every other carrier
	std::vector<std::pair<size_t, bool>> own; // the regions on its carrier: whether its left side is in each
};
std::vector<Piece> arrange(const std::vector<Region> &regions, const Rect &bounds);

// The pieces between the shadow and the light under `active`: the lit side a
// terminal, the shadowed side a pass, neither plannable, not drawn (id: first_id
// plus the piece's index).
std::vector<BuiltSurface> boundary(const std::vector<Piece> &pieces, const std::vector<Region> &regions, const std::vector<bool> &active,
		int first_id);

// The shadow within the bounds, triangulated (the display's fill).
rc::vis::Fill fill(const std::vector<Region> &regions, const std::vector<bool> &active, const Rect &bounds);

// A point of a surface (a line's middle, an arc's via, a point of a circle).
V2 point_of(const rc::arc::SegGeom &g);

// A surface in the shadow: both sides a pass with no hits (its presence and solidity kept).
BuiltSurface darkened(const BuiltSurface &s);

} // namespace rcg::shade
