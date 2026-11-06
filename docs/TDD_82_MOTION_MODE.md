# TDD 82: One motion, its mode (Stage 204)

Your request: "Let's have a single \"motion\" field, with a \"motion_mode\" which can be \"opening\" or \"cycle\" for
now. Can I set the \"opening\" flag in the editor? I want it to be possible." The rules are GAME_SPEC 9.8 and 9.12.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 204.1 | The spec (GAME_SPEC 9.8, 9.12, 2.100.0): `motion` and `motion_mode` (cycle, opening); the `opening` key retired; steps the cycle plays there and back | Done |
| 204.2 | The format: one parse of `motion` per element by its mode -- an opening (`Level::openings`, steps only), or the cycle's (`Level::motions`: keys, or steps in `Motion::played`); `played_time`; `motion_mode` an element key (not an edge's) | Done |
| 204.3 | The session: a motion played by steps placed at the cycle's phase (`opening_pose` at `played_time`); nothing else changes (the cycle's HUD, scroll, checkpoint and record are a motion's) | Done |
| 204.4 | The editor: one `motion` row, the `motion_mode` row (a click switches it, the motion kept), an opening's `flag` row (typed) and its chips (the default, the detectors' flags, the level's active flags; the current one lit); steps and keys under `motion`; `+ key` refused in the opening mode or on steps | Done |
| 204.5 | The demo `objectives/swing_gate` migrated (its session golden unchanged); tests; docs; `make gate` | Done |
| 204.6 | The user's interactive sign-off (with Stages 202-203, 2026-11-06) | Done |

## Decisions

- **The mode is what plays the motion; the motion is kept across a switch.** So an opening becomes a cycle's motion,
  and back, by one click. Steps are the shared form: an opening plays them on its flag; the cycle plays them forward
  over its first half and back over its second (each step's share of the half by its seconds) -- an opening whose
  flag holds for half the cycle and then not. Keys stay the cycle's only: they are poses at phases, with no flag time.
- **The flag is kept, unread, in the cycle's mode**, so a switch back finds it. A cycle's motion never holds the shot
  (as Stage 151's), and its steps take no door look.
- **The default stays `cycle`**, so every level with a `motion` reads as before (`timing_gate`; the session golden
  unchanged). The `opening` key was never merged; the demo is migrated.
- **The flag in the editor**: a typed row (any key or expression) and chips of the flags the level already has --
  `door:<name>` (the default, a button's `opens`), each detector's flag, and the active flags (those a button, a door or
  a detector of the level uses), at most nine.

## Tests

`rcg_tests`: "motion: steps played by the cycle, there and back; the same steps an opening plays (Stage 204)";
"opening: the schema" (keys in the opening mode, an unknown mode, steps with keys); the opening cases on the new form;
"editor: one motion field, its motion_mode (cycle or opening) and an opening's flag (Stage 204)" (the mode row, the flag
row typed, a detector's chip, the default chip, keys refused, the motion kept across a switch); the Stage 202 and 203
editor cases on the new form.
