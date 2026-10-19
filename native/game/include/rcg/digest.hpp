// Stage 116: the session replay golden's record encoding, natively
// (docs/research/session_digest.md; the port of SessionDigest.encode / sha /
// data): canonical JSON -- sorted keys, no whitespace, every float as "f" +
// the hex of its float64 bytes (little-endian), ints in decimal, strings with
// ", \, \n, \t, \r escaped by name and other control characters as \u00XX.
// A nlohmann::json number keeps its kind: a float is encoded as a float even
// when integral (Vector2 components, radii), an int as an int.
#pragma once

#include "rc/trace_types.hpp"
#include "rcg/level.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace rcg::digest {

std::string encode(const nlohmann::json &v);
// lowercase hex sha256 of the UTF-8 text (String.sha256_text)
std::string sha256_text(const std::string &text);
// sha(v): the sha256 of encode(v)
std::string sha(const nlohmann::json &v);

// a float / a Vector2 / a Color as the digest holds them
inline nlohmann::json f(double x) { return x; }
inline nlohmann::json vec(const rc::V2 &v) { return nlohmann::json::array({double(v.x), double(v.y)}); }

// SessionDigest.data: a spec as plain data (its fields; a side's effect and
// hits as authored).
nlohmann::json spec_data(const SurfaceSpec &s);

} // namespace rcg::digest
