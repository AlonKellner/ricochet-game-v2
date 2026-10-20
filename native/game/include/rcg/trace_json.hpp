// Stage 119: the solver's inputs and outputs as JSON -- the scene defs, a
// plan, flags (the schema native/tests/support/json_defs.hpp reads), and a
// traced pair (every field the game reads of it). The GUT suite's solver
// scenarios are recorded this way before the GDScript game is deleted
// (bench/gut_traces, the BeamTracer capture) and replayed natively bit for
// bit (rcg_tests "gut traces"). Floats are JSON numbers (nlohmann writes the
// shortest round-trip spelling: they read back to the same bits).
#pragma once

#include "rc/scene.hpp"
#include "rc/solver.hpp"
#include "rc/tracer.hpp"

#include <nlohmann/json.hpp>

#include <vector>

namespace rcg::trace_json {

nlohmann::ordered_json defs(const std::vector<rc::SurfaceDef> &defs);
nlohmann::ordered_json plan(const std::vector<rc::PlanItem> &plan);
nlohmann::ordered_json flags(const rc::Flags &flags);
nlohmann::json path(const rc::TracePath &p);
// a traced pair (none: null): both paths, the final flags, cursor_point_agrees, shared
nlohmann::json pair(const std::optional<rc::TracePair> &p);

} // namespace rcg::trace_json
