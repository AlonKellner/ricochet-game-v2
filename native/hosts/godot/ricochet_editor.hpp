// The editor program (docs/EDITOR.md, docs/TDD_49): the level editor in Godot,
// on the C API alone (rc_api.h's rc_editor_*). RicochetEditor forwards Godot's
// pointer, keys and typed text to the native editor, paints its draw list (in
// the canvas's pixels, no camera) through the game's layers, writes the
// document when a save is requested, and plays it with a RicochetGame child
// while the editor is in play mode.
#pragma once

#include "rc_api.h"
#include "ricochet_game.hpp"

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/variant/string.hpp>

#include <map>
#include <string>

namespace rc_godot {

class RicochetEditor : public godot::Node2D {
	GDCLASS(RicochetEditor, godot::Node2D)

public:
	RicochetEditor();
	~RicochetEditor() override;

	// the level edited (res://data/levels/...; empty: a new level); the command
	// line's user argument --level=<path> overrides it
	void set_level_path(const godot::String &p) { level_path_ = p; }
	godot::String get_level_path() const { return level_path_; }
	// where a save writes (empty: the repository's data/levels file the level
	// path's res://data copy came from, else the level path)
	void set_save_path(const godot::String &p) { save_path_ = p; }
	godot::String get_save_path() const { return save_path_; }

	void _ready() override;
	void _process(double delta) override;
	void _input(const godot::Ref<godot::InputEvent> &event) override;

	// for tools and tests: a command (JSON in, JSON out), the document, the layers drawn, play mode, the save target
	godot::String command(const godot::String &json);
	godot::String document() const;
	int layer_count() const { return int(by_key_.size()); }
	bool is_playing() const { return game_ != nullptr; }
	godot::String save_target() const;

protected:
	static void _bind_methods();

private:
	void run(const char *json); // a command whose result is not needed
	void after_input(); // a save requested, play mode entered or left
	void sync();
	void start_play();
	void stop_play();
	// Stage 155: the level menu -- the levels' folder (the repository's data when there, else res://data),
	// the levels listed for the editor, a requested one opened
	godot::String levels_dir() const;
	void list_levels();
	void open_requested();

	godot::String level_path_, save_path_;
	rc_editor *editor_ = nullptr;
	godot::Node2D *layers_ = nullptr;
	godot::Label *hud_ = nullptr; // (the editor draws no HUD label)
	std::map<std::string, RicochetLayer *> by_key_;
	RicochetGame *game_ = nullptr;
	godot::Vector2 size_;
};

void register_editor();

} // namespace rc_godot
