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

// A template checked (check_template) and ready to expand: a preset of the surface
// element (Stage 207: {"extends": "surface", "defaults": {...}}) takes the surface
// element's parameters, with its defaults, and one composed surface.
json prepare(const json &tmpl, const std::string &path);

// The builtin kinds (kind -> prepared template), parsed and checked once.
const std::map<std::string, json> &builtin();

// A template that does not follow the template schema: LevelError naming `path`.
void check_template(const json &tmpl, const std::string &path);

// The element's parameters with the defaults filled in ("name" included).
// LevelError on an unknown key, a missing required parameter, a value outside
// its enum or of the wrong type.
json params_of(const json &tmpl, const json &element, const std::string &name, const std::string &path);

// The element's surfaces: rc-level/1 surfaces without ids, labelled.
std::vector<json> expand(const json &tmpl, const json &element, const std::string &name, const std::string &path);

// Stage 215: what an element's tags give its surfaces (Stage 156; level2's lowering, match's inverse): every side --
// a missing one a pass that carries them -- and every resolver row the tags' hits (tag_hits); frosted only on sides
// that bear frost (LevelError naming `path`). A pre-transform surface takes none.
void apply_tags(std::vector<json> &surfaces, const std::string &name, const std::vector<std::string> &tags, const std::string &path);

// The template's key `key` (a template key holding expressions: "shade") evaluated
// for the element; null when the template has none.
json evaluate(const json &tmpl, const json &element, const std::string &name, const std::string &key, const std::string &path);

// The element of kind `kind` whose expansion is surfaces[at, at + n) (n: the
// template's surface count), labels compared after the match (a differing one is
// the element's `label`). The element's name: what the match bound, else
// `fallback_name`; a bound name in `taken` fails the match. relabel false: a
// differing label fails it.
std::optional<json> match(const std::string &kind, const json &tmpl, const std::vector<json> &surfaces, size_t at,
		const std::string &fallback_name, const std::set<std::string> &taken, bool relabel = true);

} // namespace rcg::elements
