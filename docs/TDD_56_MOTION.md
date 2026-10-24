# TDD 56: Moving surfaces (Stage 151)

The second of the next primitives sketched in TDD_52. Your description: "According to a schedule or cycle,
changes along a trajectory with different potential orientations, positions and states. The player then may
scroll to move the cycle to the perfect moment for them to make their shot. By default the cycle moves on its
own, when the player uses the scroll the auto cycle stops and follows the player guidance, and after a shot
resumes at normal auto pace. If the cycle is paused, and the last time the player scrolled, or changed the planned
surfaces was more than 10 seconds ago (without shooting), the cycle may resume at normal auto pace." The rule is
in GAME_SPEC 9.8.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 151.1 | The format: the level's `cycle`, an element's `motion` (keys: phase, offset, angle; a pivot) | Done |
| 151.2 | The world's moving surfaces: re-placed from their base specs through a rigid transform (`mapped`: a portal's image follows) | Done |
| 151.3 | The session's cycle: per tick on its own; the scroll pauses and sets it; a shot, or `resume_after` idle seconds without a scroll or a plan change, runs it again; frozen in flight; in the checkpoint; HUD and record (a cycle's level only) | Done |
| 151.4 | The C API 1.10 (`rc_game_scroll`, `rc_frame.phase`, `cycle_auto`); the wheel in Godot and tkinter; the session op `scroll` in every replay | Done |
| 151.5 | The editor (the motion field, its keys drawn faint, the cycle field); the demo level `objectives/timing_gate`; tests; docs; `make gate` | Done |
| 151.6 | The user's interactive sign-off | Open |

## Decisions

- **The cycle advances per physics tick, not per frame.** The native replay steps ticks and the C API replay
  advances frames of one tick each. Both must give the same golden, so the clock is the tick's.
- **"Positions, orientations and states":** positions and orientations are the motion's keys. States are
  already flags (doors, switches), and a moving door is a door with a motion.
- **The world re-places, never rebuilds the level.** A moving surface keeps its id and is rebuilt from its base
  spec through the frame's transform, the same mapping spawns use. Plans name surfaces, not positions, so they
  stay valid as surfaces move.
- **A shot sees the world of its moment.** The game is paused in flight, so the arrow flies through the surfaces
  as they stood when it was fired.

## Tests

`rcg_tests` "motion: ..." (4): a wall follows its keys over the cycle and loops, a scroll pauses and sets the phase,
idle seconds run it again; a rotation about the pivot, a portal's image moving with it, a shot running a paused
cycle again and undo restoring the phase; the demo level (the slider takes the shot until the cycle is set, then
the button opens the door to the goal); the schema. "editor: a moving element shows its keys faint ...". The session
golden gains `objectives__timing_gate__play` (its `scroll` replayed natively, through the C API and from Python).
