// Stage 122: the presentation layer (docs/TDD_27_PRESENTATION.md). Between
// the session and the draw list: what is on screen at time t, animated from
// the game's state, never changing it. The game's results, the record and the
// session golden are the session's alone; a host that wants animation draws the
// presenter's frame (the C API: rc_game_config.animate).
//
// - The player is drawn between its last two physics ticks (the accumulator's
//   remainder), so motion is smooth at any display rate.
// - A late visibility region morphs in as a diff of two shapes (the user's design,
//   2026-10-21): each region is its outline -- polygons of straight edges, every
//   vertex tagged with the surfaces it lies on -- and the old and new outlines
//   are aligned like a diff (a cyclic sequence alignment, dynamic programming):
//   a vertex paired with its counterpart moves to it in a straight line, one
//   with none moves to (or from) the closest point of the other outline between
//   its paired neighbours. Every frame is polygons with straight edges. A newer
//   region retargets the morph from the shape on screen; a morph ends on the
//   exact region.
// - While a region is late, the shown one follows the drawn player where it can
//   (its apex is the player): a shadow edge pivots about its occluding corner,
//   its far end recast from the player onto its wall.
// - On time (nothing late: the region came within the frame's budget), nothing
//   is smoothed: the session's region is drawn as it is.
#pragma once

#include "rc/region_union.hpp"
#include "rc/visibility.hpp"
#include "rcg/render.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace rcg {

class Session;
class World;

// The smoothing covers for lateness only (the user, 2026-10-21): a region that
// arrives on time (the frame's budget, SessionConfig::budget) is drawn as it is;
// only while a region is late (Session::region_pending) does the shown one
// follow the player, and the late one, when it arrives, morphs in.
struct PresentConfig {
	double region_seconds = 0.1; // a late region's morph (0: none)
	// Stage 124 (the user): a region is late -- smoothed: followed while it computes,
	// morphed in when it arrives -- only once it has computed this long (slower
	// than 30 Hz); one that arrives sooner is drawn as it is
	double late_seconds = 1.0 / 30.0;
	bool interpolate = true;     // the player between physics ticks
	bool follow = true;          // while a region is late, the shown one's shadows follow the drawn player
	bool smooth_on_time = false; // (tests) smooth as if every region were late
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
// A region's outline: closed simple polygons (the union of its cells), their
// carriers, and the apex the shadows lie on rays from (has_apex: the player).
struct Shape {
	std::vector<Ring> rings; // outer counterclockwise, holes clockwise
	std::vector<Carrier> carriers;
	bool has_apex = false;
	Pt apex;
};

// A region's shape: its outline -- the union of its cells (Session::region_outline,
// rc::vis::union_outline): outer rings counterclockwise, holes clockwise --
// straight runs merged and arcs reduced to chords within `tol`, every vertex
// tagged by the carriers.
Shape shape_of(const rc::vis::Outline &o, std::vector<Carrier> carriers, double tol = 0.5);
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
// What two shapes both cover (Clipper2): the part of a morph that stays lit.
std::vector<std::vector<Pt>> common(const Shape &a, const Shape &b);
// A shape's fill united with `also` (a morph frame with the part it keeps lit:
// a slit the morph opens there is covered; Clipper2, then earcut).
void fill_with(const Shape &s, const std::vector<std::vector<Pt>> &also, std::vector<rc::V2> &pts, std::vector<int> &indices);
// The holes' outer rings: a clockwise ring is a hole of the smallest counterclockwise
// ring around it (-1: an outer ring; -2: a hole of nothing).
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
		double seconds = 0.0; // its duration (a retarget on time: the rest of the one it continues)
		std::vector<std::vector<rc::vis::Pt>> stable; // what the old and new shapes both cover: lit throughout
	};
	PresentConfig config_;
	double now_ = 0.0;
	int region_version_ = -1;  // the session's region shown (or morphed to)
	shape_morph::Shape target_; // its outline
	shape_morph::Shape shown_;  // the outline on screen (the last frame's)
	std::optional<Morph> morph_;
	// Stage 124: when each region request was first seen (the clock), from the shown one on
	std::map<long long, double> requested_at_;
	uint64_t frame_serial_ = 0;
};

} // namespace rcg
