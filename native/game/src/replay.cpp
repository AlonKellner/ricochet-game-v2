#include "rcg/replay.hpp"

#include "rcg/digest.hpp"
#include "rcg/render.hpp"
#include "rcg/session.hpp"

#include <filesystem>
#include <stdexcept>

namespace rcg {
namespace {

using json = nlohmann::json;

V2 at(const json &a) { return V2::of(a[0].get<double>(), a[1].get<double>()); }

} // namespace

std::vector<std::string> replay_session(const json &script, const std::string &levels_dir, DrawRecord draw, bool async) {
	const std::filesystem::path levels = levels_dir;
	SessionConfig cfg;
	if (script.contains("switches")) {
		const json &sw = script["switches"];
		if (sw.contains("visibility")) {
			cfg.visibility = sw["visibility"].get<bool>();
		}
	}
	cfg.async = async;
	Session s(load_level((levels / (script["level"].get<std::string>() + ".json")).string()), cfg);
	std::vector<std::string> lines;
	auto record = [&](json extra) {
		if (async) {
			s.drain();
		}
		if (draw != DrawRecord::None) {
			const json d = render::list_json(s.draw());
			extra["draw"] = draw == DrawRecord::Full ? d : json(digest::sha(d));
		}
		lines.push_back(digest::encode(s.record(extra)));
	};
	s.settle();
	record({{"i", 0}, {"op", "start"}});
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
		} else if (op == "hold") {
			std::vector<std::string> actions;
			for (const json &a : step["actions"]) {
				actions.push_back(a.get<std::string>());
			}
			json traj = json::array();
			for (const V2 &p : s.hold(actions, step["ticks"].get<int>())) {
				traj.push_back(digest::vec(p));
			}
			extra["trajectory"] = traj;
		} else {
			throw std::runtime_error("unsupported op " + op);
		}
		s.settle();
		record(extra);
	}
	return lines;
}


} // namespace rcg
