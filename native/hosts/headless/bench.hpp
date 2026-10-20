// rc_tool bench (bench.cpp): what a frame costs the game, per level and phase.
#pragma once

#include <string>
#include <vector>

namespace bench {

// rc_tool bench (<level json> | --all <levels dir>) [--frames N] [--click-frames N] [--budget ms] [--strict]
//               [--json out] [--md out]; exit 1 when a check fails, 2 on usage
int run(std::vector<std::string> args);

} // namespace bench
