// Stage 118: the C API (rc_api.h) over the native game (rcg::Session).
#include "rc_api.h"

#include "rc/error.hpp"
#include "rcg/digest.hpp"
#include "rcg/level.hpp"
#include "rcg/render.hpp"
#include "rcg/session.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct rc_level {
	std::shared_ptr<const rcg::Level> level;
};

struct rc_game {
	std::shared_ptr<const rcg::Level> level;
	std::unique_ptr<rcg::Session> session;
	// the borrowed views (valid until the next non-const call)
	std::vector<rc_draw_node> nodes;
	std::vector<rc_draw_cmd> cmds;
	std::vector<float> points, colors;
	std::vector<int32_t> indices;
	std::string text;
	std::vector<uint64_t> node_ids, node_versions;
	std::vector<uint8_t> node_unchanged;
	std::map<uint64_t, uint64_t> sent;        // rc_game_draw_changes: id -> the version last sent
	std::map<uint64_t, std::string> key_text; // id -> its key's JSON
	std::string hud;
	std::string record;
};

static_assert(RC_DRAW_LINE == int(rcg::render::Kind::Line) && RC_DRAW_ARC == int(rcg::render::Kind::Arc) &&
				  RC_DRAW_POLYGON == int(rcg::render::Kind::Polygon) && RC_DRAW_COLORED_POLYGON == int(rcg::render::Kind::ColoredPolygon) &&
				  RC_DRAW_CIRCLE == int(rcg::render::Kind::Circle) && RC_DRAW_RECT == int(rcg::render::Kind::Rect) &&
				  RC_DRAW_POLYLINE == int(rcg::render::Kind::Polyline) && RC_DRAW_MULTILINE == int(rcg::render::Kind::Multiline) &&
				  RC_DRAW_DASHED_LINE == int(rcg::render::Kind::DashedLine) && RC_DRAW_TEXT == int(rcg::render::Kind::Text) &&
				  RC_DRAW_TRIANGLES == int(rcg::render::Kind::Triangles) && RC_DRAW_POLYGON2D == int(rcg::render::Kind::Polygon2D) &&
				  RC_DRAW_LABEL == int(rcg::render::Kind::Label),
	"rc_api.h's draw kinds are rcg::render::Kind's");

namespace {

thread_local std::string g_last_error;

std::mutex g_log_mutex;
rc_log_fn g_log_fn = nullptr;
void *g_log_user = nullptr;

void log(int32_t level, const char *message) {
	rc_log_fn fn;
	void *user;
	{
		std::lock_guard<std::mutex> lock(g_log_mutex);
		fn = g_log_fn;
		user = g_log_user;
	}
	if (fn != nullptr) {
		fn(user, level, message);
	} else {
		std::fputs(message, stderr);
		std::fputc('\n', stderr);
	}
}

rc_status fail(rc_status status, const std::string &message) {
	g_last_error = message;
	if (status == RC_E_INTERNAL) {
		log(RC_LOG_ERROR, message.c_str());
	}
	return status;
}

// Every entry point: its body, its exceptions turned into statuses.
template <class F>
rc_status guarded(F &&body) {
	g_last_error.clear();
	try {
		return body();
	} catch (const rcg::LevelError &e) {
		return fail(RC_E_LEVEL, e.what());
	} catch (const nlohmann::json::exception &e) {
		return fail(RC_E_ARGUMENT, e.what());
	} catch (const std::exception &e) {
		return fail(RC_E_INTERNAL, std::string("internal: ") + e.what());
	} catch (...) {
		return fail(RC_E_INTERNAL, "internal: unknown exception");
	}
}

// A size-prefixed struct the caller passes: at least its size field.
template <class T>
bool sized(const T *p) {
	return p != nullptr && p->size >= sizeof(uint32_t);
}

// Copies the library's struct into the caller's, as far as the caller's size reaches.
template <class T>
void fill(T *out, const T &full) {
	const uint32_t size = out->size;
	std::memcpy(static_cast<void *>(out), &full, std::min<size_t>(size, sizeof(T)));
	out->size = size;
}

uint32_t u32(size_t n) { return uint32_t(n); }

} // namespace

extern "C" {

uint32_t rc_api_version(void) { return (uint32_t(RC_API_VERSION_MAJOR) << 16) | uint32_t(RC_API_VERSION_MINOR); }

const char *rc_last_error(void) { return g_last_error.c_str(); }

void rc_set_log(rc_log_fn fn, void *user) {
	{
		std::lock_guard<std::mutex> lock(g_log_mutex);
		g_log_fn = fn;
		g_log_user = user;
	}
	// the core's errors (a non-aborting RC_ASSERT, report_error) go to the log
	if (fn != nullptr) {
		rc::set_error_sink([](const char *message) { log(RC_LOG_ERROR, message); });
	} else {
		rc::set_error_sink(nullptr);
	}
}

rc_status rc_level_parse(const char *json, size_t length, rc_level **out) {
	return guarded([&]() -> rc_status {
		if (json == nullptr || out == nullptr) {
			return fail(RC_E_ARGUMENT, "rc_level_parse: null argument");
		}
		*out = nullptr;
		auto level = std::make_shared<const rcg::Level>(rcg::parse_level(std::string_view(json, length)));
		rcg::build_level(*level); // a level whose specs do not validate is refused here
		*out = new rc_level{std::move(level)};
		return RC_OK;
	});
}

void rc_level_release(rc_level *level) { delete level; }

rc_status rc_game_create(const rc_level *level, const rc_game_config *config, rc_game **out) {
	return guarded([&]() -> rc_status {
		if (level == nullptr || out == nullptr || (config != nullptr && !sized(config))) {
			return fail(RC_E_ARGUMENT, "rc_game_create: null argument or unsized config");
		}
		*out = nullptr;
		rc_game_config cfg{sizeof(rc_game_config), -1, -1, 0};
		if (config != nullptr) {
			std::memcpy(&cfg, config, std::min<size_t>(config->size, sizeof(rc_game_config)));
		}
		rcg::SessionConfig sc;
		if (cfg.infinity >= 0) {
			sc.infinity = cfg.infinity != 0;
		}
		if (cfg.visibility >= 0) {
			sc.visibility = cfg.visibility != 0;
		}
		sc.async = cfg.async != 0;
		auto game = std::make_unique<rc_game>();
		game->level = level->level;
		game->session = std::make_unique<rcg::Session>(*game->level, sc);
		game->session->settle();
		*out = game.release();
		return RC_OK;
	});
}

void rc_game_destroy(rc_game *game) { delete game; }

rc_status rc_game_event(rc_game *game, int32_t event) {
	return guarded([&]() -> rc_status {
		if (game == nullptr || event < RC_EVENT_PLAN_ADD || event > RC_EVENT_ANY_KEY) {
			return fail(RC_E_ARGUMENT, "rc_game_event: null game or unknown event");
		}
		rcg::Session &s = *game->session;
		const bool click = event == RC_EVENT_PLAN_ADD || event == RC_EVENT_PLAN_REMOVE;
		if (s.flying()) {
			if (!click) {
				s.speed_up(); // a key in flight
			}
			return RC_OK;
		}
		switch (event) {
			case RC_EVENT_PLAN_ADD: s.click(false); break;
			case RC_EVENT_PLAN_REMOVE: s.click(true); break;
			case RC_EVENT_FIRE: s.start_fire(); break;
			case RC_EVENT_UNDO: s.key(rcg::Action::Undo); break;
			case RC_EVENT_RESET: s.key(rcg::Action::Reset); break;
			case RC_EVENT_CLEAR_PLAN: s.key(rcg::Action::ClearPlan); break;
			case RC_EVENT_CYCLE_DISPLAY: s.key(rcg::Action::CycleDisplay); break;
			case RC_EVENT_TOGGLE_GRAVITY: s.key(rcg::Action::ToggleGravity); break;
			case RC_EVENT_TOGGLE_VISIBILITY: s.key(rcg::Action::ToggleVisibility); break;
			default: break; // any key: nothing outside a flight
		}
		return RC_OK;
	});
}

rc_status rc_game_set_cursor(rc_game *game, float x, float y) {
	return guarded([&]() -> rc_status {
		if (game == nullptr) {
			return fail(RC_E_ARGUMENT, "rc_game_set_cursor: null game");
		}
		game->session->set_cursor(rc::V2{x, y});
		return RC_OK;
	});
}

rc_status rc_game_advance(rc_game *game, double dt, uint32_t held, uint32_t pressed) {
	return guarded([&]() -> rc_status {
		if (game == nullptr || !(dt >= 0.0) || !std::isfinite(dt)) {
			return fail(RC_E_ARGUMENT, "rc_game_advance: null game or a negative / non-finite dt");
		}
		rcg::kinematics::Input in;
		in.axis_x = float((held & RC_KEY_RIGHT) != 0) - float((held & RC_KEY_LEFT) != 0);
		in.axis_y = float((held & RC_KEY_DOWN) != 0) - float((held & RC_KEY_UP) != 0);
		in.jump = (pressed & RC_KEY_UP) != 0;
		game->session->advance(dt, in);
		return RC_OK;
	});
}

rc_status rc_game_teleport(rc_game *game, float x, float y) {
	return guarded([&]() -> rc_status {
		if (game == nullptr) {
			return fail(RC_E_ARGUMENT, "rc_game_teleport: null game");
		}
		game->session->teleport(rc::V2{x, y});
		return RC_OK;
	});
}

rc_status rc_game_frame(const rc_game *game, rc_frame *out) {
	return guarded([&]() -> rc_status {
		if (game == nullptr || !sized(out)) {
			return fail(RC_E_ARGUMENT, "rc_game_frame: null game or unsized frame");
		}
		const rcg::Session &s = *game->session;
		const rcg::Level &l = s.level();
		rc_frame f{};
		f.size = sizeof(rc_frame);
		f.player_x = s.player().x;
		f.player_y = s.player().y;
		f.facing = s.facing();
		f.flying = s.flying() ? 1 : 0;
		f.arrow_x = s.arrow_position().x;
		f.arrow_y = s.arrow_position().y;
		f.arrow_dx = s.arrow_direction().x;
		f.arrow_dy = s.arrow_direction().y;
		f.camera = l.camera ? 1 : 0;
		f.camera_x = s.camera_target().x;
		f.camera_y = s.camera_target().y;
		f.camera_smoothing = l.camera && l.camera->smoothing ? 1 : 0;
		f.camera_smoothing_speed = l.camera ? float(l.camera->smoothing_speed) : 0.0f;
		f.bounds_x = float(l.bounds.x);
		f.bounds_y = float(l.bounds.y);
		f.bounds_w = float(l.bounds.w);
		f.bounds_h = float(l.bounds.h);
		f.display_mode = l.features.preview ? s.display_mode() : -1;
		f.gravity_x = s.gravity().x;
		f.gravity_y = s.gravity().y;
		f.region_pending = s.region_pending() ? 1 : 0;
		f.preview_exact = s.preview_exact() ? 1 : 0;
		fill(out, f);
		return RC_OK;
	});
}

namespace {

// The draw list into the game's pools; changes_only: unchanged nodes (since the
// last such call) without their commands.
rc_status draw_list(rc_game *game, rc_draw_list *out, bool changes_only) {
	if (game == nullptr || !sized(out)) {
		return fail(RC_E_ARGUMENT, "rc_game_draw_list: null game or unsized list");
	}
	std::vector<rcg::render::FrameNode> nodes = game->session->draw_frame();
	std::stable_sort(nodes.begin(), nodes.end(), [](const auto &a, const auto &b) { return a.z < b.z; });
	game->nodes.clear();
	game->cmds.clear();
	game->points.clear();
	game->colors.clear();
	game->indices.clear();
	game->text.clear();
	game->node_ids.clear();
	game->node_versions.clear();
	game->node_unchanged.clear();
	std::map<uint64_t, uint64_t> sent;
	for (const rcg::render::FrameNode &n : nodes) {
		const uint64_t id = n.key.id();
		auto kt = game->key_text.find(id);
		if (kt == game->key_text.end()) {
			kt = game->key_text.emplace(id, n.key.json().dump()).first;
		}
		const std::string &key = kt->second;
		const bool unchanged = changes_only && game->sent.count(id) && game->sent[id] == n.version;
		sent[id] = n.version;
		rc_draw_node dn{};
		dn.key_first = u32(game->text.size());
		dn.key_length = u32(key.size());
		game->text += key;
		dn.z = n.z;
		dn.x = n.position.x;
		dn.y = n.position.y;
		std::copy(n.modulate.begin(), n.modulate.end(), dn.modulate);
		dn.cmd_first = u32(game->cmds.size());
		dn.cmd_count = unchanged ? 0 : u32(n.calls->size());
		for (size_t ci = 0; !unchanged && ci < n.calls->size(); ci++) {
			const rcg::render::Call &c = (*n.calls)[ci];
			rc_draw_cmd dc{};
			dc.kind = int32_t(c.kind);
			dc.point_first = u32(game->points.size() / 2);
			dc.point_count = u32(c.pts.size());
			for (const rc::V2 &p : c.pts) {
				game->points.push_back(p.x);
				game->points.push_back(p.y);
			}
			dc.color_first = u32(game->colors.size() / 4);
			dc.color_count = u32(c.colors.size());
			for (const rcg::render::Color &k : c.colors) {
				game->colors.insert(game->colors.end(), k.begin(), k.end());
			}
			dc.index_first = u32(game->indices.size());
			dc.index_count = u32(c.indices.size());
			game->indices.insert(game->indices.end(), c.indices.begin(), c.indices.end());
			dc.text_first = u32(game->text.size());
			dc.text_length = u32(c.text.size());
			game->text += c.text;
			dc.arc_point_count = c.point_count;
			dc.alignment = c.alignment;
			dc.font_size = c.font_size;
			dc.radius = c.radius;
			dc.start_angle = c.start;
			dc.end_angle = c.end;
			dc.width = c.width;
			dc.dash = c.dash;
			dc.rotation = c.rotation;
			game->cmds.push_back(dc);
		}
		game->nodes.push_back(dn);
		game->node_ids.push_back(id);
		game->node_versions.push_back(n.version);
		game->node_unchanged.push_back(unchanged ? 1 : 0);
	}
	if (changes_only) {
		game->sent = std::move(sent);
	}
	rc_draw_list l{};
	l.size = sizeof(rc_draw_list);
	l.nodes = game->nodes.data();
	l.node_count = u32(game->nodes.size());
	l.cmds = game->cmds.data();
	l.cmd_count = u32(game->cmds.size());
	l.points = game->points.data();
	l.point_count = u32(game->points.size() / 2);
	l.colors = game->colors.data();
	l.color_count = u32(game->colors.size() / 4);
	l.indices = game->indices.data();
	l.index_count = u32(game->indices.size());
	l.text = game->text.data();
	l.text_length = u32(game->text.size());
	l.node_ids = game->node_ids.data();
	l.node_versions = game->node_versions.data();
	l.node_unchanged = game->node_unchanged.data();
	fill(out, l);
	return RC_OK;
}

} // namespace

rc_status rc_game_draw_list(rc_game *game, rc_draw_list *out) {
	return guarded([&]() -> rc_status { return draw_list(game, out, false); });
}

rc_status rc_game_draw_changes(rc_game *game, rc_draw_list *out) {
	return guarded([&]() -> rc_status { return draw_list(game, out, true); });
}

rc_status rc_game_hud(rc_game *game, const char **text, size_t *length) {
	return guarded([&]() -> rc_status {
		if (game == nullptr || text == nullptr) {
			return fail(RC_E_ARGUMENT, "rc_game_hud: null argument");
		}
		game->hud = game->session->hud();
		*text = game->hud.c_str();
		if (length != nullptr) {
			*length = game->hud.size();
		}
		return RC_OK;
	});
}

rc_status rc_game_record(rc_game *game, const char *extra_json, size_t extra_length, uint32_t flags, const char **out, size_t *out_length) {
	return guarded([&]() -> rc_status {
		if (game == nullptr || out == nullptr) {
			return fail(RC_E_ARGUMENT, "rc_game_record: null argument");
		}
		nlohmann::json extra = nlohmann::json::object();
		if (extra_json != nullptr) {
			extra = nlohmann::json::parse(std::string_view(extra_json, extra_length));
			if (!extra.is_object()) {
				return fail(RC_E_ARGUMENT, "rc_game_record: extra is not a JSON object");
			}
		}
		const nlohmann::json list = rcg::render::list_json(game->session->draw());
		extra["draw"] = (flags & RC_RECORD_DRAW_LIST) != 0 ? list : nlohmann::json(rcg::digest::sha(list));
		game->record = rcg::digest::encode(game->session->record(extra));
		*out = game->record.c_str();
		if (out_length != nullptr) {
			*out_length = game->record.size();
		}
		return RC_OK;
	});
}

rc_status rc_digest_sha(const char *json, size_t length, char out[65]) {
	return guarded([&]() -> rc_status {
		if (json == nullptr || out == nullptr) {
			return fail(RC_E_ARGUMENT, "rc_digest_sha: null argument");
		}
		const std::string sha = rcg::digest::sha(nlohmann::json::parse(std::string_view(json, length)));
		std::memcpy(out, sha.c_str(), 65);
		return RC_OK;
	});
}

rc_status rc_tessellate_arc(float cx, float cy, double radius, double start_angle, double end_angle, int32_t arc_point_count, float *out_xy,
		uint32_t capacity, uint32_t *written) {
	return guarded([&]() -> rc_status {
		if (arc_point_count < 2 || (out_xy == nullptr && capacity > 0)) {
			return fail(RC_E_ARGUMENT, "rc_tessellate_arc: fewer than 2 points or a null buffer");
		}
		const std::vector<rc::V2> pts = rcg::render::tessellate_arc(rc::V2{cx, cy}, radius, start_angle, end_angle, arc_point_count);
		const uint32_t n = std::min<uint32_t>(capacity, u32(pts.size()));
		for (uint32_t i = 0; i < n; i++) {
			out_xy[2 * i] = pts[i].x;
			out_xy[2 * i + 1] = pts[i].y;
		}
		if (written != nullptr) {
			*written = n;
		}
		return RC_OK;
	});
}

} // extern "C"
