// Stage 116.4: what a shot does when the arrow lands (GAME_SPEC 10.6; the port
// of LandingFold, SpawnFrame, SpawnTemplate.place, SurfaceSpec / SideSpec.mapped):
// a pure fold over the physical trace's hit records -- the deferred writes,
// the spawns placed at the hits that last fired them (a template mapped
// through the spawn frame, trimmed by what is present), the channels'
// generations. Nothing here touches the solver or the world.
#pragma once

#include "rc/flags.hpp"
#include "rc/trace_types.hpp"
#include "rc/xf2.hpp"
#include "rcg/build.hpp"
#include "rcg/geom.hpp"

#include <map>
#include <string>
#include <vector>

namespace rcg {

// SurfaceSpec.mapped: the spec through a rigid frame (a reflecting frame swaps
// the sides; a portal's motion is conjugated; a directional normal is mapped
// by the basis).
SurfaceSpec mapped(const SurfaceSpec &s, const rc::Xf2 &xf);

enum class SpawnMode { BeamTangent, BeamNormal, CarrierTangent, CarrierNormal, BeamReflected, World };
SpawnMode spawn_mode(const std::string &name); // SpawnFrame.Mode by name

// SpawnFrame.carrier_tangent / axis / frame
V2 carrier_tangent(const V2 &start, const V2 &via, const V2 &end, bool is_line, const V2 &center, const V2 &point);
V2 spawn_axis(SpawnMode mode, const V2 &beam, const V2 &tangent);
rc::Xf2 spawn_frame(SpawnMode mode, const V2 &point, const V2 &beam, const V2 &tangent, bool from_right, bool mirror_with_side);

// LandingFold.place: a template placed at a hit on its host.
SurfaceSpec place(const SpawnTemplateSpec &t, const BuiltSurface &host, const rc::HitRecord &hit);
// LandingFold.trim: the piece containing its midpoint (a full circle: its
// longest arc), split by its proper crossings with the cutters.
SurfaceSpec trim(const SurfaceSpec &spec, const std::vector<SegGeom> &cutters);

struct Placed {
	std::string channel;
	int generation = 0;
	SurfaceSpec spec;
};

struct Landing {
	rc::Flags flags;                        // to promote
	std::vector<Placed> placed;             // by channel name
	std::map<std::string, int> generations; // channel -> the last generation landed
};

// LandingFold.fold: hits (TracePath.hits), the world the shot was traced in,
// the shot's final flags, the generations so far.
Landing fold(const std::vector<rc::HitRecord> &hits, const std::vector<BuiltSurface> &world, const rc::Flags &final_flags,
		const std::map<std::string, int> &generations);

} // namespace rcg
