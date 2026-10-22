#include "ricochet_game.hpp"

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>
#include <set>

using namespace godot;

namespace rc_godot {
namespace {

// the game's log: Godot's
void log_to_godot(void *, int32_t level, const char *message) {
	if (level >= RC_LOG_ERROR) {
		UtilityFunctions::push_error(String::utf8(message));
	} else if (level == RC_LOG_WARNING) {
		UtilityFunctions::push_warning(String::utf8(message));
	} else {
		UtilityFunctions::print(String::utf8(message));
	}
}

void report(const char *what) { UtilityFunctions::push_error(String("RicochetGame: ") + what + ": " + String::utf8(rc_last_error())); }

template <class T>
void append(std::string &blob, const T &v) {
	blob.append(reinterpret_cast<const char *>(&v), sizeof(T));
}

Color color_at(const float *c) { return Color(c[0], c[1], c[2], c[3]); }

} // namespace

// ---------------------------------------------------------------- RicochetLayer

void RicochetLayer::set_cmds(std::vector<LayerCmd> cmds, std::string blob) {
	if (blob == blob_) {
		return;
	}
	cmds_ = std::move(cmds);
	blob_ = std::move(blob);
	queue_redraw();
}

void RicochetLayer::_draw() {
	for (const LayerCmd &c : cmds_) {
		const rc_draw_cmd &k = c.cmd;
		const Color col = c.colors.is_empty() ? Color(1, 1, 1, 1) : c.colors[0];
		const float width = float(k.width);
		switch (k.kind) {
			case RC_DRAW_LINE: draw_line(c.points[0], c.points[1], col, width); break;
			case RC_DRAW_ARC: draw_arc(c.points[0], float(k.radius), float(k.start_angle), float(k.end_angle), k.arc_point_count, col, width); break;
			case RC_DRAW_POLYGON: draw_polygon(c.points, c.colors); break;
			case RC_DRAW_COLORED_POLYGON: draw_colored_polygon(c.points, col); break;
			case RC_DRAW_CIRCLE: draw_circle(c.points[0], float(k.radius), col); break;
			case RC_DRAW_RECT: draw_rect(Rect2(c.points[0], c.points[1]), col); break;
			case RC_DRAW_POLYLINE: draw_polyline(c.points, col, width); break;
			case RC_DRAW_MULTILINE: draw_multiline(c.points, col, width); break;
			case RC_DRAW_DASHED_LINE: draw_dashed_line(c.points[0], c.points[1], col, width, float(k.dash)); break;
			case RC_DRAW_TEXT:
				draw_string(ThemeDB::get_singleton()->get_fallback_font(), c.points[0], c.text, HorizontalAlignment(k.alignment), -1,
						k.font_size, col);
				break;
			case RC_DRAW_TRIANGLES:
				RenderingServer::get_singleton()->canvas_item_add_triangle_array(get_canvas_item(), c.indices, c.points, c.colors);
				break;
			case RC_DRAW_POLYGON2D:
				// the player's Polygon2D: its polygon rotated in the node's frame
				draw_set_transform(Vector2(), float(k.rotation), Vector2(1, 1));
				draw_colored_polygon(c.points, col);
				draw_set_transform(Vector2(), 0.0f, Vector2(1, 1));
				break;
			default: break; // RC_DRAW_LABEL: the HUD's Label (RicochetGame)
		}
	}
}

// ---------------------------------------------------------------- RicochetGame

RicochetGame::RicochetGame() = default;

RicochetGame::~RicochetGame() { rc_game_destroy(game_); }

void RicochetGame::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_level_path", "path"), &RicochetGame::set_level_path);
	ClassDB::bind_method(D_METHOD("get_level_path"), &RicochetGame::get_level_path);
	ClassDB::bind_method(D_METHOD("set_infinity", "value"), &RicochetGame::set_infinity);
	ClassDB::bind_method(D_METHOD("get_infinity"), &RicochetGame::get_infinity);
	ClassDB::bind_method(D_METHOD("set_visibility", "value"), &RicochetGame::set_visibility);
	ClassDB::bind_method(D_METHOD("get_visibility"), &RicochetGame::get_visibility);
	ClassDB::bind_method(D_METHOD("aim", "at"), &RicochetGame::aim);
	ClassDB::bind_method(D_METHOD("teleport", "at"), &RicochetGame::teleport);
	ClassDB::bind_method(D_METHOD("set_physics", "on"), &RicochetGame::set_physics);
	ClassDB::bind_method(D_METHOD("set_asynchronous", "on"), &RicochetGame::set_asynchronous);
	ClassDB::bind_method(D_METHOD("get_asynchronous"), &RicochetGame::get_asynchronous);
	ClassDB::bind_method(D_METHOD("set_animate", "on"), &RicochetGame::set_animate);
	ClassDB::bind_method(D_METHOD("get_animate"), &RicochetGame::get_animate);
	ClassDB::bind_method(D_METHOD("set_solve_budget_ms", "ms"), &RicochetGame::set_solve_budget_ms);
	ClassDB::bind_method(D_METHOD("get_solve_budget_ms"), &RicochetGame::get_solve_budget_ms);
	ClassDB::bind_method(D_METHOD("get_physics"), &RicochetGame::get_physics);
	ClassDB::bind_method(D_METHOD("record"), &RicochetGame::record);
	ClassDB::bind_method(D_METHOD("hud"), &RicochetGame::hud);
	ClassDB::bind_method(D_METHOD("layer_count"), &RicochetGame::layer_count);
	ClassDB::bind_method(D_METHOD("is_flying"), &RicochetGame::is_flying);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "level_path", PROPERTY_HINT_FILE, "*.json"), "set_level_path", "get_level_path");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "infinity", PROPERTY_HINT_ENUM, "Default:-1,Off:0,On:1"), "set_infinity", "get_infinity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "visibility", PROPERTY_HINT_ENUM, "Default:-1,Off:0,On:1"), "set_visibility", "get_visibility");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "physics"), "set_physics", "get_physics");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "asynchronous"), "set_asynchronous", "get_asynchronous");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "animate"), "set_animate", "get_animate");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "solve_budget_ms"), "set_solve_budget_ms", "get_solve_budget_ms");
}

void RicochetGame::_ready() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (rc_api_version() >> 16 != RC_API_VERSION_MAJOR) {
		UtilityFunctions::push_error("RicochetGame: the game library speaks another rc_api major version");
		return;
	}
	rc_set_log(log_to_godot, nullptr);
	if (!FileAccess::file_exists(level_path_)) {
		UtilityFunctions::push_error(String("RicochetGame: no level at ") + level_path_);
		return;
	}
	const CharString text = FileAccess::get_file_as_string(level_path_).utf8();
	rc_level *level = nullptr;
	if (rc_level_parse(text.get_data(), size_t(text.length()), &level) != RC_OK) {
		report(level_path_.utf8().get_data());
		return;
	}
	const rc_game_config cfg{sizeof(rc_game_config), infinity_, visibility_, asynchronous_ ? 1 : 0, animate_ ? 1 : 0, float(solve_budget_ms_), 0.0f};
	const rc_status s = rc_game_create(level, &cfg, &game_);
	rc_level_release(level);
	if (s != RC_OK) {
		report("rc_game_create");
		return;
	}
	layers_ = memnew(Node2D);
	layers_->set_name("Layers");
	add_child(layers_);
	rc_frame f{};
	f.size = sizeof f;
	rc_game_frame(game_, &f);
	if (f.camera) {
		camera_ = memnew(Camera2D);
		camera_->set_name("Camera");
		camera_->set_position_smoothing_enabled(f.camera_smoothing != 0);
		camera_->set_position_smoothing_speed(f.camera_smoothing_speed);
		camera_->set_limit(SIDE_LEFT, int(f.bounds_x));
		camera_->set_limit(SIDE_TOP, int(f.bounds_y));
		camera_->set_limit(SIDE_RIGHT, int(f.bounds_x + f.bounds_w));
		camera_->set_limit(SIDE_BOTTOM, int(f.bounds_y + f.bounds_h));
		camera_->set_position(Vector2(f.camera_x, f.camera_y));
		add_child(camera_);
		camera_->make_current();
	}
	sync();
}

void RicochetGame::_process(double delta) {
	if (game_ == nullptr) {
		return;
	}
	const Vector2 m = cursor();
	rc_game_set_cursor(game_, m.x, m.y);
	Input *in = Input::get_singleton();
	uint32_t held = 0, pressed = 0;
	const struct {
		const char *action;
		uint32_t key;
	} keys[] = {{"move_left", RC_KEY_LEFT}, {"move_right", RC_KEY_RIGHT}, {"move_up", RC_KEY_UP}, {"move_down", RC_KEY_DOWN}};
	for (const auto &k : keys) {
		held |= in->is_action_pressed(k.action) ? k.key : 0;
		pressed |= in->is_action_just_pressed(k.action) ? k.key : 0;
	}
	// the physics off: no tick for the player (a flight still flies)
	if (rc_game_advance(game_, physics_ || is_flying() ? delta : 0.0, held, pressed) != RC_OK) {
		report("rc_game_advance");
	}
	sync();
}

void RicochetGame::_input(const Ref<InputEvent> &ev) {
	if (game_ == nullptr) {
		return;
	}
	// a flight: every key but the movement keys speeds it up (the game ignores clicks)
	const Ref<InputEventKey> key = ev;
	const Ref<InputEventMouseButton> mouse = ev;
	if (key.is_valid() && key->is_pressed()) {
		for (const char *a : {"move_up", "move_down", "move_left", "move_right"}) {
			if (key->is_action(a)) {
				return;
			}
		}
		if (is_flying()) {
			if (!key->is_echo()) {
				event(RC_EVENT_ANY_KEY);
			}
			return;
		}
		switch (key->get_physical_keycode()) {
			case KEY_TAB: event(RC_EVENT_CYCLE_DISPLAY); get_viewport()->set_input_as_handled(); return;
			case KEY_G: event(RC_EVENT_TOGGLE_GRAVITY); return;
			case KEY_V: event(RC_EVENT_TOGGLE_VISIBILITY); return;
			case KEY_Z: event(RC_EVENT_UNDO); return;
			case KEY_R: event(RC_EVENT_RESET); return;
			case KEY_C: event(RC_EVENT_CLEAR_PLAN); return;
			case KEY_F12: debug_dump(); return;
			default: break;
		}
	}
	if (mouse.is_valid() && mouse->is_pressed()) {
		if (mouse->get_button_index() == MOUSE_BUTTON_LEFT) {
			event(RC_EVENT_PLAN_ADD);
		} else if (mouse->get_button_index() == MOUSE_BUTTON_RIGHT) {
			event(RC_EVENT_PLAN_REMOVE);
		}
		return;
	}
	if (ev->is_action_pressed("fire")) {
		event(RC_EVENT_FIRE);
	}
}

void RicochetGame::event(int32_t e) {
	// the cursor where the event happened, the game settled before the next frame shows it
	const Vector2 m = cursor();
	rc_game_set_cursor(game_, m.x, m.y);
	if (rc_game_event(game_, e) != RC_OK || rc_game_advance(game_, 0.0, 0, 0) != RC_OK) {
		report("rc_game_event");
	}
	sync();
}

void RicochetGame::sync() {
	rc_frame f{};
	f.size = sizeof f;
	rc_game_frame(game_, &f);
	if (camera_ != nullptr) {
		camera_->set_position(Vector2(f.camera_x, f.camera_y));
	}
	// (Stage 120: only what changed carries commands; an unchanged layer keeps its drawing)
	rc_draw_list l{};
	l.size = sizeof l;
	if (rc_game_draw_changes(game_, &l) != RC_OK) {
		report("rc_game_draw_list");
		return;
	}
	std::set<std::string> live;
	for (uint32_t i = 0; i < l.node_count; i++) {
		const rc_draw_node &n = l.nodes[i];
		const std::string key(l.text + n.key_first, n.key_length);
		live.insert(key);
		RicochetLayer *layer = by_key_.count(key) ? by_key_[key] : nullptr;
		if (layer == nullptr) {
			layer = memnew(RicochetLayer);
			layer->set_name(String::utf8(key.c_str()).replace("\"", "").replace(",", "_").replace("[", "").replace("]", ""));
			layers_->add_child(layer);
			by_key_[key] = layer;
		}
		if (layer->get_index() != int(i)) {
			layers_->move_child(layer, int(i));
		}
		if (layer->get_z_index() != n.z) {
			layer->set_z_index(n.z);
		}
		if (layer->get_position() != Vector2(n.x, n.y)) {
			layer->set_position(Vector2(n.x, n.y));
		}
		if (layer->get_modulate() != color_at(n.modulate)) {
			layer->set_modulate(color_at(n.modulate));
		}
		if (l.node_unchanged[i]) {
			continue;
		}
		std::vector<LayerCmd> cmds;
		std::string blob;
		for (uint32_t j = n.cmd_first; j < n.cmd_first + n.cmd_count; j++) {
			LayerCmd c;
			c.cmd = l.cmds[j];
			append(blob, c.cmd.kind);
			for (const double d : {c.cmd.radius, c.cmd.start_angle, c.cmd.end_angle, c.cmd.width, c.cmd.dash, c.cmd.rotation}) {
				append(blob, d);
			}
			for (const int32_t v : {c.cmd.arc_point_count, c.cmd.alignment, c.cmd.font_size}) {
				append(blob, v);
			}
			const float *pts = l.points + 2 * c.cmd.point_first;
			blob.append(reinterpret_cast<const char *>(pts), 2 * sizeof(float) * c.cmd.point_count);
			for (uint32_t k = 0; k < c.cmd.point_count; k++) {
				c.points.push_back(Vector2(pts[2 * k], pts[2 * k + 1]));
			}
			const float *cols = l.colors + 4 * c.cmd.color_first;
			blob.append(reinterpret_cast<const char *>(cols), 4 * sizeof(float) * c.cmd.color_count);
			for (uint32_t k = 0; k < c.cmd.color_count; k++) {
				c.colors.push_back(color_at(cols + 4 * k));
			}
			const int32_t *idx = l.indices + c.cmd.index_first;
			blob.append(reinterpret_cast<const char *>(idx), sizeof(int32_t) * c.cmd.index_count);
			for (uint32_t k = 0; k < c.cmd.index_count; k++) {
				c.indices.push_back(idx[k]);
			}
			const std::string text(l.text + c.cmd.text_first, c.cmd.text_length);
			blob += text;
			blob.push_back('\0');
			c.text = String::utf8(text.c_str());
			if (c.cmd.kind == RC_DRAW_LABEL) {
				// the HUD: a Label at (10, 10), white, a child of its layer
				if (hud_ == nullptr) {
					hud_ = memnew(Label);
					hud_->set_position(Vector2(10, 10));
					hud_->add_theme_color_override("font_color", Color(1, 1, 1, 1));
					Control *holder = memnew(Control);
					holder->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
					holder->add_child(hud_);
					layer->add_child(holder);
				}
				hud_->set_text(c.text);
			}
			cmds.push_back(std::move(c));
		}
		layer->set_cmds(std::move(cmds), std::move(blob));
	}
	for (auto it = by_key_.begin(); it != by_key_.end();) {
		if (live.count(it->first) == 0) {
			it->second->queue_free();
			layers_->remove_child(it->second);
			it = by_key_.erase(it);
		} else {
			++it;
		}
	}
}

Vector2 RicochetGame::cursor() const { return aimed_ ? aim_ : get_global_mouse_position(); }

void RicochetGame::aim(const Vector2 &at) {
	aimed_ = true;
	aim_ = at;
	if (game_ != nullptr) {
		rc_game_set_cursor(game_, at.x, at.y);
		rc_game_advance(game_, 0.0, 0, 0);
		sync();
	}
}

void RicochetGame::teleport(const Vector2 &at) {
	if (game_ != nullptr) {
		rc_game_teleport(game_, at.x, at.y);
		rc_game_advance(game_, 0.0, 0, 0);
		sync();
	}
}

String RicochetGame::record() const {
	if (game_ == nullptr) {
		return String();
	}
	const char *out = nullptr;
	size_t n = 0;
	if (rc_game_record(game_, nullptr, 0, 0, &out, &n) != RC_OK) {
		return String();
	}
	return String::utf8(out, int64_t(n));
}

// F12 (the GDScript game's DEBUG STATE): the game's state, printed and saved to
// user://debug_dumps (one file per press)
void RicochetGame::debug_dump() {
	const char *text = nullptr;
	size_t n = 0;
	if (rc_game_debug_dump(game_, &text, &n) != RC_OK) {
		UtilityFunctions::push_error(String::utf8(rc_last_error()));
		return;
	}
	const String dump = String::utf8(text, int64_t(n));
	DirAccess::make_dir_recursive_absolute("user://debug_dumps");
	const String path = "user://debug_dumps/" + Time::get_singleton()->get_datetime_string_from_system().replace(":", "-") + ".json";
	const Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (f.is_valid()) {
		f->store_string(dump);
	}
	UtilityFunctions::print("=== DEBUG STATE (F12) ===\n", dump, "\n=== END DEBUG STATE === saved to ",
		ProjectSettings::get_singleton()->globalize_path(path));
}

String RicochetGame::hud() const {
	const char *text = nullptr;
	if (game_ == nullptr || rc_game_hud(game_, &text, nullptr) != RC_OK) {
		return String();
	}
	return String::utf8(text);
}

int RicochetGame::layer_count() const { return int(by_key_.size()); }

bool RicochetGame::is_flying() const {
	if (game_ == nullptr) {
		return false;
	}
	rc_frame f{};
	f.size = sizeof f;
	rc_game_frame(game_, &f);
	return f.flying != 0;
}

void register_adapter() {
	GDREGISTER_CLASS(RicochetLayer);
	GDREGISTER_CLASS(RicochetGame);
}

} // namespace rc_godot
