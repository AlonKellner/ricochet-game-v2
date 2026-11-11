// Stage 208: the visual style -- every colour, width, size and alpha the game
// draws with (GAME_SPEC 22), one table the game's painter and the editor share.
// Grouped by what a channel means: what the arrow does, what the player does,
// flags, targets and spawns, journeys, the preview, light and shadow, the
// preview's consequences. Units are the world's (1 unit = 1 pixel).
#pragma once

#include "rcg/render.hpp"

namespace rcg::style {

using render::Color;

inline Color rgba(double r, double g, double b, double a = 1.0) { return {float(r), float(g), float(b), float(a)}; }

// ---------------------------------------------------------------- sizes
constexpr double LINE_WIDTH = 3.0;
constexpr double SIDE_OFFSET = 2.0;
constexpr double HOVER_WIDTH = 24.0; // ClickDetector.CLICK_TOLERANCE
constexpr int GRADIENT_STRIPS = 6;
constexpr double CHEVRON_SIZE = 8.0;
constexpr double INNER_CHEVRON_DIST = 2.0;
constexpr double OUTER_CHEVRON_DIST = 18.0;
constexpr double MARKER_SIZE = 5.0;    // a hit effect's mark
constexpr double MARKER_SPACING = 14.0;
constexpr double CROSSING_DOT = 2.5;
constexpr double DASH = 8.0;
constexpr double BAND_WIDTH = 10.0;    // Stage 186: a shield's band; Stage 207: the steel band
constexpr double DOOR_POST = 8.0, DOOR_POST_WIDTH = 3.0, DOOR_CORE_WIDTH = 1.0;
constexpr double DESIGNER_MARKER_SIZE = 7.0; // Stage 179: a designer's marker's radius
constexpr double ABSENT_MARKER_ALPHA = 0.4;  // ... on an absent surface
constexpr double SHAPE_HITBOX_ALPHA = 0.3;   // Stage 181: a shaped object's circle under its outline

// ---------------------------------------------------------------- the base colours
inline const Color WHITE{1, 1, 1, 1};
inline const Color GRAY = render::hex(0xBEBEBEFF);
inline const Color BLUE = render::hex(0x0000FFFF);
inline const Color PURPLE = render::hex(0xA020F0FF);
inline const Color RED = render::hex(0xFF0000FF);
inline const Color CYAN = render::hex(0x00FFFFFF);
inline const Color MAGENTA = render::hex(0xFF00FFFF);
inline const Color GREEN = render::hex(0x00FF00FF);
inline const Color YELLOW = render::hex(0xFFFF00FF);

// ---------------------------------------------------------------- what the arrow does (a side's colour)
// pass gray, terminal red, reflection blue, inversion purple, portal cyan, and:
inline const Color PROJ_FWD = rgba(1.0, 0.8, 0.2);  // a projection through
inline const Color PROJ_BACK = rgba(1.0, 0.4, 0.1); // ... and back
inline const Color PRE_TRANSFORM_COLOR = rgba(0.55, 0.45, 0.85, 0.35);
inline const Color ABSENT_COLOR = rgba(0.6, 0.6, 0.6, 0.35);
inline const Color CROSSING_COLOR = rgba(1.0, 1.0, 1.0, 0.6);

// ---------------------------------------------------------------- what the player and objects do
inline const Color PLATFORM_COLOR = rgba(0.62, 0.42, 0.22);  // Stage 172: wood brown
inline const Color SHIELD_COLOR = rgba(0.45, 1.0, 0.85);     // Stage 186: a shield's core (turquoise: a portal is pure cyan)
inline const Color SHIELD_BAND = rgba(0.45, 1.0, 0.85, 0.22); // ... and the band around it
inline const Color STEEL_BAND = rgba(0.6, 0.72, 0.85, 0.3);   // Stage 207: blocking, not stopping
inline const Color COLLIDER = rgba(0.55, 0.62, 0.72, 0.9);    // Stage 164: a player-only collider
inline const Color BODY_COLOR = rgba(0.8, 0.8, 0.8);          // Stage 173: the tall player's body under its triangle
inline const Color CROSSING_BODY = rgba(0.95, 0.6, 0.3);      // Stage 166: an object crossing a portal

// ---------------------------------------------------------------- flags
inline const Color FLAG_TOGGLER_COLOR = rgba(1.0, 0.85, 0.2); // a side that writes a flag
inline const Color GHOST_TOGGLER_COLOR = rgba(0.75, 0.95, 1.0);
inline const Color DOOR_COLOR = rgba(1.0, 0.72, 0.25);        // Stage 190: a door's posts and core (amber)
inline const Color DETECTOR_COLOR = rgba(1.0, 0.8, 0.25, 0.8); // Stage 145
inline const Color DETECTOR_FILL = rgba(1.0, 0.8, 0.25, 0.18);
inline const Color DEFERRED_DOT = rgba(0.1, 0.1, 0.1);        // a mark whose writes wait for the landing

// ---------------------------------------------------------------- targets, spawns, the goal
inline const Color TARGET_COLOR = rgba(1.0, 0.85, 0.3);
inline const Color SPAWN_COLOR = rgba(0.6, 0.95, 1.0);
inline const Color RESET_COLOR = rgba(1.0, 0.55, 0.85);
inline const Color GHOST_SPAWN_COLOR = rgba(0.6, 0.95, 1.0, 0.8);
inline const Color VANISH_COLOR = rgba(1.0, 0.35, 0.3, 0.7);
inline const Color GOAL_COLOR = rgba(0.3, 0.95, 0.45); // Stage 145
inline const Color GOAL_WON_FILL = rgba(0.3, 0.95, 0.45, 0.25);

// ---------------------------------------------------------------- journeys (a grapple's, a pull's)
inline const Color JOURNEY = rgba(0.6, 1.0, 0.7);                  // the journey's mint
inline const Color JOURNEY_TRAIL = render::with_alpha(JOURNEY, 0.6);       // Stage 166: the trip's path
inline const Color JOURNEY_END = render::with_alpha(JOURNEY, 0.95);        // Stage 181: the traveller where it ends
inline const Color JOURNEY_BLOCKED = rgba(1.0, 0.6, 0.2, 0.95);   // ... stopped short

// ---------------------------------------------------------------- the preview
// Stage 130 (the user, 2026-10-22): a solid preview step is a slightly dark stroke with a slightly bright dashed
// stroke over it
inline const Color GREEN_DARK = render::hex(0x00B300FF);
inline const Color GREEN_BRIGHT = render::hex(0x80FF80FF);
inline const Color RED_DARK = render::hex(0xB30000FF);
inline const Color RED_BRIGHT = render::hex(0xFF8080FF);
inline const Color HOVER_COLOR = rgba(1.0, 1.0, 1.0, 0.3);
inline const Color PLAN_VALID_COLOR = rgba(1.0, 1.0, 1.0, 0.15);
inline const Color PLAN_INVALID_COLOR = rgba(1.0, 0.0, 0.0, 0.15);
inline const Color POINT_COLOR = rgba(0.75, 0.85, 1.0, 0.9);
inline const Color FILL = rgba(1.0, 1.0, 1.0, 0.10);                    // the visibility region
inline const Color FILL_BEFORE_TERMINAL = rgba(0.55, 1.0, 0.6, 0.14); // Stage 197: the region of a plan without its last terminal
// Stage 156: the arrow's states along its path, and as glows on what it would light
inline const Color MARK_FIRE = rgba(1.0, 0.3, 0.15);
inline const Color MARK_CHARGE = rgba(1.0, 1.0, 1.0);
inline const Color MARK_FROST = rgba(0.6, 0.85, 1.0);
inline const Color GLOW_FROSTED = rgba(0.6, 0.85, 1.0, 0.35);
inline const Color GLOW_FIRE = rgba(1.0, 0.45, 0.1, 0.55);
inline const Color GLOW_CHARGE = rgba(0.85, 0.95, 1.0, 0.5);
inline const Color BECOMES_FIRE = rgba(1.0, 0.45, 0.1);   // (its alpha: how sure)
inline const Color BECOMES_CHARGE = rgba(0.85, 0.95, 1.0);
inline const Color BECOMES_PLAIN = rgba(0.5, 0.5, 0.55);
// Stage 157: the consequences -- what a shot would change, dashed
inline const Color CONSEQUENCE_VANISH = rgba(1.0, 0.35, 0.35, 0.95);
inline const Color CONSEQUENCE_APPEAR = rgba(0.4, 1.0, 0.5, 0.95);   // (and the goal that opens)
inline const Color CONSEQUENCE_CHANGE = rgba(1.0, 0.85, 0.3, 0.95);  // (and a bubble's released load)
inline const Color CONSEQUENCE_TARGET = rgba(0.3, 0.9, 1.0, 0.95);
inline const Color CONSEQUENCE_DARK_ON = rgba(0.65, 0.55, 1.0, 0.95);
inline const Color CONSEQUENCE_LIGHT_ON = rgba(1.0, 0.9, 0.4, 0.95);
inline const Color CONSEQUENCE_SHADE_OFF = rgba(0.5, 0.5, 0.55, 0.95);

// ---------------------------------------------------------------- light and shadow (Stage 153)
inline const Color SHADOW_FILL = rgba(0.02, 0.02, 0.05, 0.82);
inline const Color SHADE_DARK = rgba(0.55, 0.45, 0.95);
inline const Color SHADE_LIGHT = rgba(1.0, 0.85, 0.35);

} // namespace rcg::style
