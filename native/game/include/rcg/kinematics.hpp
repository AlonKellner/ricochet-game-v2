// Stage 117: the player's movement natively (GAME_SPEC 25; it replaces
// Godot's CharacterBody2D.move_and_slide). A circle of radius 12 moved at a
// fixed physics step against the player-solid surfaces that are present
// (exact segments and arcs) and the level's colliders: swept exactly (no
// tunnelling), sliding along what it meets (at most four contacts a step),
// pushed out of any overlap, resting a skin away from what it touches.
//
// Top-down (no gravity): the input direction, normalized, times SPEED.
// Gravity: the horizontal input times SPEED; gravity integrated in the air;
// a jump (the up action just pressed) from a floor -- a contact whose normal
// is within 45 degrees of up (0, -1). Godot's numbers: SPEED 200, JUMP 400,
// a 1 px floor snap, a 0.08 px skin (the engine's safe margin).
//
// Deterministic: float64 inside a step, the position and velocity kept as the
// game's float32 vectors between steps.
#pragma once

#include "rc/trace_types.hpp"
#include "rcg/build.hpp"
#include "rcg/level.hpp"

#include <vector>

namespace rcg::kinematics {

constexpr double RADIUS = 12.0;
constexpr double SKIN = 0.08;
constexpr double SPEED = 200.0;
constexpr double JUMP_VELOCITY = 400.0;
constexpr double FLOOR_SNAP = 1.0;
constexpr int MAX_SLIDES = 4;
constexpr double FLOOR_MIN_DOT = 0.7071067811865476; // cos 45 degrees
constexpr double STEP = 1.0 / 60.0;                  // the physics tick

// What the player collides with: a segment, or an arc (a full circle, or the
// part of its carrier from start through via to end).
struct Collider {
	bool arc = false;
	double ax = 0, ay = 0, bx = 0, by = 0; // a segment's ends; an arc's ends
	double cx = 0, cy = 0, r = 0;          // an arc's circle
	bool full = false;
	rc::arc::SegGeom seg;                  // an arc's segment (its angular range)
};

// The present player-solid surfaces (absent ones do not block: TDD_22) and the
// level's colliders.
std::vector<Collider> colliders_of(const std::vector<BuiltSurface> &world, const rc::Flags &flags, const Level &level);

// A collider from a surface's segment (a line, an arc, a full circle) or two points.
Collider collider_of(const rc::arc::SegGeom &seg);
Collider segment_collider(const V2 &a, const V2 &b);

struct Body {
	V2 pos;
	V2 vel;
	bool on_floor = false;
};

struct Input {
	float axis_x = 0.0f; // right - left
	float axis_y = 0.0f; // down - up
	bool jump = false;   // the up action just pressed (gravity: a jump)
};

// One physics tick (player.gd's _physics_process: the velocity, then the move).
void step(Body &body, const Input &in, const V2 &gravity, const std::vector<Collider> &colliders);

// The distance from a point to a collider and the unit normal from it toward the point.
double distance(const Collider &c, double px, double py, double &nx, double &ny);

} // namespace rcg::kinematics
