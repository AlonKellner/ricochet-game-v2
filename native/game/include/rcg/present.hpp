// Stage 122: the presentation layer (docs/TDD_27_PRESENTATION.md). Between
// the session and the draw list: what is on screen at time t, animated from
// the game's state, never changing it. The game's results, the record and the
// session golden are the session's alone; a host that wants animation draws the
// presenter's frame (the C API: rc_game_config.animate).
//
// - The player is drawn between its last two physics ticks (the accumulator's
//   remainder), so motion is smooth at any display rate.
// - The visibility region morphs from the shape shown to a new one, with crisp
//   edges and no blending. Each region is sampled along directions from its
//   apex (P_K: the player, or its image behind the planned surfaces); a
//   sample's visible spans end on surfaces, and an end found on the same
//   surface before and after slides along that surface (a circle's ends along
//   the circle), an end on another surface along the sample's ray; a span
//   with no partner grows out of (or shrinks into) its near end. A region
//   whose apex jumped, or one the sampling does not cover (a pencil of
//   circles, of parallels), collapses into the player, then the new one grows
//   out of it. A morph retargets from the shape on screen when a newer region
//   arrives (walking), and ends on the exact region.
#pragma once

#include "rc/visibility.hpp"
#include "rcg/render.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace rcg {

class Session;

struct PresentConfig {
	double region_seconds = 0.1;  // the region's morph (0: none)
	double collapse_share = 0.4;  // a morph with nothing matched: the old region collapses in this share, the new grows in the rest
	double match_distance = 64.0; // an apex that moved further: nothing matched
	bool interpolate = true;      // the player between physics ticks
};

namespace region_field {

using Pt = rc::vis::Pt;

// An end of a visible span along a sample ray: on a surface's carrier (its
// geometry: a line, or a circle to slide along), on the domain's edge, or the apex.
struct End {
	enum Kind : uint8_t { Apex, Carrier, Bounds } kind = Apex;
	Pt p;
	int surface = -1;
	bool circle = false;
	Pt center;
	double radius = 0.0;
};

// A visible span along one sample ray, and its identity (the same span at
// neighbouring samples is one strip of the fill).
struct Span {
	End near, far;
	int64_t id = 0;
};

struct Sample {
	double phi = 0.0; // the ray's angle at the apex, [0, 2 pi)
	std::vector<Span> spans; // by distance from the apex
};

// A region as samples along rays from its apex. ok false: not sampled (a
// pencil the sampling does not cover); an empty, ok field is no region.
struct Field {
	bool ok = false;
	bool empty = true;
	Pt apex;
	std::vector<Sample> samples; // by phi
};

// The field of a region (720 rays, and every cell's edges from inside).
Field field_of(const rc::vis::Result &r);
// The spans of a field along the ray at phi (between samples: interpolated).
std::vector<Span> spans_at(const Field &f, double phi);
// The field e of the way from a to b (e in [0, 1]; 0: a, 1: b).
Field morph(const Field &a, const Field &b, double e);
// The fill of a field: a triangle mesh (points, three indices a triangle).
void fill_of(const Field &f, std::vector<rc::V2> &pts, std::vector<int> &indices);
// A triangle mesh clipped to the disk (c, r) (a polygon of `sides`).
void clip_disk(const std::vector<rc::V2> &pts, const std::vector<int> &indices, const Pt &c, double r, std::vector<rc::V2> &out_pts,
	std::vector<int> &out_indices, int sides = 96);

} // namespace region_field

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
		region_field::Field from, to;          // a field morph
		bool disk = false;                // nothing matched: collapse into the player, then grow
		std::vector<rc::V2> from_pts, to_pts; // a disk morph's fills
		std::vector<int> from_idx, to_idx;
		double start = 0.0;
	};
	PresentConfig config_;
	double now_ = 0.0;
	int region_version_ = -1;      // the session's region the morph runs to
	region_field::Field shown_;         // the field of the region shown when no morph runs
	std::vector<rc::V2> shown_pts_; // ... and its fill
	std::vector<int> shown_idx_;
	std::optional<Morph> morph_;
	region_field::Field current_;       // the morph's field this frame
	bool current_ok_ = false;
	uint64_t frame_serial_ = 0;
};

} // namespace rcg
