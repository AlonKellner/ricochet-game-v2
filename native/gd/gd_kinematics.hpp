// Stage 117: the player's movement natively (rcg/kinematics.hpp) for the
// GDScript player (player.gd): one physics tick against the present
// player-solid surfaces of the scene defs and the level's colliders.
#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

class RcgKinematics : public godot::RefCounted {
	GDCLASS(RcgKinematics, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// body: {pos, vel, on_floor}; input: {axis_x, axis_y, jump}; defs: SceneDefs
	// (a surface collides when player_solid and present under `flags`);
	// colliders: segment ends in pairs. -> the body after the tick.
	static godot::Dictionary step(const godot::Dictionary &body, const godot::Dictionary &input, const godot::Vector2 &gravity,
			const godot::Array &defs, const godot::Dictionary &flags, const godot::PackedVector2Array &colliders);
	static double radius();
	static double speed();
	static double jump_velocity();
};
