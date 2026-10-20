// Stage 118: a session script played through the C API alone (replay_capi.cpp).
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace capi_replay {

// The records of a session on the level (its JSON text): the golden's lines;
// draw_list: each record holds the draw list itself, not its sha. Throws
// std::runtime_error (a failing call, an op it does not know).
std::vector<std::string> replay(const nlohmann::json &script, const std::string &level_text, bool draw_list);

} // namespace capi_replay
