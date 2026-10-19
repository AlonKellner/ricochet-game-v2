# TDD 19: The Fork Rule (Stage 114)

Specification: GAME_SPEC §4.2 and §13.3 (the fork rule), §31.4 (K15). Background: TDD_18 (the projective visibility region, B6, K15 found), TDD_11 (hit effects), TDD_17 (exact exit lines).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 114.0 | Spec, the oracle (`back_propagate`: on-segment candidates preferred, then the click; a target on the carrier is its own candidate exactly; K15: the exit beam of a circle normal passes its centre by provenance, an inversion centred there keeps the line), oracle tests, fixtures regenerated (`stage110_visibility`, `stage87_click_forks`, `stage87_projective_plans` changed; the others identical) | Done |
| 114.1 | The judged re-bless (`make judge REF=main RULES=stage114`, `stage114_judge.py`): 0 sweep combos change (every manifest click is the via point, which never loses an arc's on-segment antipode); 19 golden fields -- 15 `fork-on-segment` (the corpus scenarios `projective_arcs` 00 and 06 with random clicks, the fixture `stage87_projective_plans/fork_other_click_planned`) and 4 error-bound consequences; 0 findings -- approved by the user (2026-10-19), applied; plus the renamed click-fork fixture's goldens (`cn_twice_clicked_other_planned`, formerly `cn_twice_at_target_planned`, and `cn_twice_clicked_target_planned`: 6 fields, all `fork-on-segment`) and `test_infinity.cpp`'s record (31 fixtures changed, 84 wrap events) | Done |
| 114.2 | Native: `effects::choose_candidate` / `back_propagate` (the snapped candidate), `Solver::backward_pass` (every candidate snapped to its data point before the segment test), K12 for circles (through the exact candidate), K15 (`Walk.through_c`, the straight inversion in the walker, `build_simple`'s `on_line` frames with the straight beam oriented by the pole order P -> X -> Q), the region's fork (`sigma_rule` by the segment first; the switch members at the click's line and the arc ends' radials) | Done |
| 114.3 | GUT (the Stage 113 fork and concentric tests, the Stage 87 click tests on the semicircular arc 9, `tests/test_stage114_fork_rule.gd`; 1123 passing), docs, `make gates` (628 s, green) | Done |
| 114.4 | Interactive sign-off (user, 2026-10-19) | Done |

## The rule

"When the path has an ambiguous fork, prefer hitting the segment, and then prefer closer to the plan click" (the user, 2026-10-19). Of a fork's candidates, those on the segment are preferred; among what remains, the click-nearest (the via point without a click); with none on the segment, the click-nearest off-segment one, a planned-only firing as before (B8). Consequences:

- An arc under a semicircle holds at most one of two antipodes: the click never matters, as if it sat at the via.
- An arc over a semicircle: the click decides only where both antipodes are on the arc; where one is off, the other wins.
- A full circle: the click, as before.
- A target on the carrier (a later entry's hit point on the same surface, the cursor exactly on it) is its own candidate, exactly, and the rule chooses between it and the other. Before, it was excluded ("the beam must reach X from the other one", B6). With the cursor's own point chosen the cursor is reached at once (K12, now for circles too). With a later entry's hit point chosen, the beam fires there and meets the entry again after its full loop (the departure re-hit last, §11.4): the plan [5 L, 5 L] is served by it, [5 L, 5 R] by the antipode; the click picks, with no search.
- The rule applies to every present and future fork (circle directional projections too).

## Known findings

| # | Finding | Status |
|---|---|---|
| K15 | A beam through an inversion's centre (found on `projective_concentric`, TDD_18): the native inverted the rounded beam into a circle of enormous radius whose wrap overflowed, and the solve returned nothing where the oracle traced | Fixed: the exit beam carries its centre by provenance (`through_c`, both sides); an inversion centred there (exactly, on the data) keeps the line; the chain's straight beam keeps the circle's order P -> X -> Q (the target behind the centre is reached through the real infinity) |

## Unit Tests Added

| File | Covers |
|---|---|
| `docs/research/oracle/test_stage114.py` | the rule on arcs under and over a semicircle, a full circle, no candidate on the segment; the cursor on the segment as its own candidate; K15: the beam after the concentric inversion is straight |
| `docs/research/oracle/test_trace2.py` | the snapped candidate (`test_back_propagate_provenance_makes_the_target_its_own_candidate`) |
| `docs/research/oracle/gen_fixtures_stage87_clicks.py` | the two entries on one circle: clicked at the second's point, the first hit is that point; clicked at its antipode, the other |
| `native/tests/test_visibility.cpp` | "K15" (now a plain test), the debug probe's clicks (`RC_VIS_PLAN="5L@650;370"`) and raw ladder |
| `tests/test_stage114_fork_rule.gd` | an arc over a semicircle (the click decides only where both antipodes are on it; the region follows the preview), under a semicircle (the click is irrelevant), a cursor on the segment as its own hit point |
| `tests/test_stage113_projective_visibility.gd` | the fork test's expectations under the rule |

## Regression Checklist

The full GUT suite, ctest (RelWithDebInfo and ASan), the oracle tests, the level parity, the goldens re-blessed with the user's approval, the region digest.
