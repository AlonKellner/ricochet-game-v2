// rc-level/2 (docs/LEVEL_FORMAT.md): a level as elements -- walls, mirrors,
// doors, buttons, ... (rcg/elements.hpp) -- with its initial flags. It lowers
// to the rc-level/1 document the builder reads: the room's walls when the room
// is built, every element's surfaces in order, the four edge walls (where an
// `edges` element stands, else after the elements), the pre-transform circles
// last; ids in that order. migrate_level is the inverse for an rc-level/1
// document: the elements it is the lowering of, exactly.
#pragma once

#include "rcg/level.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rcg {

struct Lowered {
	nlohmann::json level;             // the rc-level/1 document (no flags: the surfaces' view)
	std::vector<std::string> origins; // per surface: where it came from ("level.elements[3] (m1)", "level.room", ...)
	nlohmann::json flags = nlohmann::json::object(); // the initial flags
	// Stage 152: per element with tags: its index, name and tags (its own and its kind's)
	struct Tagged {
		size_t element = 0;
		std::string name;
		std::vector<std::string> tags;
		std::string at; // its surfaces' origin prefix (Stage 169: an edge's too)
	};
	std::vector<Tagged> tagged;
	// Stage 153: per element whose kind bounds a region: its index, name and its kind's `shade` evaluated
	struct Shaded {
		size_t element = 0;
		std::string name;
		nlohmann::json shade;
		std::string at;
	};
	std::vector<Shaded> shaded;
};

// Stage 156 (GAME_SPEC 10.6): the hit effects an element's or an object's tags give each of its sides --
// flammable (fire passes between the arrow and it), conductive (the arrow and it swap charge), frosted
// (the arrow takes frost); burning and charged start it on fire or charged (and imply flammable,
// conductive). The flags: el:<name>:fire, el:<name>:charge, shot:fire, shot:charge, shot:frost.
nlohmann::json tag_hits(const std::string &name, const std::vector<std::string> &tags);
// ... the flags they start with ({el:<name>:fire: true} for burning, ...)
std::vector<std::string> tag_initial_flags(const std::string &name, const std::vector<std::string> &tags);
// ... whether a side's effect type may carry frost (pass, reflection, inversion)
bool frost_bearing(const std::string &effect_type);

// The lowering of an rc-level/2 document. Throws LevelError naming the element.
Lowered lower_level(const nlohmann::json &v2);

// Stage 179 (the user): the markers the flags give. An active flag is one two or more elements or detectors
// connect to (read -- presence, a resolver's key, a condition, through a flag expression too -- or write; the
// per-shot and channel flags aside). Each has a marker: the level's `flag_markers` entry ("none": none), else its
// own default, a shape and colour picked from its name (distinct ones while they last). An element or a detector
// whose one active flag is F shows F's marker unless its `sync_marker` is false; otherwise its own `marker`.
struct FlagMarkers {
	std::vector<std::string> active;                       // in first use order (the elements', then the detectors')
	std::map<std::string, std::optional<Marker>> of_flag;  // each active flag's
	std::vector<std::vector<std::string>> element_flags, detector_flags; // each one's active flags
	std::vector<std::optional<Marker>> element, detector;  // what each shows
};
FlagMarkers flag_markers(const nlohmann::json &v2, const Lowered &low);
// a marker as a document spells it ({"shape", "color"}: a palette colour by its name)
nlohmann::json marker_json(const Marker &m);

// The rc-level/2 document whose lowering is the rc-level/1 document `v1`, byte
// for byte (the room and the edge walls implicit where they are the generated
// ones; each run of surfaces the first template (by priority) that expands to it,
// else a `raw` element). Throws LevelError when there is none.
nlohmann::json migrate_level(const nlohmann::json &v1);

// Stage 149: an element on a carrier ("carrier", "from", "to") as one with its own geometry (start/end/via or
// circle), as the lowering places it; the carriers are the document's. Throws LevelError.
nlohmann::json off_carrier(const nlohmann::json &element, const nlohmann::json &doc);

// The walls of a rectangle [x, y, w, h] as rc-level/1 surfaces (no ids): top, right, bottom, left.
std::vector<nlohmann::json> rect_walls(const nlohmann::json &rect, const nlohmann::json &side, const nlohmann::json &label, bool solid);

} // namespace rcg
