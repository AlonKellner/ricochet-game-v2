# TDD 80: Openings (Stage 202)

Your request: "a new surface type ... similar to a door, but [with] a closed position (start+via+end) and an
opening rigid motion transformation (a sequence of rotations and translations). When the flag fires, the surface
should rigidly animate the opening transformation from the closed position to the open position" -- doors that
swing about an axis when a button or a detector fires, platforms a button sends across. The rule is GAME_SPEC 9.12.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 202.1 | The spec (GAME_SPEC 9.12, 2.98.0): an element's `opening`, the clock, the shot as fired, no shot mid-swing, nothing rides, the preview | Done |
| 202.2 | The format: `opening` on any element (`Opening`, `opening_pose`), its schema; not with `motion`, not on a shade or an edge | Done |
| 202.3 | The session: the openings' surfaces are `World::Moving` after the motions'; per tick each time goes on while its flag holds and back while not; the readiness falls while any swings; the checkpoint and the record (`openings`) | Done |
| 202.4 | The look: an opening's surface has the door's (amber core and posts; a platform keeps its own); a consequence that switches its flag draws the pose it goes to, dashed amber | Done |
| 202.5 | The editor: the `opening` field, the open pose drawn faint, a body move or a duplicate carries its pivots, a scale its pivots and translations | Done |
| 202.6 | The demo level `objectives/swing_gate` (a plate slides a shield away, a button swings the gate open), its session in the replay golden; tests; docs; `make gate` | Done |
| 202.7 | The user's interactive sign-off | Open |

## Decisions (the user's, 2026-11-06)

- **The shot sees the world as fired.** A hit that writes an opening's flag does not move the element mid-shot:
  the solver works on a fixed scene plus presence, and the arrow meets the element where it stood. (A door, by
  presence, vanishes for the arrow that opens it; that stays as it is.)
- **The swing plays after the shot lands.** Time is frozen in flight (GAME_SPEC 21.1); the openings' clock is the
  physics tick's, like the cycle's.
- **No shot mid-swing.** The readiness falls while any opening is between where it was and where its flag sends
  it, as when the player moves (GAME_SPEC 21.0), and `can_fire` also waits for every opening to settle (a shot that
  just landed has not ticked yet).
- **Nothing rides.** The element's colliders move; the kinematics push the player and objects out of an overlap as
  for any moving surface. Carrying is a later stage (it would apply to the cycle's motions too).
- **Doors stay as they are** (asked mid-stage): a door is a presence template, and its mid-shot vanishing is what
  makes the button open the way for the same arrow; an opening cannot do that without the solver seeing two poses.

## Design notes (mine)

- The time is in seconds, a tick at a time, snapped to the ends within 1e-9 s (a sum of ticks is a hair off), so a
  settled opening is exactly closed (the identity: the element bit for bit as written) or exactly open.
- A rotation's pivot is a point of the element in its closed pose, carried by the earlier steps; a translation is in
  the level's axes. The default pivot is the first surface's start (a circle: its centre), the hinge of a door.
- A flag that switches mid-swing turns the swing where it stands; a level starts settled under its starting flags.
- The amber look is display only (`SurfaceSpec::swings`, copied by `build_spec` and `mapped`).

## Tests

`rcg_tests` "opening: ..." (6): a swing about the hinge, the shot as fired, no shot mid-swing, back exactly closed;
turning mid-swing, undo, redo and reset, a detector's expression; the steps' order and a carried pivot, a level that
starts open, the record; the demo level; the preview's amber destination; the schema. "editor: an opening element
shows its open pose faint ..." The session golden gains `objectives__swing_gate__play` (replayed natively, through
the C API, from Python and in Godot).
