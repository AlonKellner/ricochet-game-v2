#include "ricochet_editor.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>
#include <functional>

using namespace godot;

namespace rc_godot {
namespace {

void report(const char *what) { UtilityFunctions::push_error(String("RicochetEditor: ") + what + ": " + String::utf8(rc_last_error())); }

// Godot's key -> the editor's (rc_api.h: letters and digits as ASCII, RC_EDKEY_* for the rest); -1: none
int32_t key_code(Key k) {
	if ((k >= KEY_A && k <= KEY_Z) || (k >= KEY_0 && k <= KEY_9)) {
		return int32_t(k);
	}
	if (k >= KEY_F1 && k <= KEY_F12) {
		return RC_EDKEY_F1 + int32_t(k - KEY_F1);
	}
	switch (k) {
		case KEY_SPACE: return RC_EDKEY_SPACE;
		case KEY_ESCAPE: return RC_EDKEY_ESCAPE;
		case KEY_ENTER:
		case KEY_KP_ENTER: return RC_EDKEY_ENTER;
		case KEY_TAB: return RC_EDKEY_TAB;
		case KEY_BACKSPACE: return RC_EDKEY_BACKSPACE;
		case KEY_DELETE: return RC_EDKEY_DELETE;
		case KEY_LEFT: return RC_EDKEY_LEFT;
		case KEY_RIGHT: return RC_EDKEY_RIGHT;
		case KEY_UP: return RC_EDKEY_UP;
		case KEY_DOWN: return RC_EDKEY_DOWN;
		default: return -1;
	}
}

uint32_t mods_of(const Ref<InputEventWithModifiers> &e) {
	uint32_t m = 0;
	m |= e->is_shift_pressed() ? RC_MOD_SHIFT : 0u;
	m |= e->is_ctrl_pressed() || e->is_meta_pressed() ? RC_MOD_CTRL : 0u; // Control, or Command on a Mac
	m |= e->is_alt_pressed() ? RC_MOD_ALT : 0u;
	return m;
}

uint32_t button_of(MouseButton b) {
	return b == MOUSE_BUTTON_LEFT ? RC_BTN_LEFT : b == MOUSE_BUTTON_RIGHT ? RC_BTN_RIGHT : b == MOUSE_BUTTON_MIDDLE ? RC_BTN_MIDDLE : 0u;
}

} // namespace

void RicochetEditor::run(const char *json) {
	const char *out = nullptr;
	if (rc_editor_command(editor_, json, std::strlen(json), &out, nullptr) != RC_OK) {
		report("rc_editor_command");
	}
}

RicochetEditor::RicochetEditor() = default;

RicochetEditor::~RicochetEditor() { rc_editor_destroy(editor_); }

void RicochetEditor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_level_path", "path"), &RicochetEditor::set_level_path);
	ClassDB::bind_method(D_METHOD("get_level_path"), &RicochetEditor::get_level_path);
	ClassDB::bind_method(D_METHOD("set_save_path", "path"), &RicochetEditor::set_save_path);
	ClassDB::bind_method(D_METHOD("get_save_path"), &RicochetEditor::get_save_path);
	ClassDB::bind_method(D_METHOD("command", "json"), &RicochetEditor::command);
	ClassDB::bind_method(D_METHOD("document"), &RicochetEditor::document);
	ClassDB::bind_method(D_METHOD("layer_count"), &RicochetEditor::layer_count);
	ClassDB::bind_method(D_METHOD("is_playing"), &RicochetEditor::is_playing);
	ClassDB::bind_method(D_METHOD("save_target"), &RicochetEditor::save_target);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "level_path", PROPERTY_HINT_FILE, "*.json"), "set_level_path", "get_level_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "save_path"), "set_save_path", "get_save_path");
}

void RicochetEditor::_ready() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (rc_api_version() >> 16 != RC_API_VERSION_MAJOR || (rc_api_version() & 0xFFFF) < 8) {
		UtilityFunctions::push_error("RicochetEditor: the game library has no editor (rc_api 1.8)");
		return;
	}
	const PackedStringArray args = OS::get_singleton()->get_cmdline_user_args();
	for (int i = 0; i < args.size(); i++) {
		if (args[i].begins_with("--level=")) {
			level_path_ = args[i].substr(8);
		}
	}
	rc_status s = RC_OK;
	if (!level_path_.is_empty() && FileAccess::file_exists(level_path_)) {
		const CharString text = FileAccess::get_file_as_string(level_path_).utf8();
		s = rc_editor_create(text.get_data(), size_t(text.length()), &editor_);
	} else {
		s = rc_editor_create(nullptr, 0, &editor_);
	}
	if (s != RC_OK) {
		report(level_path_.utf8().get_data());
		return;
	}
	layers_ = memnew(Node2D);
	layers_->set_name("Layers");
	add_child(layers_);
	size_ = get_viewport_rect().size;
	rc_editor_resize(editor_, size_.x, size_.y);
	run("{\"op\": \"frame\"}");
	list_levels();
	sync();
}

String RicochetEditor::levels_dir() const {
	const String repo = ProjectSettings::get_singleton()->globalize_path("res://").path_join("../../data/levels").simplify_path();
	return DirAccess::dir_exists_absolute(repo) ? repo : String("res://data/levels");
}

void RicochetEditor::list_levels() {
	const String root = levels_dir();
	Array names;
	std::function<void(const String &)> walk = [&](const String &rel) {
		const Ref<DirAccess> d = DirAccess::open(rel.is_empty() ? root : root.path_join(rel));
		if (d.is_null()) {
			return;
		}
		for (const String &f : d->get_files()) {
			if (f.ends_with(".json")) {
				names.push_back(rel.is_empty() ? f.get_basename() : rel.path_join(f.get_basename()));
			}
		}
		for (const String &sub : d->get_directories()) {
			walk(rel.is_empty() ? sub : rel.path_join(sub));
		}
	};
	walk("");
	names.sort();
	Dictionary cmd;
	cmd["op"] = "levels";
	cmd["list"] = names;
	for (const String &prefix : {String("res://data/levels/"), root + String("/")}) {
		if (level_path_.begins_with(prefix) && level_path_.ends_with(".json")) {
			cmd["current"] = level_path_.substr(prefix.length()).get_basename();
		}
	}
	const CharString text = JSON::stringify(cmd).utf8();
	const char *out = nullptr;
	if (rc_editor_command(editor_, text.get_data(), size_t(text.length()), &out, nullptr) != RC_OK) {
		report("rc_editor_command (levels)");
	}
}

void RicochetEditor::open_requested() {
	const char *out = nullptr;
	size_t n = 0;
	const char *take = "{\"op\": \"open_request\"}";
	if (rc_editor_command(editor_, take, std::strlen(take), &out, &n) != RC_OK) {
		report("rc_editor_command (open_request)");
		return;
	}
	const Dictionary res = JSON::parse_string(String::utf8(out, int64_t(n)));
	const String name = res.get("name", String());
	const String as = res.get("as", String()); // Stage 165: a copy, loaded under its new name (unsaved)
	const String path = levels_dir().path_join(name + String(".json"));
	if (name.is_empty() || !FileAccess::file_exists(path)) {
		UtilityFunctions::push_error("RicochetEditor: no level " + path);
		return;
	}
	Dictionary cmd;
	cmd["op"] = "load";
	cmd["text"] = FileAccess::get_file_as_string(path);
	cmd["name"] = as.is_empty() ? name : as;
	if (!as.is_empty()) {
		cmd["unsaved"] = true;
	}
	const CharString text = JSON::stringify(cmd).utf8();
	if (rc_editor_command(editor_, text.get_data(), size_t(text.length()), &out, &n) != RC_OK) {
		report("rc_editor_command (load)");
		return;
	}
	level_path_ = as.is_empty() ? path : levels_dir().path_join(as + String(".json")); // (saves go to it)
	save_path_ = String();
	UtilityFunctions::print("RicochetEditor: opened ", path, as.is_empty() ? String() : String(" as ") + as);
}

void RicochetEditor::_process(double) {
	if (editor_ == nullptr || game_ != nullptr) {
		return;
	}
	const Vector2 size = get_viewport_rect().size;
	if (size != size_) {
		size_ = size;
		rc_editor_resize(editor_, size_.x, size_.y);
	}
	sync();
}

void RicochetEditor::_input(const Ref<InputEvent> &ev) {
	if (editor_ == nullptr) {
		return;
	}
	const Ref<InputEventKey> key = ev;
	if (game_ != nullptr) {
		// playing: the game takes its input; Esc returns to the editor
		if (key.is_valid() && key->is_pressed() && !key->is_echo() && key->get_keycode() == KEY_ESCAPE) {
			rc_editor_key(editor_, RC_EDKEY_ESCAPE, 0, 1);
			after_input();
			get_viewport()->set_input_as_handled();
		}
		return;
	}
	const Ref<InputEventMouseMotion> motion = ev;
	const Ref<InputEventMouseButton> button = ev;
	const Vector2 at = get_global_mouse_position(); // the canvas's pixels (the stretch applied)
	if (motion.is_valid()) {
		rc_editor_pointer(editor_, RC_PTR_MOVE, at.x, at.y, 0, mods_of(motion));
	} else if (button.is_valid()) {
		const MouseButton b = button->get_button_index();
		if (b == MOUSE_BUTTON_WHEEL_UP || b == MOUSE_BUTTON_WHEEL_DOWN) {
			if (button->is_pressed()) {
				rc_editor_scroll(editor_, 0.0f, b == MOUSE_BUTTON_WHEEL_UP ? -1.0f : 1.0f, mods_of(button));
			}
		} else if (button_of(b) != 0) {
			rc_editor_pointer(editor_, button->is_pressed() ? RC_PTR_DOWN : RC_PTR_UP, at.x, at.y, button_of(b), mods_of(button));
		}
	} else if (key.is_valid()) {
		const Key k = key->get_keycode() != KEY_NONE ? key->get_keycode() : key->get_physical_keycode();
		const int32_t code = key_code(k);
		const uint32_t mods = mods_of(key);
		if (code >= 0 && !(key->is_echo() && code >= RC_EDKEY_F1)) {
			rc_editor_key(editor_, code, mods, key->is_pressed() ? 1 : 0);
		}
		const char32_t ch = char32_t(key->get_unicode());
		if (key->is_pressed() && ch >= 32 && ch != 127 && !(mods & RC_MOD_CTRL)) {
			const CharString utf8 = String::chr(ch).utf8();
			rc_editor_text(editor_, utf8.get_data(), size_t(utf8.length()));
		}
	} else {
		return;
	}
	after_input();
	sync();
	get_viewport()->set_input_as_handled();
}

void RicochetEditor::after_input() {
	rc_editor_status st{};
	st.size = sizeof st;
	if (rc_editor_get_status(editor_, &st) != RC_OK) {
		report("rc_editor_get_status");
		return;
	}
	if (st.open_requested) {
		open_requested(); // Stage 155: a level from the menu
	}
	if (st.save_requested) {
		const String target = save_target();
		if (!target.begins_with("user://") && !target.begins_with("res://")) {
			DirAccess::make_dir_recursive_absolute(target.get_base_dir()); // (a new level's folder)
		}
		const Ref<FileAccess> f = FileAccess::open(target, FileAccess::WRITE);
		if (f.is_valid()) {
			f->store_string(document());
			f->close();
			// Stage 170: a renamed level -- saved under its new name in the levels' folder, its old file removed
			const Dictionary es = state();
			const String from = es.get("renamed_from", String()), level = es.get("level", String());
			const String old = levels_dir().path_join(from + String(".json"));
			if (!from.is_empty() && target == levels_dir().path_join(level + String(".json")) && old != target && FileAccess::file_exists(old)) {
				DirAccess::remove_absolute(old);
				UtilityFunctions::print("RicochetEditor: removed ", old, " (renamed)");
			}
			// (an unnamed level saved in the levels' folder: named by its file)
			Dictionary saved;
			saved["op"] = "saved";
			if (level.is_empty() && target.begins_with(levels_dir() + String("/"))) {
				saved["name"] = target.substr(levels_dir().length() + 1).get_basename();
			}
			const CharString text = JSON::stringify(saved).utf8();
			run(text.get_data());
			UtilityFunctions::print("RicochetEditor: saved ", target);
		} else {
			run("{\"op\": \"save_cancelled\"}");
			UtilityFunctions::push_error("RicochetEditor: cannot write " + target);
		}
	}
	if (st.mode == RC_EDITOR_PLAY && game_ == nullptr) {
		start_play();
	} else if (st.mode == RC_EDITOR_EDIT && game_ != nullptr) {
		stop_play();
	}
}

void RicochetEditor::start_play() {
	game_ = memnew(RicochetGame);
	game_->set_name("PlayTest");
	game_->set_level_json(document());
	layers_->set_visible(false);
	add_child(game_);
}

void RicochetEditor::stop_play() {
	remove_child(game_);
	game_->queue_free(); // (its camera with it)
	game_ = nullptr;
	get_viewport()->set_canvas_transform(Transform2D()); // a game's camera leaves the canvas moved: the editor draws at identity
	layers_->set_visible(true);
	sync();
}

void RicochetEditor::sync() {
	rc_draw_list l{};
	l.size = sizeof l;
	if (rc_editor_draw_changes(editor_, &l) != RC_OK) {
		report("rc_editor_draw_changes");
		return;
	}
	apply_draw_list(layers_, by_key_, l, hud_);
}

String RicochetEditor::command(const String &json) {
	if (editor_ == nullptr) {
		return String();
	}
	const CharString text = json.utf8();
	const char *out = nullptr;
	size_t n = 0;
	if (rc_editor_command(editor_, text.get_data(), size_t(text.length()), &out, &n) != RC_OK) {
		report("rc_editor_command");
		return String();
	}
	const String result = String::utf8(out, int64_t(n));
	after_input();
	sync();
	return result;
}

String RicochetEditor::document() const {
	const char *out = nullptr;
	size_t n = 0;
	if (editor_ == nullptr || rc_editor_document(editor_, &out, &n) != RC_OK) {
		return String();
	}
	return String::utf8(out, int64_t(n));
}

Dictionary RicochetEditor::state() const {
	const char *take = "{\"op\": \"state\"}";
	const char *out = nullptr;
	size_t n = 0;
	if (editor_ == nullptr || rc_editor_command(editor_, take, std::strlen(take), &out, &n) != RC_OK) {
		return Dictionary();
	}
	return JSON::parse_string(String::utf8(out, int64_t(n)));
}

String RicochetEditor::save_target() const {
	if (!save_path_.is_empty()) {
		return save_path_;
	}
	// Stage 165: the editor's level name places it under the levels' folder (a new level, a copy)
	const String level = state().get("level", String());
	if (!level.is_empty()) {
		return levels_dir().path_join(level + String(".json"));
	}
	// a level under res://data is a copy (make native) of the repository's data: save the original
	const String root = ProjectSettings::get_singleton()->globalize_path("res://").path_join("../../data").simplify_path();
	const String rel = level_path_.is_empty() ? String("levels/untitled.json")
			: level_path_.begins_with("res://data/") ? level_path_.substr(11)
													  : String();
	if (!rel.is_empty() && DirAccess::dir_exists_absolute(root.path_join(rel).get_base_dir())) {
		return root.path_join(rel);
	}
	return level_path_.is_empty() ? String("user://untitled.json") : level_path_;
}

void register_editor() { GDREGISTER_CLASS(RicochetEditor); }

} // namespace rc_godot
