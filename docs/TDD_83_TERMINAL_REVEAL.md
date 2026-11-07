# TDD 83: Only physical sections fade -- a planned terminal shown whole (Stage 205)

Your request: "when a terminal surface is planned the green visibility is active, but one additional thing should
happen: the preview should reach the planned surface before fading off, ignoring the cursor, at least when fully
aligned. When diverged, the planned continuation should fade, and the physical walk should fade after the divergence."
And: "The planned terminal fading behavior should be consistent with the non terminal behavior." Then, on a dump
(grapple_1, the aim crossing the grapple's line off its segment past the cursor): "I expect the aligned preview to reach
the divergence, and then for the continuation to fade ... the off-segment hitpoint should be reached." Then, on a second
dump (grapple_1, an unplanned terminal wall before the planned grapple): "I expect the divergence to fully show, and the
planned path to visualize up to the planned surface. There should be an invariant, that the planned sections of the
preview up to the cursor should always be visible (not the planned continuation). Only physical sections may fade,
whether it's the physical walk or the continuation of the planned walk." And: "Add any invariants to the tests which
are expected but not yet implemented, in relations to the behavior of the preview and fading systems."

The rules are GAME_SPEC 4.3 (how far the preview shows), 12.2 (the planned walk past its cursor), 14.2 (the merge).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 205.1 | The spec (GAME_SPEC 4.3, 12.2, 13.5, 14.2; 2.101.0); the oracle (`trace2.py`: the planned walk keeps the plan's rules past the cursor while a planned terminal is left, until an unplanned passage) | Done |
| 205.2 | The solver (`Solver::planned_past_cursor`), the ladder's and the tracer's shortcuts (`Ladder::spent_at`: the physical walk is the planned one only once the plan is spent), V3 by the walk that leaves the cursor by PHYSICAL rules (a second solve, only for plans with a terminal left) | Done |
| 205.3 | The merge (`sweep::merge_segments`): both paths' segments end at the cursor when both reach it | Done |
| 205.4 | The drawing (`reveal_of`, session_draw.cpp): the planned walk to its planned terminal whole, the physical walk past the divergence and the planned walk's physical continuation fade; the display modes (Tab) fade each walk where the merge does | Done |
| 205.5 | Tests: the user's two dumps; the fade's invariants over sweeps of plans and cursors | Done |
| 205.6 | Goldens: `stage201_terminal_cursor.json` (3 planned walks), the session golden (13 draws): the user's approval | Open |
| 205.7 | The user's interactive sign-off | Open |

## Decisions

- **The planned walk keeps the plan's rules past the cursor to a planned terminal.** Since Stage 201 the cursor counts
  once only a planned terminal is left; until now the walk went on by PHYSICAL rules from the cursor, so an unplanned
  surface on the way (the second dump's wall) stopped it short of the terminal. Now it passes unplanned surfaces as
  before the cursor and stops at the terminal's planned hit, on or off its segment (Stage 162). The physical walk is
  unchanged: where it meets something the plan does not, the walks diverge, past the cursor.
- **An unplanned passage of infinity or of the ray's origin ends the plan's rules** past the cursor (before the cursor
  it is a divergence, Stage 131): a beam that reaches the terminal only through one is not going to it. A circle that
  never meets the terminal loops no more. Past such a passage the planned walk goes on by PHYSICAL rules, and fades.
- **The cursor's own point is decided as the region decides it.** The region of a plan ending at a terminal is the
  region of the entries before it (Stage 201), where both walks leave the cursor by PHYSICAL rules. V3 reads the walk
  that does so (a second ladder solve with `planned_past_cursor` off, only when a terminal is left): the region and the
  preview agree on every sweep combo, the degenerate cursors on planned carriers included.
- **The merge's segments end at the cursor in both paths** (GAME_SPEC 14.2 named the planned path's cursor a boundary):
  walks that part just past the cursor are aligned to it, whatever the steps after it (a step to infinity, a near-line
  arc of radius 1e10, where an end cannot be found on a step).
- **Only physical sections fade.** Aligned steps and a diverged planned walk to its cursor are whole always (a planned
  walk that never reaches its cursor aside, Stage 200); a planned walk that goes on to its planned terminal is whole to
  it; the physical walk fades from the divergence; the planned walk's continuation by PHYSICAL rules from the cursor.
- **The display modes (Tab) fade each walk where the merge does.** Until now the planned walk alone did not apply Stage
  200 (a planned walk that never reaches its cursor was drawn whole to its limits).

## Tests

`rcg_tests`:
- "preview: planned to a planned terminal, whole to its hit or its miss; physical sections fade (Stage 205)": the
  cursor 309 short of a planned grapple's hit draws what the full preview draws; an aim just above it draws whole to
  its crossing of the grapple's line, then fades; the user's first dump (the miss 500 past the cursor) draws whole to
  the miss; an unplanned mirror on the way: the planned path whole to the grapple, the physical one fading past the
  mirror; the user's second dump (the wall before the grapple): nothing fades, the planned path diverged past the wall.
- "preview: the fade's invariants over plans and cursors (Stage 205)": a room with a grapple, two mirrors and a wall
  under four plans, and the user's two grapple_1 setups, over grids of cursors (about 2,000 previews): a step never
  brightens along itself; the planned path to its cursor is whole; on to a planned terminal whole; only physical
  sections fade; past the cursor a planned walk not stopped by a planned terminal fades; each walk fades continuously;
  the full preview fades nothing; each walk alone (Tab) fades exactly when it does in the merge.
- "session: the cursor counts before a planned terminal, reached or not (Stage 201)": no divergence before the cursor
  (past it a miss now parts the walks at the grapple's line).

`rc_tests`: the Stage 201 fixtures (`stage201_terminal_cursor.json`, regenerated by the oracle):
- `line_missed_planned`: cursor, the grapple's line off its segment (the planned walk stops) -- was: on to the wall;
- `circle_missed_planned`: cursor, infinity (the plan's rules end), origin, the wall -- was: the wall;
- `the_other_way_planned`: cursor, infinity, the terminal from behind -- was: the far wall.
