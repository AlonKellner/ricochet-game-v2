// Stage 122: the presentation layer (docs/TDD_27_PRESENTATION.md). Between
// the session and the draw list: what is on screen at time t, animated from
// the game's state, never changing it. The game's results, the record and the
// session golden are the session's alone; a host that wants animation draws the
// presenter's frame (the C API: rc_game_config.animate).
//
// - The player is drawn between its last two physics ticks (the accumulator's
//   remainder), so motion is smooth at any display rate.
// - The visibility region morphs as a diff of two shapes (the user's design,
//   2026-10-21): each region is its outline -- polygons of straight edges, every
//   vertex tagged with the surfaces it lies on -- and the old and new outlines
//   are aligned like a diff (a cyclic sequence alignment, dynamic programming):
//   a vertex paired with its counterpart moves to it in a straight line, one
//   with none moves to (or from) the closest point of the other outline between
//   its paired neighbours. Every frame is polygons with straight edges. A newer
//   region retargets the morph from the shape on screen; a morph ends on the
//   exact region.
// - While no newer region has arrived, the region follows the drawn player at
//   once where it can (its apex is the player): a shadow edge pivots about its
//   occluding corner, its far end recast from the player onto its wall.
#pragma once

#include "rc/visibility.hpp"
#include "rcg/render.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace rcg {

class Session;
class World;

struct PresentConfig {
	double region_seconds = 0.1; // the region's morph (0: none)
	bool interpolate = true;     // the player between physics ticks
	bool follow = true;          // the region's shadows follow the drawn player before its next region
};

namespace shape_morph {

using Pt = rc::vis::Pt;

// A surface's carrier (the tags' source; a straight one also its segment, for
// recasting a shadow's end onto it).
struct Carrier {
	int surface = -1;
	bool line = true;
	Pt p, d; // a line
	Pt c;    // a circle
	double r = 0.0;
	bool has_segment = false;
	Pt s, e; // the surface's segment (a straight one)
	double dist(const Pt &q) const;
};

// A vertex of an outline: its point, the surfaces it lies on (a tag: 0 none),
// and how it follows the player (Stage 122: a shadow's far end recast through
// its pivot onto its carrier; the apex itself).
struct Vertex {
	Pt p;
	int64_t tag = 0;
	int carrier = -1;  // the one straight carrier it lies on (an index), or -1
	bool shadow = false;
	Pt pivot;          // a shadow's occluding corner (on the ray from the apex)
	bool apex = false; // the apex itself
};
using Ring = std::vector<Vertex>;
// A region's pieces: closed simple polygons (its cells), their carriers, and the
// apex the shadows lie on rays from (has_apex: the player).
struct Shape {
	std::vector<Ring> rings;
	std::vector<Carrier> carriers;
	bool has_apex = false;
	Pt apex;
};

// A region as pieces: its cells' rings as its fill makes them (each a simple
// polygon, counterclockwise; cells of a pencil's two sheets may overlap, as the
// fill draws them), straight runs merged and arcs reduced to chords within
// `tol`, every vertex tagged by the carriers.
Shape shape_of(const rc::vis::Result &r, std::vector<Carrier> carriers, double tol = 0.5);
// The carriers of a region (its cells' carrier edges; a straight surface's segment from the world, if given).
std::vector<Carrier> carriers_of(const rc::vis::Result &r, const World *world = nullptr);
// The shadows of a shape whose rays leave `apex` (the player): an edge on a ray
// from it is a shadow's edge, its far end recast through its near end (the
// occluding corner) onto its straight carrier as the apex moves.
void mark_shadows(Shape &s, const Pt &apex);
// A vertex for the apex moved to `apex` (a shadow's end recast while it stays on its surface).
Vertex recast(const Vertex &v, const std::vector<Carrier> &carriers, const Pt &apex);
// The shape with its shadows recast from the apex moved to `apex` (no apex: as it is).
Shape follow(const Shape &s, const Pt &apex);

// A morph: per ring, every vertex's way (from -> to) in order, and which ring
// each is the hole of (-1: an outer ring).
struct Track {
	Vertex from, to;
};
struct Plan {
	std::vector<std::vector<Track>> rings;
	std::vector<int> parent;
};
// The diff of two shapes: rings paired by place, then each pair aligned; a
// ring with no partner moves from (to) the closest points of the other shape,
// or the anchor when it is empty.
Plan plan(const Shape &a, const Shape &b, const Pt &anchor);
// The shape at e of the way (0: a, 1: b).
Shape at(const Plan &p, double e);
// A shape's fill: triangles (points, three indices a triangle).
void fill_of(const Shape &s, std::vector<rc::V2> &pts, std::vector<int> &indices);
// The holes' outer rings, by containment (-1: an outer ring).
std::vector<int> parents_of(const Shape &s);

} // namespace shape_morph

class Presenter {
public:
	explicit Presenter(PresentConfig config = {});
	void advance(double dt); // the presenter's clock
	// the frame on screen now: the session's frame, its player between ticks and
	// its region morphing (an unchanged node keeps its version)
	std::vector<render::FrameNode> frame(std::vector<render::FrameNode> nodes, const Session &s);
	bool animating() const { return morph_.has_value(); }
	// where the player is drawn
	rc::V2 player(const Session &s) const;
	const PresentConfig &config() const { return config_; }

private:
	struct Morph {
		shape_morph::Plan plan;
		double start = 0.0;
	};
	PresentConfig config_;
	double now_ = 0.0;
	int region_version_ = -1;  // the session's region shown (or morphed to)
	shape_morph::Shape target_; // its outline
	shape_morph::Shape shown_;  // the outline on screen (the last frame's)
	std::optional<Morph> morph_;
	uint64_t frame_serial_ = 0;
};

} // namespace rcg
