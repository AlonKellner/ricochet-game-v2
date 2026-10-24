// rc-level/2's element kinds (docs/ELEMENTS.md): an element is a template --
// data, never code -- expanded into rc-level/1 surfaces. The builtin kinds are
// data/elements/<kind>.json, embedded at build time; a level's `templates` adds
// or overrides kinds. The template language is small on purpose:
//   "$p"                       the value of parameter p (any JSON)
//   "text ${p} text"           p's value (a string) inside a string
//   {"$switch": "p", "cases": {...}}   the case named by p's value's spelling
// Parameters have a default (else they are required), an optional enum and type;
// "name" is the element's name. Every surface of an element shares its geometry
// (start/end/via or circle, copied verbatim); "flip" swaps their left and right.
// match() is the inverse, for `rc_tool migrate`: the element a run of rc-level/1
// surfaces is the expansion of, if any.
#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace rcg::elements {

using json = nlohmann::json;

// The keys every element may carry besides its parameters.
const std::set<std::string> &common_keys();

// The builtin kinds (kind -> template), parsed and checked once.
const std::map<std::string, json> &builtin();

// A template that does not follow the template schema: LevelError naming `path`.
void check_template(const json &tmpl, const std::string &path);

// The element's parameters with the defaults filled in ("name" included).
// LevelError on an unknown key, a missing required parameter, a value outside
// its enum or of the wrong type.
json params_of(const json &tmpl, const json &element, const std::string &name, const std::string &path);

// The element's surfaces: rc-level/1 surfaces without ids, labelled.
std::vector<json> expand(const json &tmpl, const json &element, const std::string &name, const std::string &path);

// The element of kind `kind` whose expansion is surfaces[at, at + n) (n: the
// template's surface count), labels compared after the match (a differing one is
// the element's `label`). The element's name: what the match bound, else
// `fallback_name`; a bound name in `taken` fails the match.
std::optional<json> match(const std::string &kind, const json &tmpl, const std::vector<json> &surfaces, size_t at,
		const std::string &fallback_name, const std::set<std::string> &taken);

} // namespace rcg::elements
