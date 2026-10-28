// Stage 117: the player's movement natively (GAME_SPEC 25; it replaces
// Godot's CharacterBody2D.move_and_slide). A circle of radius 12 moved at a
// fixed physics step against the player-solid surfaces that are present
// (exact segments and arcs) and the level's colliders: swept exactly (no
// tunnelling), sliding along what it meets (at most four contacts a step),
// pushed out of any overlap, resting a skin away from what it touches.
// Stage 173 (the user): the player is taller -- three such circles stacked
// from its head (the arrow origin) down to its feet (PLAYER); an object stays
// one circle.
//
// Top-down (no gravity): the input direction, normalized, times SPEED.
// Gravity: the horizontal input times SPEED; gravity integrated in the air;
// a jump (the up action just pressed) from a floor -- a contact whose normal
// is within 45 degrees of up (0, -1). Godot's numbers: SPEED 200, JUMP 400,
// a 1 px floor snap, a 0.08 px skin (the engine's safe margin).
//
// Stage 172, platforms (GAME_SPEC 25.1): one-way colliders. A platform takes
// part in a step only while the body's centre is above it at the step's start
// and down is not held; then only its floor-like contacts count (it is met from
// above, never from below or the side), and an overlap pushes the body up onto
// its top -- not while rising: a jump through it carries on and lands on it.
//
// Stage 176, climbing (the user): up held, under gravity, while the body touches
// a platform along its length that does not hold it (one it is below, or one
// steeper than 45 degrees: a ladder, a rope) rises at least CLIMB_SPEED -- up
// through a platform, up a ladder; past its top it lands on what is there. Its
// hands reach REACH above its head: standing, it grabs a platform just over it.
// Stage 183 (the user): climbing kills the horizontal momentum, snappily -- the
// horizontal speed goes to the input's times CLIMB_SIDE_SPEED (a slow shuffle)
// at CLIMB_GRIP: from full speed in 3 ticks.
//
// Stage 182, the player's feel (the user: platformer practice, restrictive -- the
// arrow, the pull and the grapple should move it better than walking): under
// gravity it accelerates to SPEED (slowly in the air) instead of at once; a jump
// is taken up to COYOTE_TICKS after leaving a floor, and a press waits up to
// JUMP_BUFFER_TICKS for one; releasing up while rising cuts the jump (JUMP_CUT);
// held at the apex it hangs (APEX_GRAVITY), and it falls faster (FALL_GRAVITY)
// up to MAX_FALL. A full held jump keeps the old reach: 85 px up, 167 px across
// at full speed on the level, in 50 ticks. Objects keep the plain motion.
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
constexpr double BODY = 24.0;                        // Stage 173: the player's feet below its head (the arrow origin)
constexpr double CLIMB_SPEED = 150.0;                // Stage 176: the least upward speed while climbing a platform
constexpr double REACH = RADIUS;                      // Stage 176: the hands above the head (a climb's reach)
constexpr double CLIMB_SIDE_SPEED = 60.0;            // Stage 183: the horizontal speed while climbing (the input's; no momentum)
constexpr double CLIMB_GRIP = 4000.0;                // ... reached at this rate, px/s^2 (from full speed in 3 ticks)
// Stage 182: the player's feel (see above)
constexpr double FEEL_JUMP_VELOCITY = 395.0; // (the apex hang and the faster fall: the old arc's height and reach)
constexpr double GROUND_ACCEL = 1800.0;      // px/s^2 toward the input's speed on a floor (full speed in ~0.11 s)
constexpr double GROUND_DECEL = 2400.0;      // ... to a stop, or turning
constexpr double AIR_ACCEL = 900.0;          // ... in the air
constexpr double AIR_DECEL = 300.0;
constexpr double FALL_GRAVITY = 1.6;         // gravity's factor falling
constexpr double APEX_GRAVITY = 0.5;         // ... near the apex of a jump, up held
constexpr double APEX_SPEED = 80.0;          // near the apex: slower than this
constexpr double MAX_FALL = 900.0;           // the fastest fall
constexpr double JUMP_CUT = 0.4;             // the rising speed kept when up is released
constexpr int COYOTE_TICKS = 5;              // a jump still taken this many ticks off a floor
constexpr int JUMP_BUFFER_TICKS = 6;         // a press waits this many ticks for a floor
constexpr int LONG_AIR = 1 << 20;            // (ticks off a floor: never stood, or a jump spent the coyote time)

// Stage 173: a body's shape -- circles of one radius stacked straight down from
// its position. An object is one circle; the player three, head to feet, spaced
// a radius apart (their union a capsule but for notches no floor can rest in),
// its position the head's centre: the arrow's origin.
struct Shape {
	double radius = RADIUS;
	int n = 1;
	double drop[3] = {0.0, 0.0, 0.0}; // each circle's centre below the position
	double feet() const { return drop[n - 1]; }
};
constexpr Shape PLAYER{RADIUS, 3, {0.0, BODY / 2, BODY}};

// What the player collides with: a segment, or an arc (a full circle, or the
// part of its carrier from start through via to end).
struct Collider {
	bool arc = false;
	double ax = 0, ay = 0, bx = 0, by = 0; // a segment's ends; an arc's ends
	double cx = 0, cy = 0, r = 0;          // an arc's circle
	bool full = false;
	rc::arc::SegGeom seg;                  // an arc's segment (its angular range)
	bool platform = false;                 // Stage 172: one-way (see step_body)
};

// The present player-solid surfaces and platforms (absent ones do not block:
// TDD_22) and the level's colliders; but the surfaces of `except` (Stage 173:
// the objects' -- the player passes them).
std::vector<Collider> colliders_of(const std::vector<BuiltSurface> &world, const rc::Flags &flags, const Level &level,
		const std::vector<int> &except = {});

// A collider from a surface's segment (a line, an arc, a full circle) or two points.
Collider collider_of(const rc::arc::SegGeom &seg);
Collider segment_collider(const V2 &a, const V2 &b);

struct Body {
	V2 pos;
	V2 vel;
	bool on_floor = false;
	// Stage 182: the feel's state -- ticks since a floor, ticks a pressed jump still waits, rising from a jump
	int air = LONG_AIR;
	int buffer = 0;
	bool jumped = false;
};

struct Input {
	float axis_x = 0.0f; // right - left
	float axis_y = 0.0f; // down - up
	bool jump = false;   // the up action just pressed (gravity: a jump)
};

// One physics tick of the player (player.gd's _physics_process: the velocity, then the move; Stage 173: PLAYER;
// Stage 182: with the player's feel).
void step(Body &body, const Input &in, const V2 &gravity, const std::vector<Collider> &colliders);
// Stage 146: the same for a circle of any radius; Stage 173: for any shape; Stage 182: `feel` the player's
// (else the plain motion: the input's speed at once, a jump from a floor, gravity as it is).
void step_body(Body &body, const Input &in, const V2 &gravity, const std::vector<Collider> &colliders, double radius);
void step_body(Body &body, const Input &in, const V2 &gravity, const std::vector<Collider> &colliders, const Shape &shape, bool feel = false);
// Stage 172: a body whose centre is above a platform it overlaps moved up onto
// its top (on the floor); one whose centre is below it left to fall through.
void settle(Body &body, const std::vector<Collider> &colliders, const Shape &shape);
// Stage 146: how far (0..1) a circle of that radius moves along (dx, dy) before
// it first touches a collider (1: freely); what it touches only while approaching.
double sweep_fraction(double px, double py, double dx, double dy, const std::vector<Collider> &colliders, const Shape &shape);
// Stage 146: a full circle (a body, the player, as others collide with them).
Collider circle_collider(const V2 &center, double r);

// The distance from a point to a collider and the unit normal from it toward the point.
double distance(const Collider &c, double px, double py, double &nx, double &ny);
// Stage 173: the same from a shape's nearest circle centre (a shape at (px, py)).
double distance(const Collider &c, const Shape &shape, double px, double py, double &nx, double &ny);

} // namespace rcg::kinematics
