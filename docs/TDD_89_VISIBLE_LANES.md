# TDD 89: Only what shows takes a lane -- side by side for the visible preview (Stage 211)

Your request (2026-11-11): "currently the overlap detection mechanism considers segments which are not visible (opacity
0) after the fade. I want the overlap side by side logic to only treat visible segments."

The rule is GAME_SPEC 4.3 (steps drawn on one another, the Stage 211 note).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 211.1 | Lanes laid for the parts the fade leaves seen (`lay_out`: the fade, then `side_by_side` on what it shows) | Done |
| 211.2 | Goldens: the session golden's draw shas (37 sessions) | Done (approved) |
| 211.3 | The user's interactive sign-off | Done ("Works perfectly! All stages are approved", 2026-11-11) |

## Decisions

**Why.** Lanes (Stage 135) went by every step of the merge, the fade (Stage 195) by the lanes' lengths after. A shot that
runs back and forth between mirrors past the cursor had each faded lap take a lane in the group; the lanes are centred
on the path, so the laps that show were pushed to the group's edge -- on three_mirrors 14 px off the line the arrow
flies, where nothing else was drawn.

**The parts seen.** `lay_out` (session_draw.cpp) measures the fade with every step's lane (an arc's length is its
lane's, so the fade needs lanes first), takes each step's part from its start to where its fade is over (none: the
whole step; over before it begins: nothing), lays the lanes for those parts (`side_by_side`'s `shown`), and measures
the fade again with them. A step seen not at all takes no lane and is on no other; a part seen keeps its lane over the
whole step (its rest is drawn at alpha 0, that is, not drawn). The two measures differ only on arcs, by a lane's radius:
a step's last band (1/32 of its alpha) may lie on the other side of the cut by a few px.

**Every caller.** The path, its event marks, the stop markers, the glows and `preview_seen` all take the fade from
`lay_out`, so they agree. With the whole preview (F) nothing fades, every step takes part, as before. `lane_offsets`
and `dash_offsets` (the steps alone, no session) keep every step.

## Tests

- `rcg_tests`, `test_present.cpp` "side by side only for what shows": a planned step on the physical walk's line past
  its fade is drawn on the line (it failed before: 1.75 px aside); where the physical walk still shows, both, side by
  side.
- The session golden: 37 sessions' draw shas, nothing else (the judge's report).
