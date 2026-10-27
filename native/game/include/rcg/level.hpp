// The level data (docs/LEVEL_FORMAT.md): the authoring form of a level -- its
// surfaces as specs in build order with the ids they take, the level's own parts
// (bounds, gravity, room, player, camera, background, colliders) and its initial
// flags. rc-level/2 (elements, rcg/level2.hpp) lowers to rc-level/1 (surfaces).
// Engine-free: any host reads a level through parse_level.
#pragma once

#include "rc/flags.hpp"
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

// A condition on a flag: flag_key(flags[key]) is one of values ("DEFAULT": missing).
struct PresenceSpec {
	std::string key;
	std::vector<std::string> values;
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
	std::vector<PresenceSpec> when; // Stage 156: a state write only while these hold (the side's flags when it fired)
};

struct SideSpec {
	EffectSpec effect;
	bool interactive = false;
	std::vector<HitSpec> hits;
	bool rigid = false; // Stage 156: frost never turns it (a shadow's edge)
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
	bool platform = false; // Stage 172: one-way, stood on from above, dropped through (GAME_SPEC 25.1)

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

// rc-level/2: a region of the plane (a goal, a detector): a rectangle or a circle.
struct Region {
	bool circle = false;
	Rect rect;
	V2 center;
	double radius = 0.0;
	bool contains(const V2 &p) const; // the boundary included
};

// rc-level/2 (Stage 146): an object (GAME_SPEC 25.5) -- a circle body: pullable
// (it falls under gravity, and a pull brings it back along the arrow's path) or
// static; its surface (a full circle) takes the next ids after the level's.
struct Object {
	std::string name;
	V2 center;
	double radius = 16.0;
	bool pullable = true;
	bool solid = true;          // the player collides with it
	std::string face = "wall";  // its sides: wall (terminal) | mirror (reflection) | pass
	std::string label = "Object";
	// Stage 150: bubbles -- its own gravity (a factor of the level's: negative floats up), popped by the
	// arrow (gone for good: the flag obj:<name>:popped), carrying another object until it pops
	double gravity = 1.0;
	bool pops = false;
	std::string carries;
	std::vector<std::string> tags; // Stage 152 (an object conducts, burns, ...)
};

// Stage 152: an element with tags, and its surfaces (indices into Level::surfaces).
struct Tagged {
	std::string name;
	std::vector<std::string> tags;
	std::vector<size_t> surfaces;
};

// Stage 153 (GAME_SPEC 9.9): an element bounding a dark or a light region -- a
// side of its line (left, right) or of its circle (inside, outside; an arc: its
// circle's), active while the flag `when` is true (always when empty). Its
// surface (an index into Level::surfaces) is never present: the shadow's
// boundary is the game's (rcg/shade.hpp).
struct Shade {
	std::string name;
	size_t surface = 0;
	bool dark = true;
	std::string side = "auto"; // auto (left for a line, inside for a circle) | left | right | inside | outside
	std::string when;
};

// Stage 152 (GAME_SPEC 10.6): a spreading rule -- the flag el:<name>:<flag> of
// every element (and object) tagged `tag` that touches, through others so
// tagged, one that has it. Persisting (fire): set ones stay set, through absent
// ones too. Not persisting (charge): recomputed from the present elements with a
// source tag; an absent one touching a lit one is lit (it does not pass it on).
struct SpreadRule {
	std::string flag;
	std::string tag;
	bool persist = true;
	std::vector<std::string> sources;
};

// rc-level/2 (Stage 151): an element's motion over the level's cycle -- keys
// (phase in [0, 1), an offset dx, dy, an angle in radians about the pivot),
// interpolated linearly and looping; its surfaces (indices into
// Level::surfaces) move together.
struct Motion {
	std::vector<size_t> surfaces;
	std::vector<std::array<double, 4>> keys;
	std::optional<V2> pivot; // default: the first surface's middle (a circle: its center)
};
struct Cycle {
	double period = 4.0;       // seconds per cycle while it runs on its own
	double resume_after = 10.0; // seconds of no scroll and no plan change before a paused cycle runs again
};

// rc-level/2: the level's goal (GAME_SPEC 3.2): the player inside the region
// while every condition holds (the presence form, GAME_SPEC 9.6) wins.
struct Objective {
	Region goal;
	std::vector<PresenceSpec> conditions; // (its JSON key: "requires")
};

// rc-level/2: a detector (GAME_SPEC 24.5): a region the player is in, or a
// segment the player crosses, that writes a flag -- while inside (unset
// outside), on entering, or toggled on entering.
struct Detector {
	std::string name;
	std::string target = "player"; // "for": player | object | any (objects: a later stage)
	bool segment = false;
	Region region;
	V2 a, b;          // a segment's ends
	std::string flag; // default det:<name>
	std::string mode = "while"; // while | enter | toggle
	rc::FlagValue value = true;
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
	// rc-level/2: the flags the level starts with (in key order; a number as a float64)
	std::vector<std::pair<std::string, rc::FlagValue>> flags;
	// rc-level/2: the goal (none: the level is won when every target is hit), the detectors
	std::optional<Objective> objective;
	std::vector<Detector> detectors;
	std::vector<Object> objects; // Stage 146
	std::vector<Motion> motions; // Stage 151
	Cycle cycle;
	std::vector<Tagged> tagged;     // Stage 152
	std::vector<SpreadRule> spread; // ... (default: burnt through flammable; charge through conductive from powered)
	std::vector<Shade> shades;      // Stage 153
};

// Stage 146: an object's surface (a full circle at `center`, its sides by its face, a pull on them if pullable).
SurfaceSpec object_spec(const Object &o, const V2 &center);

// The level in a JSON text (rc-level/1 or rc-level/2). Throws LevelError.
Level parse_level(std::string_view json_text);
// ... in a JSON document.
Level parse_level_json(const nlohmann::json &doc);

// The level of a file. Throws LevelError (an unreadable file too).
Level load_level(const std::string &path);

} // namespace rcg
