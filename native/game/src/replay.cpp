#include "rcg/replay.hpp"

#include "rcg/digest.hpp"
#include "rcg/session.hpp"

#include <filesystem>
#include <stdexcept>

namespace rcg {
namespace {

using json = nlohmann::json;

V2 at(const json &a) { return V2::of(a[0].get<double>(), a[1].get<double>()); }

} // namespace

std::vector<std::string> replay_session(const json &script, const std::string &levels_dir) {
	const std::filesystem::path levels = levels_dir;
	SessionConfig cfg;
	if (script.contains("switches")) {
		const json &sw = script["switches"];
		if (sw.contains("infinity")) {
			cfg.infinity = sw["infinity"].get<bool>();
		}
		if (sw.contains("visibility")) {
			cfg.visibility = sw["visibility"].get<bool>();
		}
	}
	Session s(load_level((levels / (script["level"].get<std::string>() + ".json")).string()), cfg);
	std::vector<std::string> lines;
	s.settle();
	lines.push_back(digest::encode(s.record({{"i", 0}, {"op", "start"}})));
	const json &steps = script["steps"];
	for (size_t i = 0; i < steps.size(); i++) {
		const json &step = steps[i];
		const std::string op = step["op"].get<std::string>();
		json extra = {{"i", int(i + 1)}, {"op", op}};
		if (op == "teleport") {
			s.teleport(at(step["at"]));
		} else if (op == "cursor") {
			s.set_cursor(at(step["at"]));
		} else if (op == "click") {
			s.click(step["button"] == "right");
		} else if (op == "key") {
			const std::string a = step["action"].get<std::string>();
			extra["action"] = a;
			const std::optional<Action> act = action_of(a);
			if (!act) {
				throw std::runtime_error("unknown action " + a);
			}
			s.key(*act);
		} else if (op == "fire") {
			const Flight f = s.fire(step.value("speedup_at", -1), 1.0 / 60.0);
			json stream = json::array();
			for (const auto &[p, d] : f.stream) {
				stream.push_back(json::array({digest::vec(p), digest::vec(d)}));
			}
			extra["flight"] = {{"frames", f.frames}, {"stream", digest::sha(stream)},
				{"end", f.stream.empty() ? json(nullptr) : digest::vec(f.stream.back().first)}};
		} else {
			throw std::runtime_error("unsupported op " + op);
		}
		s.settle();
		lines.push_back(digest::encode(s.record(extra)));
	}
	return lines;
}


} // namespace rcg
