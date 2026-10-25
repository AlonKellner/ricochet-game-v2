# TDD 66: A planned terminal stops the planned walk (Stage 162)

Your report (2026-10-25, objectives/grapple_gap, the grapple planned): "when the cursor directs the path to not hit
a surface, there is no divergence, but when the cursor does lead to hit the surface there is a divergence. I expect
the exact opposite". And: "It should behave similarly to pass-through planned surfaces." GAME_SPEC 14.1.

## The cause

The planned walk applied a planned entry's effect only when it was transformative or projective; a planned terminal
was consumed with no effect (and Stage 110's quiet-entry rule had the physical walk consume it too, even off its
segment). So:
- a path missing the grapple (past its end) crossed its carrier off the segment in both walks, with no effect in
  either: the walks agreed;
- a path hitting it stopped physically, while the planned walk went through, wrapped through infinity, came back to
  the cursor and only then stopped: the walks diverged.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 162.1 | The oracle and the solver: a planned terminal entry stops the planned walk at its planned hit (on or off its segment); the physical walk still consumes it quietly off its segment; a planned walk ending at a terminal at the cursor's point reaches it (the visibility rule, both sides) | Done |
| 162.2 | The fixtures: `stage110_quiet_entries.json` (the off-segment terminal now diverges; an on-segment one added), every other fixture regenerated unchanged; tests; docs; `make gate` | Done |
| 162.3 | The user's interactive sign-off | Open |

## Decisions

- **Like a planned mirror.** A planned mirror reflects the planned walk at its planned hit, on or off the segment; the
  physical walk reflects only on the segment. A planned terminal now does the same with a stop: on the segment the
  walks stop together (aligned), off it the planned walk stops and the physical walk passes (diverged) -- the
  outcome of a planned pass-through entry, as you asked.
- **The cursor at the stop.** With the planned terminal as the last entry, the planned walk can reach the cursor only
  where it stops, so a cursor exactly there is reached (the rule the physical walk already had). The visibility
  region keeps its whiskers on the planned terminal's segment; the region digests are unchanged.
- **Evidence.** One fixture file changed (the off-segment case now diverges, by design); every other oracle fixture
  regenerated identical, no session record and no region digest changed.

## Tests

`rc_tests` "quiet entries: ..." (the fixtures at f64, Dual, 4 and 64 limbs; a planned terminal on its segment: both
walks stop together; off it: the planned walk stops, the physical one reaches the cursor). `rcg_tests` "a planned
stopping surface: ...": your grapple_gap dumps -- the cursor past the grapple's end diverges, the cursor toward it is
aligned. The oracle's pytest.
