# TDD 36: Hit limits, event marks, unplanned wraps and the ray's origin (Stage 131)

The user's F12 report on open_room_playable (2026-10-22): the path went round a full loop, then off the screen.
Was there no loop detection to stop it where it came back through the player? The user decided:

- **No loop detection.** A shot makes at most **10 significant events** and at most **64 events** in all (no
  third constant). The cursor is not counted.
- **Significant events:** a block, a reflection or other applied effect, a state change, a passage of infinity,
  a passage of the ray's origin.
- **Marks:** a significant event gets a clear mark (a ringed dot); any other crossing a small dot. An off-screen
  passage of infinity gets two half rings, where the path leaves and where it re-enters the level; a portal the
  same (one significant event). Repeated marks sit side by side, like their steps.
- **An unplanned infinity is a divergence.** The aim prefers a path without infinity; when only a wrap reaches the
  cursor it aims through it, but the path diverges there.
- **The ray's origin** is significant and can diverge like infinity. It is the player's image in the frame (the
  player carried by every map since); after a circle normal projection, the circle's centre (the user: "this
  makes the inner circle normal projection completely non ambiguous"). It is a candidate of the walk, like the
  cursor. *Planning it is Stage 132.*

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 131.1 | The limits (10 significant, 64 in all), no loop detection (spec, oracle, native) | Done |
| 131.2 | Event marks: ringed dots, small dots, half rings at the bounds and at portals, side by side | Done |
| 131.3 | An unplanned passage of infinity before the cursor diverges; the region stops there | Done |
| 131.4 | The ray's origin: the player's image (a circle normal's centre), a candidate; unplanned, a divergence | Done |
| 131.5 | Regeneration on every core; goldens judged and re-blessed (approved); docs; `make gates`; sign-off | Sign-off pending |

## 131.1 The limits

`MAX_HITS = 64`, `MAX_SIGNIFICANT = 10` (`trace2.py`, `solver.hpp`). A walk counts every crossing and every
significant event. The crossing that would pass either limit is a `max` event: drawn up to, no effect. The
realization (GAME_SPEC 13.1) gets 8 × each. Loop detection is gone: a loop goes round again, and its passages of
infinity and of the ray's origin end it by the significant limit.

## 131.2 Marks

`PathPainter::event_marks` (session_draw.cpp) marks each step's end:

- a significant event: a white ring (radius 5.5) around a dot (radius 4) in the step's colour;
- any other crossing: a small dot (radius 2) at half opacity.

An infinity passage off the level is two half rings at the level-bounds crossings of the outgoing and incoming
steps (`bounds_crossing`), facing into the level. A portal is two half rings: at its entry, facing back, and at its
exit, facing forward. Marks of steps in side-by-side lanes move with their lanes.

## 131.3 An unplanned wrap diverges

The physical walk leaves the plan at a passage of infinity that consumes no entry while its cursor is still ahead
(`left_plan`, `wrapped_off`, the event `off_plan`). After it the physical walk has no cursor. The region stops a
cast at the first such passage past the last entry (`End::PoleQ`, or `End::PoleP` when the passage lies at P_K,
as in a pencil through the image of infinity). A radial pencil's P_K is inside its members (`te = tP`).

The user's report (open_room_playable, [5 L]) and its control ([5 L, ∞], aligned) are tests:
`region reports: open_room_playable [5 L]`.

## 131.4 The ray's origin

The walk carries `O`, the ray's origin in the frame:

- the player at the start;
- mapped by every pushed effect (`map_point`), restored by a pop;
- after a projection, the new ray's departure, or a circle normal's centre.

Where `O` is not the beam's departure, it is a candidate at its parameter, and a known point of the beam. A
carrier exactly through it is crossed there, the crossing first. At the departure, the lap's end is the passage.
A lap end at another departure is no event. A lap that counted nothing ends there all the same.

The region stops a cast at an origin passage past the last entry, away from the cast's start (`End::PoleP` at P_K,
else a stop at its point).

The user's report (open_room_playable, [5 L, ∞]: the beam back from the wrap passes the player's image in the
mirror x = 1200 before the cursor) is a test: `region reports: open_room_playable [5 L, inf]`, and
`test_stage131.py`.

## 131.5 Regeneration on every core

The fixture regeneration took about 22 minutes on one core: `gen_fixtures_stage110_visibility.py` labels each
cursor in turn.

- It now labels a case's cursors in a fork pool, keeping the order.
- `regen.py` runs the generators concurrently, the slowest first. Each writes its own files.
- The whole regeneration takes about 3 minutes, byte for byte the same. `RC_REGEN_JOBS=1` runs it serially.

## The goldens (judged, approved by the user 2026-10-22)

| Golden | Changed | Evidence |
|---|---|---|
| sweep goldens | 83,252 combos | the planned aim the same in all; the classes: the next event an `origin` passage (most: a reflected beam passing the player's image after a wrap), cuts by the new limits, walks cut at 32 before now running on |
| region digests | 9 | all shrink only. 37 sampled cursors that left: main's oracle visible, the new one not -- each passes infinity or the ray's origin unplanned first. 8 kept controls visible to both |
| corpus and fixture goldens | 3,504 fields | the same rules |
| sessions | 48 of 48 | draw (the marks) everywhere; 816 preview records in 42 sessions (548 longer, 262 shorter, 6 frames only); 45 flight records in 18 sessions (e.g. hit_effects shots step 10: an inverted beam laps until the 10th significant event); 7 visibility records (ghost_room) |
| GUT traces | 488 rows | re-recorded natively (`RC_GUT_TRACES_WRITE`). The 82 rows where the cursor is no longer reached: 80 of them are `test_a_circle_normal_fork_follows_the_click`'s grid cursors, all past the circle's centre (the circle normal's origin). The ledger's assertion, (620, 330) visible for both clicks, holds |

## Findings

| # | Finding | Status |
|---|---|---|
| F26 | pre_inverted's preview costs more: cursor orbit mean 0.11 → 0.74 ms (max 5.4 ms). Its circle beams now lap until the 10th significant event | Open (under budget) |
| F27 | crossings: the worst plan-orbit frame (a region computed in the frame) went from 13 to 24 ms. In live play the region is asynchronous | Open |
| F23, F24, F21 | as in TDD_34 / TDD_33 | Open |

## Tests

| Test | What it checks |
|---|---|
| `test_stage131.py` (oracle) | the limits, an unplanned wrap diverges, a passage after the cursor is the continuation, the player's image is the origin (the user's dump), a circle normal's origin is its centre |
| `test_trace2.py` | the loops and realizations, updated: the origin passages in their places |
| `region reports` (rcg_tests) | the user's two dumps and the control |
| `test_present.cpp` | the marks: significant and other, wrap and portal half rings, side by side |
| `test_infinity.cpp` | the switch on against the switch off: insertions (infinity and origin passages), wraps past an escape, earlier cuts (47 / 86 / 12 / 9) |
