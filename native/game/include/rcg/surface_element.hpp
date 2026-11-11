// Stage 207: the surface element (GAME_SPEC 9.13, docs/ELEMENTS.md). One
// primitive composes a surface from orthogonal parameters:
//   face, back   what the arrow does on the left and the right side: stop | pass | mirror
//   body         what the player and objects do: pass | block | platform
//   plannable    the sides' interactivity (null: a side that reflects or has hits)
// and optional features, each on when its key parameter is set:
//   flag/opens   a flag written on a hit (op, value, deferred): a button
//   target       registers target:<id>
//   grapple      pulls the player to the hit
//   spawn        places a surface at the hit (channel, orientation, ...)
//   channels     re-arms spawn channels
//   breaks       gone for the rest of the shot once hit (shot:<name>:broken)
//   ghost        toggles the arrow's ghost mode
//   open         gone while a flag expression holds: a door
// The builtin kinds wall ... ghost_gate are presets of it: data/elements/<kind>.json
// as {"extends": "surface", "defaults": {...}, "label", ...}.
#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace rcg::surface_element {

using json = nlohmann::json;

// A feature: its name (the editor's), the parameter that turns it on and the ones it reads.
struct Feature {
	std::string name;
	std::string key;
	std::vector<std::string> params;
};
const std::vector<Feature> &features();

// The parameter specs, in a template's `params` form (default, enum, type, doc).
const json &params();

// The parameters a feature, or the sides and the body, read; "" for face, back, body, plannable.
std::string feature_of(const std::string &param);

// The surface the parameters `env` (params_of's, "name" included) compose: an
// rc-level/1 surface without geometry, ids or label.
json compose(const json &env, const std::string &path);

// The parameters a surface composes from, if it is a surface element's ("name"
// bound when a feature names it); none otherwise.
std::optional<json> decompose(const json &surface);

} // namespace rcg::surface_element
