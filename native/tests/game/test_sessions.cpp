// Stage 116.5: the native session (rcg::Session) against the session replay
// golden: every session (data/sessions; the kinematic ones since Stage 117,
// the native player controller) replays byte for byte to its recording (bench/sessions, made by the GDScript game). RCG_SESSIONS_PART=k/n
// plays the k-th of every n sessions (the ctest entries rcg_sessions_{k} and,
// asynchronous, rcg_sessions_async_{k}). RC_SESSIONS_SAMPLE=n: the asynchronous
// replay plays every n-th session (the commit gate, docs/WORKFLOW.md).
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

namespace {

void replay_all(bool async) {
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
	if (const char *s = std::getenv("RC_SESSIONS_SAMPLE"); s && async) {
		std::vector<fs::path> kept;
		for (size_t k = 0; k < scripts.size(); k += size_t(std::max(1, std::atoi(s)))) {
			kept.push_back(scripts[k]);
		}
		scripts = kept;
	}
	int n = 0;
	for (size_t k = 0; k < scripts.size(); k++) {
		if (int(k % size_t(parts)) != part) {
			continue;
		}
		std::ifstream in(scripts[k]);
		const nlohmann::json script = nlohmann::json::parse(in);
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
		const std::vector<std::string> got = rcg::replay_session(script, (root / "data" / "levels").string(), rcg::DrawRecord::Sha, async);
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
	CHECK((n > 0 || std::getenv("RC_SESSIONS_SAMPLE")));
}

} // namespace

TEST_CASE("sessions: every session replays natively, byte for byte") { replay_all(false); }

// Stage 121: the asynchronous session (the region and the preview's climbs in
// the background), drained before each record, is the synchronous session
TEST_CASE("sessions: the asynchronous session, drained, replays byte for byte") { replay_all(true); }
