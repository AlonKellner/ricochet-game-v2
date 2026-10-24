// Stage 118: the C API (rc_api.h) over the native game (rcg::Session).
#include "rc_api.h"

#include "rc/error.hpp"
#include "rcg/digest.hpp"
#include "rcg/editor.hpp"
#include "rcg/level.hpp"
#include "rcg/render.hpp"
#include "rcg/present.hpp"
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

// A draw list's borrowed pools (valid until the next non-const call) and what
// rc_*_draw_changes last sent.
struct DrawPools {
	std::vector<rc_draw_node> nodes;
	std::vector<rc_draw_cmd> cmds;
	std::vector<float> points, colors;
	std::vector<int32_t> indices;
	std::string text;
	std::vector<uint64_t> node_ids, node_versions;
	std::vector<uint8_t> node_unchanged;
	std::map<uint64_t, uint64_t> sent;        // id -> the version last sent
	std::map<uint64_t, std::string> key_text; // id -> its key's JSON
};

struct rc_game {
	std::shared_ptr<const rcg::Level> level;
	std::unique_ptr<rcg::Session> session;
	std::unique_ptr<rcg::Presenter> presenter; // 1.3: animate
	DrawPools pools;
	std::string hud;
	std::string record;
	std::string dump; // 1.5
};

struct rc_editor {
	std::unique_ptr<rcg::Editor> editor;
	DrawPools pools;
	std::string result, document;
};

constexpr bool same_code(long a, long b) { return a == b; }
static_assert(same_code(RC_PTR_DOWN, rcg::Editor::PtrDown) &&
			  same_code(RC_PTR_UP, rcg::Editor::PtrUp) &&
			  same_code(RC_BTN_LEFT, rcg::Editor::BtnLeft) &&
			  same_code(RC_BTN_RIGHT, rcg::Editor::BtnRight) &&
			  same_code(RC_BTN_MIDDLE, rcg::Editor::BtnMiddle) &&
			  same_code(RC_MOD_SHIFT, rcg::Editor::ModShift) &&
			  same_code(RC_MOD_CTRL, rcg::Editor::ModCtrl) &&
			  same_code(RC_MOD_ALT, rcg::Editor::ModAlt) &&
			  same_code(RC_EDKEY_ESCAPE, rcg::Editor::KeyEscape) &&
			  same_code(RC_EDKEY_ENTER, rcg::Editor::KeyEnter) &&
			  same_code(RC_EDKEY_TAB, rcg::Editor::KeyTab) &&
			  same_code(RC_EDKEY_BACKSPACE, rcg::Editor::KeyBackspace) &&
			  same_code(RC_EDKEY_DELETE, rcg::Editor::KeyDelete) &&
			  same_code(RC_EDKEY_LEFT, rcg::Editor::KeyLeft) &&
			  same_code(RC_EDKEY_RIGHT, rcg::Editor::KeyRight) &&
			  same_code(RC_EDKEY_UP, rcg::Editor::KeyUp) &&
			  same_code(RC_EDKEY_DOWN, rcg::Editor::KeyDown) &&
			  same_code(RC_EDKEY_F1, rcg::Editor::KeyF1),
	"rc_api.h's editor input codes are rcg::Editor's");

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
		rc_game_config cfg{sizeof(rc_game_config), -1, -1, 0, 0, 0.0f, 0.0f};
		if (config != nullptr) {
			std::memcpy(&cfg, config, std::min<size_t>(config->size, sizeof(rc_game_config)));
		}
		rcg::SessionConfig sc;
		// (cfg.infinity: ignored since 1.7 -- infinity is always on, Stage 136)
		if (cfg.visibility >= 0) {
			sc.visibility = cfg.visibility != 0;
		}
		sc.async = cfg.async != 0;
		sc.budget = cfg.budget_ms > 0.0f && std::isfinite(cfg.budget_ms) ? double(cfg.budget_ms) / 1000.0 : 0.0;
		auto game = std::make_unique<rc_game>();
		game->level = level->level;
		game->session = std::make_unique<rcg::Session>(*game->level, sc);
		game->session->settle();
		if (cfg.animate != 0) {
			rcg::PresentConfig pc;
			if (cfg.late_ms > 0.0f && std::isfinite(cfg.late_ms)) {
				pc.late_seconds = double(cfg.late_ms) / 1000.0;
			}
			game->presenter = std::make_unique<rcg::Presenter>(pc);
		}
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
		if (game->presenter) {
			game->presenter->advance(dt);
		}
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
		const rc::V2 target = game->presenter && !s.flying() ? game->presenter->player(s) : s.camera_target();
		f.camera_x = target.x;
		f.camera_y = target.y;
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
		f.won = s.won() ? 1 : 0;
		fill(out, f);
		return RC_OK;
	});
}

namespace {

// Frame nodes into the pools; changes_only: unchanged nodes (since the last
// such call) without their commands.
rc_status pack(DrawPools *pools, std::vector<rcg::render::FrameNode> nodes, rc_draw_list *out, bool changes_only) {
	std::stable_sort(nodes.begin(), nodes.end(), [](const auto &a, const auto &b) { return a.z < b.z; });
	pools->nodes.clear();
	pools->cmds.clear();
	pools->points.clear();
	pools->colors.clear();
	pools->indices.clear();
	pools->text.clear();
	pools->node_ids.clear();
	pools->node_versions.clear();
	pools->node_unchanged.clear();
	std::map<uint64_t, uint64_t> sent;
	for (const rcg::render::FrameNode &n : nodes) {
		const uint64_t id = n.key.id();
		auto kt = pools->key_text.find(id);
		if (kt == pools->key_text.end()) {
			kt = pools->key_text.emplace(id, n.key.json().dump()).first;
		}
		const std::string &key = kt->second;
		const bool unchanged = changes_only && pools->sent.count(id) && pools->sent[id] == n.version;
		sent[id] = n.version;
		rc_draw_node dn{};
		dn.key_first = u32(pools->text.size());
		dn.key_length = u32(key.size());
		pools->text += key;
		dn.z = n.z;
		dn.x = n.position.x;
		dn.y = n.position.y;
		std::copy(n.modulate.begin(), n.modulate.end(), dn.modulate);
		dn.cmd_first = u32(pools->cmds.size());
		dn.cmd_count = unchanged ? 0 : u32(n.calls->size());
		for (size_t ci = 0; !unchanged && ci < n.calls->size(); ci++) {
			const rcg::render::Call &c = (*n.calls)[ci];
			rc_draw_cmd dc{};
			dc.kind = int32_t(c.kind);
			dc.point_first = u32(pools->points.size() / 2);
			dc.point_count = u32(c.pts.size());
			for (const rc::V2 &p : c.pts) {
				pools->points.push_back(p.x);
				pools->points.push_back(p.y);
			}
			dc.color_first = u32(pools->colors.size() / 4);
			dc.color_count = u32(c.colors.size());
			for (const rcg::render::Color &k : c.colors) {
				pools->colors.insert(pools->colors.end(), k.begin(), k.end());
			}
			dc.index_first = u32(pools->indices.size());
			dc.index_count = u32(c.indices.size());
			pools->indices.insert(pools->indices.end(), c.indices.begin(), c.indices.end());
			dc.text_first = u32(pools->text.size());
			dc.text_length = u32(c.text.size());
			pools->text += c.text;
			dc.arc_point_count = c.point_count;
			dc.alignment = c.alignment;
			dc.font_size = c.font_size;
			dc.radius = c.radius;
			dc.start_angle = c.start;
			dc.end_angle = c.end;
			dc.width = c.width;
			dc.dash = c.dash;
			dc.rotation = c.rotation;
			pools->cmds.push_back(dc);
		}
		pools->nodes.push_back(dn);
		pools->node_ids.push_back(id);
		pools->node_versions.push_back(n.version);
		pools->node_unchanged.push_back(unchanged ? 1 : 0);
	}
	if (changes_only) {
		pools->sent = std::move(sent);
	}
	rc_draw_list l{};
	l.size = sizeof(rc_draw_list);
	l.nodes = pools->nodes.data();
	l.node_count = u32(pools->nodes.size());
	l.cmds = pools->cmds.data();
	l.cmd_count = u32(pools->cmds.size());
	l.points = pools->points.data();
	l.point_count = u32(pools->points.size() / 2);
	l.colors = pools->colors.data();
	l.color_count = u32(pools->colors.size() / 4);
	l.indices = pools->indices.data();
	l.index_count = u32(pools->indices.size());
	l.text = pools->text.data();
	l.text_length = u32(pools->text.size());
	l.node_ids = pools->node_ids.data();
	l.node_versions = pools->node_versions.data();
	l.node_unchanged = pools->node_unchanged.data();
	fill(out, l);
	return RC_OK;
}

// The game's draw list.
rc_status draw_list(rc_game *game, rc_draw_list *out, bool changes_only) {
	if (game == nullptr || !sized(out)) {
		return fail(RC_E_ARGUMENT, "rc_game_draw_list: null game or unsized list");
	}
	std::vector<rcg::render::FrameNode> nodes = game->session->draw_frame();
	if (game->presenter) {
		nodes = game->presenter->frame(std::move(nodes), *game->session);
	}
	return pack(&game->pools, std::move(nodes), out, changes_only);
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

rc_status rc_game_debug_dump(rc_game *game, uint32_t flags, const char **out, size_t *out_length) {
	return guarded([&]() -> rc_status {
		if (game == nullptr || out == nullptr) {
			return fail(RC_E_ARGUMENT, "rc_game_debug_dump: null argument");
		}
		game->dump = (flags & RC_DEBUG_SUMMARY) != 0 ? game->session->debug_summary() : game->session->debug_dump().dump(1, '\t');
		*out = game->dump.c_str();
		if (out_length != nullptr) {
			*out_length = game->dump.size();
		}
		return RC_OK;
	});
}

rc_status rc_game_set_plan(rc_game *game, const char *json, size_t length) {
	return guarded([&]() -> rc_status {
		if (game == nullptr || json == nullptr) {
			return fail(RC_E_ARGUMENT, "rc_game_set_plan: null argument");
		}
		const nlohmann::json j = nlohmann::json::parse(std::string_view(json, length));
		if (!j.is_array()) {
			return fail(RC_E_ARGUMENT, "rc_game_set_plan: not a JSON array");
		}
		std::vector<rc::PlanItem> items;
		for (const nlohmann::json &e : j) {
			rc::PlanItem it;
			it.surface_id = e.at("surface_id").get<int>();
			const nlohmann::json &side = e.at("side");
			it.left = side.is_string() ? side.get<std::string>() == "LEFT" : side.get<int>() == 0;
			if (e.contains("click") && !e["click"].is_null()) {
				it.hint = rc::effects::F32Point{e["click"][0].get<float>(), e["click"][1].get<float>()};
			}
			if (e.contains("span") && !e["span"].is_null()) {
				const nlohmann::json &sp = e["span"];
				it.span = std::array<double, 6>{sp["start"][0].get<double>(), sp["start"][1].get<double>(), sp["end"][0].get<double>(),
					sp["end"][1].get<double>(), sp["via"][0].get<double>(), sp["via"][1].get<double>()};
			}
			items.push_back(it);
		}
		game->session->set_plan(items);
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

// ------------------------------------------------------------------ the level editor (1.8)

rc_status rc_editor_create(const char *json, size_t length, rc_editor **out) {
	return guarded([&]() -> rc_status {
		if (out == nullptr) {
			return fail(RC_E_ARGUMENT, "rc_editor_create: null out");
		}
		*out = nullptr;
		auto e = std::make_unique<rc_editor>();
		if (json != nullptr) {
			nlohmann::json doc;
			try {
				doc = nlohmann::json::parse(std::string_view(json, length));
			} catch (const nlohmann::json::parse_error &ex) {
				return fail(RC_E_LEVEL, std::string("not JSON: ") + ex.what());
			}
			e->editor = std::make_unique<rcg::Editor>(&doc);
		} else {
			e->editor = std::make_unique<rcg::Editor>();
		}
		*out = e.release();
		return RC_OK;
	});
}

void rc_editor_destroy(rc_editor *editor) { delete editor; }

rc_status rc_editor_command(rc_editor *editor, const char *json, size_t length, const char **result, size_t *result_length) {
	return guarded([&]() -> rc_status {
		if (editor == nullptr || json == nullptr || result == nullptr) {
			return fail(RC_E_ARGUMENT, "rc_editor_command: null argument");
		}
		nlohmann::json cmd;
		try {
			cmd = nlohmann::json::parse(std::string_view(json, length));
		} catch (const nlohmann::json::parse_error &ex) {
			cmd = nullptr;
			editor->result = nlohmann::json{{"ok", false}, {"error", std::string("not JSON: ") + ex.what()}}.dump();
		}
		if (!cmd.is_null()) {
			editor->result = editor->editor->command(cmd).dump();
		}
		*result = editor->result.c_str();
		if (result_length != nullptr) {
			*result_length = editor->result.size();
		}
		return RC_OK;
	});
}

rc_status rc_editor_document(rc_editor *editor, const char **text, size_t *length) {
	return guarded([&]() -> rc_status {
		if (editor == nullptr || text == nullptr) {
			return fail(RC_E_ARGUMENT, "rc_editor_document: null argument");
		}
		editor->document = editor->editor->document_text();
		*text = editor->document.c_str();
		if (length != nullptr) {
			*length = editor->document.size();
		}
		return RC_OK;
	});
}

rc_status rc_editor_pointer(rc_editor *editor, int32_t kind, float x, float y, uint32_t buttons, uint32_t mods) {
	return guarded([&]() -> rc_status {
		if (editor == nullptr || kind < RC_PTR_MOVE || kind > RC_PTR_UP) {
			return fail(RC_E_ARGUMENT, "rc_editor_pointer: null editor or unknown kind");
		}
		editor->editor->pointer(kind, x, y, buttons, mods);
		return RC_OK;
	});
}

rc_status rc_editor_key(rc_editor *editor, int32_t key, uint32_t mods, int32_t down) {
	return guarded([&]() -> rc_status {
		if (editor == nullptr) {
			return fail(RC_E_ARGUMENT, "rc_editor_key: null editor");
		}
		editor->editor->key(key, mods, down != 0);
		return RC_OK;
	});
}

rc_status rc_editor_text(rc_editor *editor, const char *utf8, size_t length) {
	return guarded([&]() -> rc_status {
		if (editor == nullptr || (utf8 == nullptr && length > 0)) {
			return fail(RC_E_ARGUMENT, "rc_editor_text: null argument");
		}
		editor->editor->text(std::string(utf8 == nullptr ? "" : utf8, length));
		return RC_OK;
	});
}

rc_status rc_editor_scroll(rc_editor *editor, float dx, float dy, uint32_t mods) {
	return guarded([&]() -> rc_status {
		if (editor == nullptr) {
			return fail(RC_E_ARGUMENT, "rc_editor_scroll: null editor");
		}
		editor->editor->scroll(dx, dy, mods);
		return RC_OK;
	});
}

rc_status rc_editor_resize(rc_editor *editor, float width, float height) {
	return guarded([&]() -> rc_status {
		if (editor == nullptr || !(width > 0) || !(height > 0)) {
			return fail(RC_E_ARGUMENT, "rc_editor_resize: null editor or an empty window");
		}
		editor->editor->resize(width, height);
		return RC_OK;
	});
}

rc_status rc_editor_draw_list(rc_editor *editor, rc_draw_list *out) {
	return guarded([&]() -> rc_status {
		if (editor == nullptr || !sized(out)) {
			return fail(RC_E_ARGUMENT, "rc_editor_draw_list: null editor or unsized list");
		}
		return pack(&editor->pools, editor->editor->draw(), out, false);
	});
}

rc_status rc_editor_draw_changes(rc_editor *editor, rc_draw_list *out) {
	return guarded([&]() -> rc_status {
		if (editor == nullptr || !sized(out)) {
			return fail(RC_E_ARGUMENT, "rc_editor_draw_changes: null editor or unsized list");
		}
		return pack(&editor->pools, editor->editor->draw(), out, true);
	});
}

rc_status rc_editor_get_status(rc_editor *editor, rc_editor_status *out) {
	return guarded([&]() -> rc_status {
		if (editor == nullptr || !sized(out)) {
			return fail(RC_E_ARGUMENT, "rc_editor_get_status: null editor or unsized status");
		}
		const rcg::Editor::Status st = editor->editor->status();
		rc_editor_status s{};
		s.size = sizeof(rc_editor_status);
		s.dirty = st.dirty ? 1 : 0;
		s.errors = st.errors;
		s.mode = st.mode == rcg::Editor::Mode::Play ? RC_EDITOR_PLAY : RC_EDITOR_EDIT;
		s.tool = int32_t(st.tool);
		s.save_requested = st.save_requested ? 1 : 0;
		s.version = st.version;
		s.play_x = st.play_from.x;
		s.play_y = st.play_from.y;
		fill(out, s);
		return RC_OK;
	});
}

} // extern "C"
