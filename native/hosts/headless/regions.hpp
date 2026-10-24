// rc_tool regions (regions.cpp): every plannable side's region, timed and digested.
#pragma once

#include <string>
#include <vector>

namespace regions {

// rc_tool regions (<level json> | --all <levels dir>) [--players N] [--threads K] [--reps R] [--pairs] [--allocs]
//                 [--only <substring>] [--json out] [--md out]; exit 2 on usage
int run(std::vector<std::string> args);

} // namespace regions
