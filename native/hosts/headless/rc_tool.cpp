// rc_tool: the headless host (Stage 115). The data pipelines without an engine.
//
//   rc_tool fmt [--check] <file or dir>...    the canonical spelling (rcg/canon.hpp) of JSON data;
//                                             --check: list the files not in it, exit 1 if any
//   rc_tool dumps <levels dir> <out dir>       every level's scene defs (bench/scenes/<dir>__<name>.surfaces.json)
//   rc_tool manifests <sweeps recipes> <levels dir> <out dir>
//                                             the sweep manifests (bench/sweeps) from data/sweeps.json
//   rc_tool replay <sessions dir> <levels dir> (<out dir> | --check <golden dir>)
//                  [--only <substring>] [--shard i/n] [--full]
//                                             the session scripts played through the C API (rc_api.h, as any
//                                             engine's host): one record per step, as the golden
//                                             (bench/sessions) holds them; --check: compare with the golden
//                                             instead of writing; --full: the records hold the draw list
//                                             itself, not its sha
//   rc_tool bench <level json> [--frames N] [--budget ms]
//                                             the game's per-frame cost on a level (phases: still,
//                                             cursor and player orbits, a plan, a hover); fails when
//                                             a still frame recomputes or a mean frame is over budget
//
// Exit status: 0 done, 1 a check failed or a file is invalid, 2 usage.
#include "rcg/build.hpp"
#include "rcg/canon.hpp"
#include "rcg/level.hpp"
#include "rcg/digest.hpp"
#include "replay_capi.hpp"
#include "rcg/session.hpp"
#include "rcg/sweeps.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

int usage() {
	std::cerr << "usage: rc_tool fmt [--check] <file or dir>...\n"
				 "       rc_tool dumps <levels dir> <out dir>\n"
				 "       rc_tool manifests <sweeps recipes> <levels dir> <out dir>\n"
				 "       rc_tool replay <sessions dir> <levels dir> (<out dir> | --check <golden dir>) [--only <substring>] [--shard i/n] [--full]\n"
				 "       rc_tool bench <level json> [--frames N] [--budget ms]\n";
	return 2;
}

std::string read(const fs::path &p) {
	std::ifstream in(p, std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

bool write(const fs::path &p, const std::string &text) {
	fs::create_directories(p.parent_path());
	std::ofstream out(p, std::ios::binary);
	out << text;
	return bool(out);
}

// The .json files under the arguments (a file as given, a directory recursively), sorted.
std::vector<fs::path> json_files(const std::vector<std::string> &args) {
	std::vector<fs::path> out;
	for (const std::string &a : args) {
		if (fs::is_directory(a)) {
			for (const auto &e : fs::recursive_directory_iterator(a)) {
				if (e.is_regular_file() && e.path().extension() == ".json") {
					out.push_back(e.path());
				}
			}
		} else {
			out.push_back(a);
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

int fmt(std::vector<std::string> args) {
	bool check = false;
	if (!args.empty() && args.front() == "--check") {
		check = true;
		args.erase(args.begin());
	}
	if (args.empty()) {
		return usage();
	}
	int bad = 0, n = 0;
	for (const fs::path &p : json_files(args)) {
		const std::string text = read(p);
		json doc;
		try {
			doc = json::parse(text);
			if (doc.is_object() && doc.value("schema", "") == "rc-level/1") {
				rcg::build_level(rcg::parse_level(text)); // a level must also be valid
			}
		} catch (const std::exception &e) {
			std::cerr << p.string() << ": " << e.what() << "\n";
			bad++;
			continue;
		}
		const std::string canon = rcg::canonical(doc);
		n++;
		if (canon == text) {
			continue;
		}
		if (check) {
			std::cerr << p.string() << ": not in the canonical spelling (rc_tool fmt)\n";
			bad++;
		} else if (!write(p, canon)) {
			std::cerr << p.string() << ": cannot write\n";
			bad++;
		}
	}
	std::cout << n << " files" << (check ? " checked" : " formatted") << ", " << bad << " problems\n";
	return bad ? 1 : 0;
}

int dumps(const std::vector<std::string> &args) {
	if (args.size() != 2) {
		return usage();
	}
	const fs::path levels = args[0], out = args[1];
	int n = 0;
	for (const fs::path &p : json_files({levels.string()})) {
		const std::string name = fs::relative(p, levels).replace_extension("").generic_string();
		std::string file = name;
		for (size_t at = file.find('/'); at != std::string::npos; at = file.find('/', at + 2)) {
			file.replace(at, 1, "__");
		}
		try {
			const json defs = rcg::describe_level(rcg::build_level(rcg::load_level(p.string())));
			const json doc = {{"scene", "res://scenes/" + name + ".tscn"}, {"surfaces", defs}};
			if (!write(out / (file + ".surfaces.json"), rcg::canonical(doc))) {
				std::cerr << file << ": cannot write\n";
				return 1;
			}
		} catch (const std::exception &e) {
			std::cerr << e.what() << "\n";
			return 1;
		}
		n++;
	}
	std::cout << n << " level dumps in " << out.string() << "\n";
	return 0;
}

int manifests(const std::vector<std::string> &args) {
	if (args.size() != 3) {
		return usage();
	}
	try {
		const auto ms = rcg::sweeps::manifests(json::parse(read(args[0])), args[1]);
		for (const auto &[name, m] : ms) {
			if (!write(fs::path(args[2]) / name, rcg::canonical(m))) {
				return 1;
			}
		}
		std::cout << ms.size() << " sweep manifests in " << args[2] << "\n";
	} catch (const std::exception &e) {
		std::cerr << e.what() << "\n";
		return 1;
	}
	return 0;
}

int replay(std::vector<std::string> args) {
	std::string only, check;
	bool full = false;
	int shard = 0, shards = 1;
	for (size_t i = 0; i < args.size();) {
		const bool value = i + 1 < args.size();
		if (args[i] == "--only" && value) {
			only = args[i + 1];
		} else if (args[i] == "--check" && value) {
			check = args[i + 1];
		} else if (args[i] == "--shard" && value && std::sscanf(args[i + 1].c_str(), "%d/%d", &shard, &shards) == 2 && shards > 0) {
		} else if (args[i] == "--full") {
			full = true;
			args.erase(args.begin() + long(i));
			continue;
		} else {
			i++;
			continue;
		}
		args.erase(args.begin() + long(i), args.begin() + long(i) + 2);
	}
	if (args.size() != (check.empty() ? 3u : 2u)) {
		return usage();
	}
	const fs::path in = args[0], levels = args[1];
	const fs::path out = check.empty() ? fs::path(args[2]) : fs::path();
	int n = 0, differ = 0, index = -1;
	for (const fs::path &p : json_files({in.string()})) {
		const std::string name = p.stem().string();
		if (!only.empty() && name.find(only) == std::string::npos) {
			continue;
		}
		if (++index % shards != shard) {
			continue;
		}
		const json script = json::parse(read(p));
		try {
			std::string text;
			const std::string level = read(levels / (script["level"].get<std::string>() + ".json"));
			for (const std::string &l : capi_replay::replay(script, level, full)) {
				text += l + "\n";
			}
			if (!check.empty()) {
				if (read(fs::path(check) / (name + ".jsonl")) != text) {
					std::cerr << name << ": differs from " << check << " (docs/research/session_judge.py tells where)\n";
					differ++;
				}
			} else if (!write(out / (name + ".jsonl"), text)) {
				return 1;
			}
		} catch (const std::exception &e) {
			std::cerr << name << ": " << e.what() << "\n";
			return 1;
		}
		n++;
	}
	std::cout << n << " sessions replayed through the C API" << (check.empty() ? "" : differ ? ", some differ" : ", all equal the golden") << "\n";
	return differ ? 1 : 0;
}


// rc_tool bench: the per-frame cost of the game on a level (Stage 119; the
// port of the GDScript frame bench). Phases of `frames` frames, each frame a
// settle and the draw list (what a host's frame costs the game): still (no
// input), the cursor orbiting the player, a two-entry plan still and with the
// orbit, the cursor hovering a surface, the player orbiting. A still phase
// may compute the preview and the region once (its first frame), never again;
// the cursor orbits compute no region after their first frame; the mean frame
// of every phase but the player's orbit (a region a frame, computed in the
// frame here) stays under the budget. Exit 1 when a phase fails.
int bench(std::vector<std::string> args) {
	int frames = 250;
	double budget_ms = 8.0;
	for (size_t i = 0; i + 1 < args.size();) {
		if (args[i] == "--frames") {
			frames = std::stoi(args[i + 1]);
		} else if (args[i] == "--budget") {
			budget_ms = std::stod(args[i + 1]);
		} else {
			i++;
			continue;
		}
		args.erase(args.begin() + long(i), args.begin() + long(i) + 2);
	}
	if (args.size() != 1) {
		return usage();
	}
	const rcg::Level level = rcg::load_level(args[0]);
	rcg::Session s(level);
	const rc::V2 home = s.player();
	const rc::V2 aim = home + rc::V2{300.0f, -100.0f};
	// the first two plannable sides: 6 px off the middle of a surface, on its left
	std::vector<rc::V2> plannable;
	for (const rcg::BuiltSurface &b : s.world().surfaces()) {
		if (b.label && b.left && b.left->interactive && !b.pre_transform) {
			const rc::V2 d = (b.seg.e - b.seg.s).normalized();
			plannable.push_back(b.seg.v + rc::V2{-d.y, d.x} * 6.0f);
		}
	}
	auto orbit = [&](const rc::V2 &c, double r, int k) {
		const double a = 6.283185307179586 * k / frames;
		return rc::V2::of(double(c.x) + r * std::cos(a), double(c.y) + r * std::sin(a));
	};
	struct Phase {
		const char *name;
		bool still;
		std::function<void(int)> setup, step;
	};
	auto reset = [&](int) {
		s.key(rcg::Action::Reset);
		s.teleport(home);
		s.set_cursor(aim);
	};
	auto plan_two = [&](int k) {
		reset(k);
		for (size_t i = 0; i < plannable.size() && i < 2; i++) {
			s.set_cursor(plannable[i]);
			s.settle();
			s.click(false);
		}
		s.set_cursor(aim);
	};
	const std::vector<Phase> phases = {
		{"static", true, reset, [](int) {}},
		{"cursor_orbit", false, reset, [&](int k) { s.set_cursor(orbit(home, 420.0, k)); }},
		{"plan_static", true, plan_two, [](int) {}},
		{"plan_orbit", false, plan_two, [&](int k) { s.set_cursor(orbit(home, 420.0, k)); }},
		{"hover_static", true, [&](int k) { reset(k); if (!plannable.empty()) s.set_cursor(plannable[0]); }, [](int) {}},
		{"player_orbit", false, reset, [&](int k) { s.teleport(orbit(home, 60.0, k)); }},
	};
	std::printf("%-14s %8s %8s %8s %9s %9s  (%s, %d frames, budget %.1f ms)\n", "phase", "mean ms", "p95 ms", "max ms", "previews",
		"regions", args[0].c_str(), frames, budget_ms);
	bool failed = false;
	for (const Phase &ph : phases) {
		ph.setup(0);
		const int p0 = s.preview_version(), v0 = s.visibility_version();
		std::vector<double> ms;
		for (int k = 0; k < frames; k++) {
			const auto t0 = std::chrono::steady_clock::now();
			ph.step(k);
			s.settle();
			const std::vector<rcg::render::Node> list = s.draw();
			ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
		}
		const int previews = s.preview_version() - p0, regions = s.visibility_version() - v0;
		std::vector<double> sorted = ms;
		std::sort(sorted.begin(), sorted.end());
		double mean = 0.0;
		for (double m : ms) {
			mean += m / double(ms.size());
		}
		bool bad = false;
		bad = bad || (ph.still && (previews > 1 || regions > 1));
		bad = bad || ((ph.still || std::string(ph.name) == "cursor_orbit") && regions > 1);
		bad = bad || (std::string(ph.name) != "player_orbit" && mean > budget_ms);
		failed = failed || bad;
		std::printf("%-14s %8.3f %8.3f %8.3f %9d %9d %s\n", ph.name, mean, sorted[size_t(double(sorted.size() - 1) * 0.95)], sorted.back(),
			previews, regions, bad ? "FAIL" : "");
	}
	std::printf("rc_tool bench: %s\n", failed ? "FAIL (a still phase recomputed, or a mean frame exceeded the budget)" : "ok");
	return failed ? 1 : 0;
}

} // namespace

int main(int argc, char **argv) {
	if (argc < 2) {
		return usage();
	}
	const std::string cmd = argv[1];
	const std::vector<std::string> args(argv + 2, argv + argc);
	if (cmd == "fmt") {
		return fmt(args);
	}
	if (cmd == "dumps") {
		return dumps(args);
	}
	if (cmd == "manifests") {
		return manifests(args);
	}
	if (cmd == "bench") {
		return bench(args);
	}
	if (cmd == "replay") {
		return replay(args);
	}
	return usage();
}
