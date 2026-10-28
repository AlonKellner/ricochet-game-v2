// Stage 118: rc_tool replay -- a session script played through the C API
// alone (rc_api.h; nothing of rcg), as any engine's host plays the game:
// semantic events, the cursor, frames of a fixed time, the frame read back.
// The records it writes are the golden's (bench/sessions).
#include "replay_capi.hpp"

#include "rc_api.h"

#include <cstring>
#include <memory>
#include <stdexcept>

namespace capi_replay {
namespace {

using json = nlohmann::json;

constexpr double FRAME = 1.0 / 60.0;

void check(rc_status s, const char *what) {
	if (s != RC_OK) {
		throw std::runtime_error(std::string(what) + ": " + rc_last_error());
	}
}

struct LevelHandle {
	rc_level *p = nullptr;
	~LevelHandle() { rc_level_release(p); }
};
struct GameHandle {
	rc_game *p = nullptr;
	~GameHandle() { rc_game_destroy(p); }
};

rc_frame frame_of(rc_game *g) {
	rc_frame f{};
	f.size = sizeof f;
	check(rc_game_frame(g, &f), "rc_game_frame");
	return f;
}

json vec(float x, float y) { return json::array({double(x), double(y)}); }

int32_t event_of(const std::string &action) {
	if (action == "undo") return RC_EVENT_UNDO;
	if (action == "reset") return RC_EVENT_RESET;
	if (action == "clear_plan") return RC_EVENT_CLEAR_PLAN;
	if (action == "cycle_display") return RC_EVENT_CYCLE_DISPLAY;
	if (action == "toggle_gravity") return RC_EVENT_TOGGLE_GRAVITY;
	if (action == "toggle_visibility") return RC_EVENT_TOGGLE_VISIBILITY;
	if (action == "any") return RC_EVENT_ANY_KEY;
	if (action == "fire") return RC_EVENT_FIRE;
	throw std::runtime_error("unknown action " + action);
}

uint32_t key_of(const std::string &action) {
	if (action == "move_left") return RC_KEY_LEFT;
	if (action == "move_right") return RC_KEY_RIGHT;
	if (action == "move_up") return RC_KEY_UP;
	if (action == "move_down") return RC_KEY_DOWN;
	throw std::runtime_error("unknown movement " + action);
}

} // namespace

std::vector<std::string> replay(const json &script, const std::string &level_text, bool draw_list) {
	LevelHandle level;
	check(rc_level_parse(level_text.data(), level_text.size(), &level.p), "rc_level_parse");
	rc_game_config cfg{sizeof(rc_game_config), -1, -1, 0, 0, 0.0f, 0.0f}; // (synchronous, unanimated: the replay is the golden's)
	if (script.contains("switches")) {
		const json &sw = script["switches"];
		if (sw.contains("visibility")) {
			cfg.visibility = sw["visibility"].get<bool>() ? 1 : 0;
		}
	}
	GameHandle game;
	check(rc_game_create(level.p, &cfg, &game.p), "rc_game_create");
	rc_game *g = game.p;
	std::vector<std::string> lines;
	auto record = [&](const json &extra) {
		const std::string text = extra.dump();
		const char *out = nullptr;
		size_t n = 0;
		check(rc_game_record(g, text.data(), text.size(), draw_list ? RC_RECORD_DRAW_LIST : 0, &out, &n), "rc_game_record");
		lines.emplace_back(out, n);
	};
	auto settle = [&] { check(rc_game_advance(g, 0.0, 0, 0), "rc_game_advance"); };
	auto at = [](const json &a) { return std::make_pair(a[0].get<float>(), a[1].get<float>()); };
	// the flight under way: the arrow sampled before every frame; `any` after the speedup_at-th
	auto fly = [&](int speedup_at) {
		json stream = json::array();
		int frames = 0;
		for (rc_frame f = frame_of(g); f.flying; f = frame_of(g)) {
			stream.push_back(json::array({vec(f.arrow_x, f.arrow_y), vec(f.arrow_dx, f.arrow_dy)}));
			frames++;
			check(rc_game_advance(g, FRAME, 0, 0), "rc_game_advance");
			if (frames == speedup_at) {
				check(rc_game_event(g, RC_EVENT_ANY_KEY), "rc_game_event");
			}
		}
		const std::string text = stream.dump();
		char sha[65];
		check(rc_digest_sha(text.data(), text.size(), sha), "rc_digest_sha");
		return json{{"frames", frames}, {"stream", sha}, {"end", stream.empty() ? json(nullptr) : stream.back()[0]}};
	};
	settle();
	record({{"i", 0}, {"op", "start"}});
	const json &steps = script["steps"];
	for (size_t i = 0; i < steps.size(); i++) {
		const json &step = steps[i];
		const std::string op = step["op"].get<std::string>();
		json extra = {{"i", int(i + 1)}, {"op", op}};
		if (op == "teleport") {
			const auto [x, y] = at(step["at"]);
			check(rc_game_teleport(g, x, y), "rc_game_teleport");
		} else if (op == "scroll") { // Stage 151: the cycle
			check(rc_game_scroll(g, step["by"].get<float>()), "rc_game_scroll");
		} else if (op == "cursor") {
			const auto [x, y] = at(step["at"]);
			check(rc_game_set_cursor(g, x, y), "rc_game_set_cursor");
		} else if (op == "click") {
			check(rc_game_event(g, step["button"] == "right" ? RC_EVENT_PLAN_REMOVE : RC_EVENT_PLAN_ADD), "rc_game_event");
			if (frame_of(g).flying) { // Stage 177: a left click over nothing plannable fired: its flight
				extra["flight"] = fly(-1);
			}
		} else if (op == "key") {
			const std::string a = step["action"].get<std::string>();
			extra["action"] = a;
			check(rc_game_event(g, event_of(a)), "rc_game_event");
		} else if (op == "fire") {
			check(rc_game_event(g, RC_EVENT_FIRE), "rc_game_event");
			extra["flight"] = fly(step.value("speedup_at", -1));
		} else if (op == "hold") {
			// the keys held for `ticks` frames of one physics tick each; the up
			// key reported just pressed on the second (the engine's input timing)
			uint32_t held = 0;
			for (const json &a : step["actions"]) {
				held |= key_of(a.get<std::string>());
			}
			json traj = json::array();
			for (int k = 0; k < step["ticks"].get<int>(); k++) {
				check(rc_game_advance(g, FRAME, held, k == 1 ? (held & RC_KEY_UP) : 0), "rc_game_advance");
				const rc_frame f = frame_of(g);
				traj.push_back(vec(f.player_x, f.player_y));
			}
			extra["trajectory"] = traj;
		} else {
			throw std::runtime_error("unsupported op " + op);
		}
		settle();
		record(extra);
	}
	return lines;
}

} // namespace capi_replay
