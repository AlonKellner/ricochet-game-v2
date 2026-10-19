// Stage 116.5: a session script (data/sessions, rc-session/1) played on the
// native session: the records the session replay golden holds
// (bench/sessions/<name>.jsonl), encoded. rc_tool replay and rcg_tests use it.
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace rcg {

// The records of a session (throws std::runtime_error on an op it does not know).
std::vector<std::string> replay_session(const nlohmann::json &script, const std::string &levels_dir);

} // namespace rcg
