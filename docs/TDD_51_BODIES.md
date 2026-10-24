# TDD 51: Objects, the pull and the grapple (Stage 146)

The editor program's fifth stage, the second part of the first wave of new mechanics: **pullable objects**
(circle bodies that fall and rest), the **pull** (hit an object and it comes back along the arrow's path), and
the **grapple** (hit a grappling surface and the player travels the path to it). Fences stop both. The rules are
GAME_SPEC 10.6 and 25.5-25.6. Nothing reaches the solver: an object is a surface the world moves, and a journey
is a landing effect that walks the finished path.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 146.1 | The controller at any radius (`step_body`; `step` is it with the player's): bit-exact, every walk session unchanged | Done |
| 146.2 | `objects` in the format; the world's bodies (surfaces after the level's, moved by rebuilding, in the snapshot) | Done |
| 146.3 | Bodies under gravity per tick; no shot while one moves; detectors `for` objects | Done |
| 146.4 | The hit kinds `grapple` and `pull` (landing-time; their sides a candidate, Stage 134's rule) and the journeys | Done |
| 146.5 | The `grapple` element; the editor's object tool; two demo levels and sessions; tests; docs; `make gate` | Done |
| 146.6 | The user's interactive sign-off (play both demo levels; place objects in the editor) | Open |

## Decisions

- **An object is a surface the world moves.** Its full circle is built like any surface, with the ids after the
  level's static ones (before any spawn's), so it is planned, hit and drawn with no new path. A move rebuilds
  its surface and bumps the world's version, so the preview, the pieces and the defs follow.
- **The player's controller, at any radius.** The clearance (`RADIUS + SKIN`) became a parameter of the sweep and
  the step. `step` passes the player's, so its arithmetic is the same, and every walk session replays byte for
  byte. Bodies use `step_body` with their own radius, no input, and the level's gravity.
- **No body meets another (v1)**, but a body meets the player, as a circle collider, so a falling box rests on
  the player rather than passing through.
- **Nothing is fired while a body moves**, and the game is paused during a journey (`flying()` covers it), so a
  shot never sees a world in motion, and a journey belongs to its shot (the replays wait for it).
- **A journey walks the physical path** (the trace's steps up to the hit, sampled every 4 px, arcs along their
  arcs). It moves at 900 px/s and is swept with the controller's sweep at the traveller's radius. It stops at the
  first contact, and only while approaching, so a player standing on a floor can leave it. A portal is a jump. A
  step through infinity ends the path. A pulled object keeps its center's offset from the hit point, so it comes
  back whole rather than centred on the path.
- **Detectors see objects by their centers, in regions.** A segment detector is for the player alone (v1).
- **The kinematics random test's chord check** between two tick positions now allows the sagitta of a slide
  around a convex corner, L²/8r for a tick of length L. The true path keeps outside the corner, and its chord cuts
  in by at most that. The new `grapple_gap` level's platform ends gave a chord 0.0019 px within the radius, and no
  tick position ever penetrated. A tunnel would leave no clearance at all, so the check keeps its purpose.

## Tests

- `rcg_tests` "bodies: ...", "pull: ...", "pull and grapple: ...", "objects: ..." (6): a box falls and rests, a
  post stays, no shot while the box falls, reset restores; a pull brings the box back to touch the player (the
  game waits for it), undo restores it; a fence stops a pull and a grapple, a pass-through does not; a detector
  `for` objects sees a pulled box pass; both demo levels played to their goals; the schema.
- "editor: the object tool (Stage 146)".
- The session golden gains `objectives__pull_box__play` and `objectives__grapple_gap__play`.
