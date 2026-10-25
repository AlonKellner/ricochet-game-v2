# TDD 68: Every element visible and editable (Stage 164)

Your rule (2026-10-25), after an invisible ledge stopped a grapple in midair: "The colliders must be visible and
editable, every single level element must be visible and editable in the editor. Some elements may be invisible
during playing, but most will also be visible during play time." GAME_SPEC 19.2.

## The audit

| Level key | Before | Now |
|---|---|---|
| `colliders` | neither drawn nor editable, in the editor or in play | editor items (X places one; select, drag the ends, duplicate, remove); drawn in play |
| `camera` | not drawn; edited only in the file | an editor item (its start drags; smoothing in its rows); invisible in play |
| `background` | not drawn in the editor | outlined faintly, a panel field; painted in play as before |
| `features`, `templates`, `carriers` | no panel field | panel fields |
| elements, the room, the bounds, the player, the goal, detectors, objects, carriers, shades, motions, flags, cycle, spread | drawn and editable | unchanged |

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 164.1 | The editor: `collider:<index>` and `camera` items (paths, handles, picking, dragging, rows, move, duplicate, remove), the collider tool (X, two clicks), drawing (hatched floors, a camera frame, the background's outline), every level key a field | Done |
| 164.2 | Play: the `["colliders"]` node | Done |
| 164.3 | Tests; docs; the session golden re-recorded (draw lists only); `make gate` | Done |
| 164.4 | The user's interactive sign-off | Done (the user's sign-off 2026-10-25) |

## Decisions

- **Colliders are named by their index.** The format gives them no names; an item `collider:2` is the third.
  Duplicating inserts the copy after it.
- **Visible in play: colliders, not the camera.** Colliders are floors and ledges the player meets, so they are drawn
  (a muted line, under the surfaces). The camera is a view setting; the background is already painted.
- **Evidence.** Nine session records of the three levels with colliders changed, in their draw lists only (compared
  field by field); flags, positions, previews and visibility are unchanged.

## Tests

`rcg_tests` "editor: colliders and the camera are items ...", "editor: the collider tool places a collider with two
clicks; the level's every key is a field", "colliders are drawn in play", and the tool palette's four rows.
