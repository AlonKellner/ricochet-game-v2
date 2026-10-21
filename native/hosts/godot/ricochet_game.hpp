// Stage 118: the Godot adapter -- the game as one node, on the C API alone
// (rc_api.h; this directory includes nothing else of the native code: ctest
// godot_adapter_includes). RicochetGame reads its level file, maps Godot's
// input onto the game's events, advances the game every frame, follows its
// camera and paints its draw list: one child layer per draw node, in draw
// order, whose _draw makes the node's CanvasItem calls (the same calls the
// GDScript nodes made).
#pragma once

#include "rc_api.h"

#include <godot_cpp/classes/camera2d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/variant/string.hpp>

#include <map>
#include <string>
#include <vector>

namespace rc_godot {

// One draw node's commands, owned (the draw list is borrowed).
struct LayerCmd {
	rc_draw_cmd cmd{};
	godot::PackedVector2Array points;
	godot::PackedColorArray colors;
	godot::PackedInt32Array indices;
	godot::String text;
};

class RicochetLayer : public godot::Node2D {
	GDCLASS(RicochetLayer, godot::Node2D)

public:
	// the node's commands; a change queues a redraw (blob: their bytes)
	void set_cmds(std::vector<LayerCmd> cmds, std::string blob);
	void _draw() override;

protected:
	static void _bind_methods() {}

private:
	std::vector<LayerCmd> cmds_;
	std::string blob_;
};

class RicochetGame : public godot::Node2D {
	GDCLASS(RicochetGame, godot::Node2D)

public:
	RicochetGame();
	~RicochetGame() override;

	void set_level_path(const godot::String &p) { level_path_ = p; }
	godot::String get_level_path() const { return level_path_; }
	void set_infinity(int v) { infinity_ = v; }
	int get_infinity() const { return infinity_; }
	void set_visibility(int v) { visibility_ = v; }
	int get_visibility() const { return visibility_; }

	void _ready() override;
	void _process(double delta) override;
	void _input(const godot::Ref<godot::InputEvent> &event) override;

	// for tools and tests: a fixed cursor (the mouse no longer moves it), the
	// player moved, the physics off (the player stays put; frames still settle, a flight flies)
	void aim(const godot::Vector2 &at);
	void teleport(const godot::Vector2 &at);
	void set_physics(bool on) { physics_ = on; }
	// Stage 121: the region and the preview's exact climbs in the background (the
	// default); off, everything in the frame (the session replay test)
	void set_asynchronous(bool on) { asynchronous_ = on; }
	bool get_asynchronous() const { return asynchronous_; }
	// Stage 122: the draw list animated (the default): the player between physics
	// ticks, the region morphing; off, the game's state as it is
	void set_animate(bool on) { animate_ = on; }
	bool get_animate() const { return animate_; }
	bool get_physics() const { return physics_; }
	// the game's session record (docs/research/session_digest.md), the HUD
	godot::String record() const;
	godot::String hud() const;
	int layer_count() const;
	bool is_flying() const;

protected:
	static void _bind_methods();

private:
	void event(int32_t e);
	void sync();
	godot::Vector2 cursor() const;

	godot::String level_path_;
	int infinity_ = -1;
	int visibility_ = -1;
	bool physics_ = true;
	bool asynchronous_ = true;
	bool animate_ = true;
	bool aimed_ = false;
	godot::Vector2 aim_;
	rc_game *game_ = nullptr;
	godot::Node2D *layers_ = nullptr;
	godot::Camera2D *camera_ = nullptr;
	godot::Label *hud_ = nullptr;
	std::map<std::string, RicochetLayer *> by_key_;
};

// registers the adapter's classes (the extension's initializer calls it)
void register_adapter();

} // namespace rc_godot
