# TDD 81: Two-point steps (Stage 203)

Your request: "I want the sequence of opening to be visually editable. Specifically, I want to be able to configure
a translation by placing two arbitrary points, their difference is the translation. I also want to be able to
configure rotation by placing two arbitrary points, the first one is the pivot and the angle between the points is
the target angle for the start and end points of the surface ... Additionally, the same mechanism should be supported
for moving surfaces as well." The rules are GAME_SPEC 9.12 (openings) and 9.8 (motion keys). (Stage 204 folds the
`opening` key into `motion` with `"motion_mode": "opening"`: docs/TDD_82_MOTION_MODE.md.)

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 203.1 | The spec (GAME_SPEC 9.12, 9.8, 2.99.0): a step and a key by two points; an opening's points where the step begins | Done |
| 203.2 | The format: `{"rotate": [pivot, aim]}`, `{"translate": [from, to]}` in an opening's steps; `{"phase", "rotate", "translate"}` keys, lowered to `[phase, dx, dy, angle]` about the motion's pivot; each step's and key's points kept (`Opening::Step::points`, `Motion::points`; derived for numbers); `motion_pivot` shared by the parser, the session and the editor | Done |
| 203.3 | The editor: the steps' and keys' rows and actions (`+ turn`, `+ move`, `+ key`, `turn n`, `move n`, `remove n`), two-click placement (Esc cancels), their points as handles, the poses after each step drawn faint; the commands `step`, `remove_step`, `add_key`, `place_step`; moves carry the points and the motion's pivot; a scale maps them | Done |
| 203.4 | The demo `objectives/swing_gate` written by points (its session golden unchanged); tests; docs; `make gate` | Done |
| 203.5 | The user's interactive sign-off (with Stage 202's) | Open |

## Decisions

- **The aim sets a direction, absolutely**: after the turn the element's start -> end (its first surface's chord)
  points along pivot -> aim. For a full circle (no chord) the direction is the pivot -> its center (at its center:
  its radius along its x axis, a turn nobody sees).
- **The shortest way.** An opening's turn is in (-pi, pi]: half a turn exactly is clockwise on screen. A turn of more
  than half a turn is two steps. A motion key's turn is the one nearest the previous key's angle, so the keys between
  them turn the short way and a sequence of keys can turn on past half a turn.
- **Where the points are.** An opening's points are where the element stands when the step begins (the pose after
  the earlier steps, drawn faint in the editor), so what you click is what you see. This replaces Stage 202's pivot
  carried from the closed pose (unmerged, so no level changes: the demo's only rotation is its first step). A motion
  key's points are from the element as written (a key is a pose, not a step).
- **Numbers still read.** `{"rotate": angle, "pivot"}`, `{"translate": [dx, dy]}` and `[phase, dx, dy, angle]` keep
  their meaning; the editor shows the points they make, and dragging one rewrites that step or part by points.
- **A key by points lowers to numbers**: x -> R(a) (x - c) + c + d about the key's pivot c is, about the motion's
  pivot P, the offset R(a) (P - c) + c - P + d. Between keys the element still turns about P (the interpolation is
  Stage 151's), so a key's pivot sets where the element is at the key, not the arc it travels between keys.

## Tests

`rcg_tests`: "opening: steps by two points (Stage 203) ..." (a move's difference, a turn's exact quarter, the
shortest way, half a turn clockwise, an aim from the step's pose, a circle); "opening: steps in order ..." (a pivot
where the step begins; numbers' derived points); the schema (equal points, a pivot with points, a bad point);
"motion: keys by two points (Stage 203) ..." (the lowering, the derived points, in the game, the turn nearest the
previous key's) and the schema; "editor: an opening's steps and a motion's keys placed by two clicks, their points
dragged (Stage 203)".
