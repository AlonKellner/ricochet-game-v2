// rc_tool: the headless host (Stage 115). The data pipelines without an engine.
//
//   rc_tool fmt [--check] <file or dir>...    the canonical spelling (rcg/canon.hpp) of JSON data;
//                                             --check: list the files not in it, exit 1 if any
//   rc_tool dumps <levels dir> <out dir>       every level's scene defs (bench/scenes/<dir>__<name>.surfaces.json)
//   rc_tool replay <sessions dir> <levels dir> <out dir> [--only <substring>]
//                                             the session scripts played natively (rcg::Session): one
//                                             record per step, as the golden (bench/sessions) holds them;
//                                             kinematic sessions are skipped (no native controller yet)
//
// Exit status: 0 done, 1 a check failed or a file is invalid, 2 usage.
#include "rcg/build.hpp"
#include "rcg/canon.hpp"
#include "rcg/level.hpp"
#include "rcg/digest.hpp"
#include "rcg/replay.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
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
				 "       rc_tool replay <sessions dir> <levels dir> <out dir> [--only <substring>]\n";
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

int replay(std::vector<std::string> args) {
	std::string only;
	if (args.size() == 5 && args[3] == "--only") {
		only = args[4];
		args.resize(3);
	}
	if (args.size() != 3) {
		return usage();
	}
	const fs::path in = args[0], levels = args[1], out = args[2];
	fs::create_directories(out);
	int n = 0, skipped = 0;
	for (const fs::path &p : json_files({in.string()})) {
		const std::string name = p.stem().string();
		if (!only.empty() && name.find(only) == std::string::npos) {
			continue;
		}
		const json script = json::parse(read(p));
		if (script.value("kind", "teleport") != "teleport") {
			skipped++;
			continue;
		}
		try {
			std::string text;
			for (const std::string &l : rcg::replay_session(script, levels.string())) {
				text += l + "\n";
			}
			if (!write(out / (name + ".jsonl"), text)) {
				return 1;
			}
		} catch (const std::exception &e) {
			std::cerr << name << ": " << e.what() << "\n";
			return 1;
		}
		n++;
	}
	std::cout << n << " sessions replayed, " << skipped << " kinematic skipped\n";
	return 0;
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
	if (cmd == "replay") {
		return replay(args);
	}
	return usage();
}
