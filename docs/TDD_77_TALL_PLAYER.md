# TDD 77: The tall player (Stage 173)

Your request (2026-10-27): "Please make the player taller, it could be a simple rectangular shape added below the
existing rotating triangle. This way pulled objects would usually end up above the ground and then fall slightly
after being pulled. This is important since when standing on a platform aiming generally down and pulling objects
almost always does not bring them on to the platform ... And while we're at it, let's make the player go through
objects ... Pullable objects should be able to occupy the same space as the player position, so when pull up through
a platform, they end up directly over the player's 'feet'." The spec is GAME_SPEC 25.1 (with 19.1, 22, 24.5, 24.6,
25.5, 25.6).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 173.1 | The kinematics: `kinematics::Shape` (circles of one radius stacked below a position), `PLAYER` (head, middle, feet: 0, 12, 24); `step_body`, `sweep_fraction`, `settle` and the shape's `distance` take a shape; platforms judge the feet; `rcg_tests` "tall player: ..." | Done |
| 173.2 | The session: tall only under gravity (`tall()`, `player_shape()`); the spawn by the feet under gravity (`spawn_point`); detectors and the goal see the feet; the gravity toggle keeps the feet; the player and objects pass each other (the player's colliders skip the objects', the objects' skip the player); a pull ends at the arrow's origin (the preview's `blocked` no longer allows for the player) | Done |
| 173.3 | The draw: the rectangle under the triangle (in play, and cut at a portal while crossing); the editor's start marker under gravity (the body's outline, the head marked) | Done |
| 173.4 | Docs; the session goldens (the judge's report: 32 of 59 sessions, every win kept; approved 2026-10-27, re-recorded after merging Stage 174); `make gates` | Done |
| 173.5 | Your interactive sign-off | Open |

## Decisions

- **The position stays the arrow's origin.** The preview, visibility, firing, the facing, the light and the record
  all read the player's position; only the physics, detectors and the goal need the feet. So the head is the
  position and the body hangs below it.
- **Stacked circles, not a capsule.** Three circles of radius 12 spaced a radius apart reuse the exact sweeps
  (segments, arcs, end caps) unchanged; their union is a capsule but for 1.6-unit notches at its sides, where no
  floor can rest (a corner contact on an upper circle within 45 degrees of up would lie inside the circle below).
- **Tall only under gravity.** In a top-down level there are no feet: the start, the aim, detectors and the goal
  stay as they were, so every top-down session is unchanged but one preview trail (pull_box's, now reaching the
  player). Toggling gravity keeps the feet where they are: the player stands up from them, or folds back down.
- **The start is the feet under gravity.** Every gravity level's start was where the player's circle stood; it is
  now where its feet stand, so no level spawns the new body into its floor, and every plate and goal placed for a
  standing player still sees it.
- **The height.** The head 36 units above what the player stands on: an object up to 36 in radius (the levels' are
  16 to 25) pulled to a standing player ends clear above the floor or platform and falls onto it.
- **The player and objects pass each other.** Not the objects and other surfaces: a pulled object still stops at
  a fence, a falling one still rests on a floor.

## Tests

`rcg_tests` "tall player: it stands on its feet, its head stops at a low ceiling and under a passage too low for
it", "... platforms judge by its feet", "... standing on a platform, a pull from below brings the object up onto
it" (radii 16, 20, 25), "... the player and objects pass each other"; the random walk with the player's shape under
gravity; the platform and pull tests moved to the new heights.
