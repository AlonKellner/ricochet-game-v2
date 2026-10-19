// The canonical JSON spelling of the repository's data (the levels, the
// level dumps, the sweep manifests): object keys sorted, one per line,
// indented by one space per level; an array of scalars on one line
// ("[1.0, 2.0]"), any other array one element per line; ints as ints; floats
// as their shortest round-trip decimal in Python's repr spelling (fixed
// notation with at least one fractional digit for exponents -4..15, else
// "1e+16" / "1.5e-05"); strings as JSON with non-ASCII kept. Identical to
// docs/research/levels/level_fmt.py, so Python and native tools agree byte for byte.
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace rcg {

// Python's repr of a float64 (shortest round-trip digits).
std::string float_repr(double x);

// The document in the canonical spelling, with a trailing newline.
std::string canonical(const nlohmann::json &doc);

} // namespace rcg
