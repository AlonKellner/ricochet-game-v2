# TDD 83: A planned terminal, shown whole to its hit or its miss (Stage 205)

Your request: "when a terminal surface is planned the green visibility is active, but one additional thing should
happen: the preview should reach the planned surface before fading off, ignoring the cursor, at least when fully
aligned. When diverged, the planned continuation should fade, and the physical walk should fade after the divergence."
And: "The planned terminal fading behavior should be consistent with the non terminal behavior." Then, on a dump
(grapple_1, the aim crossing the grapple's line off its segment past the cursor): "I expect the aligned preview to reach
the divergence, and then for the continuation to fade ... the off-segment hitpoint should be reached." The rule is
GAME_SPEC 4.3 (how far the preview shows).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 205.1 | The spec (GAME_SPEC 4.3, 2.101.0) | Done |
| 205.2 | `reveal_of` (session_draw.cpp): walks aligned as far as the plan's last entry (its entries met in order) show whole to where they meet its carrier -- the hit, or a crossing off its segment -- and fade past it as past a cursor; the plan passed by every caller (the path, its stops, the consequences) | Done |
| 205.3 | Tests; docs; `make gate` | Done |
| 205.4 | The user's interactive sign-off | Open |

## Decisions

- **The terminal's carrier is where the plan's path is meant to go.** Once only a planned terminal is left the cursor
  counts (Stage 201), so the steps past it are post-planned and faded from it. Now, when the walks are aligned as far
  as the terminal's carrier, meeting the plan's entries in order with nothing unplanned between, the fade starts at
  their meeting with it instead (the later of it and the cursor): its hit, where the walk ends, or a crossing of the
  carrier off its segment, a miss, past which the walk goes on and fades as past a cursor.
- **Diverged before it, as any plan** (the user's "consistent with the non terminal behavior"): the planned path past
  the cursor, the physical one past the divergence (Stage 195), a plan never reached past its first unplanned event
  (Stage 200). A divergence past the meeting point fades from the meeting point, as one past a cursor does.
- **Drawing only.** The preview record, the region and the green drawing (Stages 197, 198) are unchanged; the session
  golden replays equal.

## Tests

`rcg_tests` "preview: aligned to a planned terminal, whole to its hit or its miss; diverged, faded as before (Stage
205)": the cursor 309 units short of a planned grapple's hit draws exactly what the full preview draws; an aim missing
it just above draws whole to its crossing of the grapple's line, then fades; the user's grapple_1 dump (the miss 500
past the cursor) draws whole to the miss; an unplanned mirror on the way fades both walks as before.
