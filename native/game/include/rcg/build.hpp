// A level's surfaces as built (SurfaceSpec.build, the portal pair, the
// pre-transform circles) and as scene defs (SceneDefs.describe_surface: the
// solver's input form, the level dumps bench/scenes/*.surfaces.json).
// Bit-identical to the GDScript game: float32 points (V2), the carriers
// computed in float64 as the game computes them.
#pragma once

#include "rc/scene.hpp"
#include "rc/visual.hpp"
#include "rcg/level.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace rcg {

// A built side (SideConfig): its effect as the game holds it (vectors rounded
// to float32), whether it is plannable, its hit effects.
struct BuiltEffect {
	std::string type = "pass";
	bool back = false;
	V2 normal;          // directional projections
	double theta = 0.0; // portal
	V2 d;               // portal
};

struct BuiltSide {
	BuiltEffect effect;
	bool interactive = false;
	std::vector<HitSpec> hits;
	bool rigid = false; // Stage 156
};

struct BuiltRow {
	std::string value;
	std::optional<BuiltSide> left, right;
};

struct BuiltSurface {
	int id = 0;
	std::optional<std::string> label; // the node prefix (PortalSrc / PortalTgt for a pair); none: not drawn
	rc::arc::SegGeom seg;
	std::optional<BuiltSide> left, right; // fixed sides (none: the pass)
	std::string resolver_key;
	std::vector<BuiltRow> resolver; // non-empty: a categorical surface
	std::vector<PresenceSpec> presence; // authored, then the host conditions of its spawn channels
	bool player_solid = false;
	bool platform = false; // Stage 172: a one-way platform (GAME_SPEC 25.1)
	std::optional<Marker> marker; // Stage 179: a designer's marker (display only)
	std::string shape;            // Stage 181: an object's look (none: its carrier; display only)
	bool pre_transform = false;
	int portal_pair = 0; // a portal: the id of the surface with the inverse motion (0: none)

	// Every side config the surface can show (both sides; every table row).
	std::vector<const BuiltSide *> all_sides() const;
	bool is_target() const;
};

// Stage 179: where a surface's designer marker sits -- a line's middle, an arc's via, a full circle's top.
V2 marker_point(const rc::arc::SegGeom &seg);

// The surfaces of a level in build order. Throws LevelError (a spec that
// does not validate).
std::vector<BuiltSurface> build_level(const Level &level);

// SurfaceSpec.segment(): its three points (a straight line: the exact line;
// a full circle: from its center and radius).
rc::arc::SegGeom segment_of(const SurfaceSpec &s);

// SurfaceSpec.validate ([] = valid) and SurfaceSpec.build: one surface, or a
// portal's pair (ids first_id, first_id + 1).
std::vector<std::string> validate_spec(const SurfaceSpec &s);
std::vector<BuiltSurface> build_spec(const SurfaceSpec &s, int first_id, const std::optional<std::string> &label);

// SceneDefs.describe_surface / defs_from_surfaces (the latter adds each
// portal side's "pair": the surface with the inverse motion).
nlohmann::json describe_surface(const BuiltSurface &s);
nlohmann::json describe_level(const std::vector<BuiltSurface> &surfaces);

// The solver's input (rc::SurfaceDef) of the surfaces: what the game hands
// BeamScene through SceneDefs, built directly (a resolver keeps its level
// order: the first-row fallback depends on it, a JSON object would sort it).
std::vector<rc::SurfaceDef> defs_of(const std::vector<BuiltSurface> &surfaces);

// SceneDefs.describe_config: a side config as scene defs (nullptr: null).
nlohmann::json describe_side(const BuiltSide *cfg, int host_id);

// The flag writes a side makes when it fires (SideConfig.in_shot_writes, as
// scene defs: [{key, op, value}]); host_id: the surface carrying it.
nlohmann::json in_shot_writes(const BuiltSide &side, int host_id);

// Portal: T(z) = e^{i theta} z + d (RigidMotionEffect.apply_motion: float64
// arithmetic on the float32 point, rounded to float32).
V2 apply_motion(const V2 &z, double theta, const V2 &d);

} // namespace rcg
