# TDD 83: A planned terminal, reached, shown whole (Stage 205)

Your request: "when a terminal surface is planned the green visibility is active, but one additional thing should
happen: the preview should reach the planned surface before fading off, ignoring the cursor, at least when fully
aligned. When diverged, the planned continuation should fade, and the physical walk should fade after the divergence."
And: "The planned terminal fading behavior should be consistent with the non terminal behavior." The rule is GAME_SPEC
4.3 (how far the preview shows).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 205.1 | The spec (GAME_SPEC 4.3, 2.101.0) | Done |
| 205.2 | `reveal_of` (session_draw.cpp): walks aligned all the way that meet the plan's entries in order and end at the last show whole to it; the plan passed by every caller (the path, its stops, the consequences) | Done |
| 205.3 | Tests; docs; `make gate` | Done |
| 205.4 | The user's interactive sign-off | Open |

## Decisions

- **Fully aligned only.** Every step is Aligned or AlignedPostPlanned, the significant events are the plan's entries in
  order with nothing unplanned between, and the last step ends at the last entry: the walk stopped at a planned
  terminal. Then nothing is faded (nothing follows a terminal). A cursor before it is still where the plan is reached
  (Stage 201): only the drawing changes.
- **Otherwise as any plan** (the user's "consistent with the non terminal behavior"): an aim that misses the terminal
  fades past the cursor; diverged walks fade, the planned path past the cursor and the physical one past the
  divergence (Stage 195), and a plan never reached past its first unplanned event (Stage 200).
- **Drawing only.** The preview record, the region and the green drawing (Stages 197, 198) are unchanged; the session
  golden replays equal.

## Tests

`rcg_tests` "preview: aligned to a planned terminal, whole to it; missed or diverged, faded as before (Stage 205)": the
cursor 309 units short of a planned grapple (past the 220 of the fade) draws exactly what the full preview draws, up to
the grapple; an aim that misses it fades; an unplanned mirror on the way fades both walks.
