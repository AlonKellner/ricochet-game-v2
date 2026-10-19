// The level data, rc-level/1 (docs/LEVEL_FORMAT.md): the authoring form of a
// level -- its surfaces as specs in build order with the ids they take, and
// the level's own parts (bounds, gravity, room, player, camera, background,
// colliders). Engine-free: any host reads a level through parse_level.
#pragma once

#include "rc/trace_types.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rcg {

using rc::V2;

// A level that does not follow the schema: the path of the offending value.
struct LevelError : std::runtime_error {
	using std::runtime_error::runtime_error;
};

// A flag value as authored: a bool, an int, a float or a string.
using FlagValue = std::variant<bool, std::int64_t, double, std::string>;

// A side's path effect (EffectSpec: the scene-def form). Vectors are kept as
// authored (float64); the built effect rounds them to the game's float32.
struct EffectSpec {
	nlohmann::json raw = {{"type", "pass"}}; // the dictionary as authored (the session digest records it)
	std::string type = "pass";
	bool back = false;
	std::optional<std::array<double, 2>> normal; // directional projections
	std::optional<double> theta;                 // portal
	std::optional<std::array<double, 2>> d;      // portal
};

struct SurfaceSpec;

// What a spawn places (SpawnTemplate): a surface in a local frame.
struct SpawnTemplateSpec {
	std::shared_ptr<SurfaceSpec> surface;
	std::string orientation = "CARRIER_TANGENT";
	bool mirror_with_side = false;
	V2 offset;
	bool trim = true;
};

// A hit effect (HitEffectSpec): state, target, reset or spawn.
struct HitSpec {
	nlohmann::json raw; // the dictionary as authored
	std::string kind;
	bool deferred = false;
	std::string key;                    // state
	std::string op = "set";             // state: set | toggle
	FlagValue value = true;             // state
	std::vector<std::string> channels;  // reset
	std::string channel;                // spawn
	std::shared_ptr<SpawnTemplateSpec> spawn_template;
};

struct SideSpec {
	EffectSpec effect;
	bool interactive = false;
	std::vector<HitSpec> hits;
};

struct PresenceSpec {
	std::string key;
	std::vector<std::string> values;
};

// One row of a categorical resolver: a flag value and the sides under it.
struct ResolverRow {
	std::string value;
	std::optional<SideSpec> left;
	std::optional<SideSpec> right;
};

// A surface as data (SurfaceSpec): three points, or a full circle.
struct SurfaceSpec {
	bool full_circle = false;
	V2 start, end, via;
	V2 center;
	double radius = 0.0;
	bool straight = false;      // an exact line through start and end
	bool pre_transform = false; // a pre-transform inversion circle (no sides)
	std::optional<SideSpec> left, right;
	std::string resolver_key;
	std::vector<ResolverRow> resolver; // empty: fixed sides
	std::vector<PresenceSpec> presence;
	bool player_solid = false;

	bool is_portal() const { return resolver.empty() && left && left->effect.type == "portal"; }
};

// A surface of a level: its spec, the ids it takes (a portal two: the source
// and its image) and its label (the node prefix; none: not drawn).
struct LevelSurface {
	std::vector<int> ids;
	std::optional<std::string> label;
	SurfaceSpec spec;
};

struct Rect {
	double x = 0, y = 0, w = 0, h = 0;
};

struct Collider {
	V2 a, b;
};

struct Level {
	std::string name;
	Rect bounds{0, 0, 1920, 1080};
	V2 gravity;
	Rect room{560, 240, 800, 600};
	bool room_built = true;
	std::optional<V2> player_start;
	struct Camera {
		V2 start;
		bool smoothing = false;
		double smoothing_speed = 5.0;
	};
	std::optional<Camera> camera;
	struct Background {
		Rect rect;
		std::array<double, 4> color{0, 0, 0, 1};
	};
	std::optional<Background> background;
	std::vector<Collider> colliders;
	struct Features {
		bool game = true, preview = true, arrow = true, hud = true, cursor = true;
	} features;
	std::vector<LevelSurface> surfaces;
};

// The level in a JSON text (rc-level/1). Throws LevelError.
Level parse_level(std::string_view json_text);

// The level of a file. Throws LevelError (an unreadable file too).
Level load_level(const std::string &path);

} // namespace rcg
