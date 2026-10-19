// Stage 116.6: the invariant sweeps' inputs (Workflow W2) without an engine:
// data/sweeps.json's groups (rc-sweeps/1) expanded into the sweep manifests
// (sweep-manifest/1, bench/sweeps/<level>__<seq>.json) that native/tests/
// test_sweeps.cpp replays. The port of SweepPlans (the plans, the combo
// levels), SweepRunner (the positions: grid, Godot's RNG fuzz, the points of
// interest) and InvariantChecker.surface_infos; a level after spawns is
// played on rcg::Session.
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace rcg::sweeps {

// [(file name, manifest)] in the groups' order
std::vector<std::pair<std::string, nlohmann::json>> manifests(const nlohmann::json &recipes, const std::string &levels_dir);

} // namespace rcg::sweeps
