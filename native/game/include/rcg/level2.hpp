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

#include <string>
#include <vector>

namespace rcg {

struct Lowered {
	nlohmann::json level;             // the rc-level/1 document (no flags: the surfaces' view)
	std::vector<std::string> origins; // per surface: where it came from ("level.elements[3] (m1)", "level.room", ...)
	nlohmann::json flags = nlohmann::json::object(); // the initial flags
};

// The lowering of an rc-level/2 document. Throws LevelError naming the element.
Lowered lower_level(const nlohmann::json &v2);

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
