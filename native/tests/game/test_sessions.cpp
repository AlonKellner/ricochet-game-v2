// Stage 116.5: the native session (rcg::Session) against the session replay
// golden: every teleport session (data/sessions) replays byte for byte to its
// recording (bench/sessions, made by the GDScript game). RCG_SESSIONS_PART=k/n
// plays the k-th of every n sessions (the ctest entries rcg_sessions_{k}).
#include "rcg/replay.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("sessions: every teleport session replays natively, byte for byte") {
	const fs::path root = RC_REPO_ROOT;
	int part = 0, parts = 1;
	if (const char *p = std::getenv("RCG_SESSIONS_PART")) {
		std::sscanf(p, "%d/%d", &part, &parts);
	}
	std::vector<fs::path> scripts;
	for (const auto &e : fs::directory_iterator(root / "data" / "sessions")) {
		if (e.path().extension() == ".json") {
			scripts.push_back(e.path());
		}
	}
	std::sort(scripts.begin(), scripts.end());
	int n = 0;
	for (size_t k = 0; k < scripts.size(); k++) {
		if (int(k % size_t(parts)) != part) {
			continue;
		}
		std::ifstream in(scripts[k]);
		const nlohmann::json script = nlohmann::json::parse(in);
		if (script.value("kind", "teleport") != "teleport") {
			continue;
		}
		const std::string name = scripts[k].stem().string();
		CAPTURE(name);
		std::ifstream gin(root / "bench" / "sessions" / (name + ".jsonl"));
		REQUIRE(gin.good());
		std::vector<std::string> golden;
		for (std::string line; std::getline(gin, line);) {
			if (!line.empty()) {
				golden.push_back(line);
			}
		}
		const std::vector<std::string> got = rcg::replay_session(script, (root / "data" / "levels").string());
		REQUIRE(got.size() == golden.size());
		for (size_t i = 0; i < got.size(); i++) {
			CAPTURE(i);
			if (got[i] != golden[i]) {
				CHECK_MESSAGE(got[i] == golden[i], "the first differing record (docs/research/session_judge.py for the fields)");
				break;
			}
		}
		n++;
	}
	CHECK(n > 0);
}
