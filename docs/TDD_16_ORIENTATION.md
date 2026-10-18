# TDD Document 16: The Realization by the First Entry

**Stage 111** | The first epoch's orientation -- toward the cursor's unfolded image, or the same circle the other way round -- is fixed by the plan's first entry, not by where the image happens to fall. ∞ entries are matched by the walker; they select a realization only where the first entry leaves both open. The region follows per cursor.

Specification: GAME_SPEC §13.1 (the two realizations, R1-R3), §4.2, §9.5, §12.5, §15. Background: TDD_10 (Stages 88, 101: the infinity entry), TDD_14 (the region).

### Stage Status

| Phase | Topic | Status |
|-------|-------|--------|
| 111.0 | Oracle: `trace2.realization` (R1-R3), `first_entry`; tests; the stage 88 and 111 fixture generators | Done |
| 111.1 | Native: `Solver::realize`, `realization`, `tie_choice`, a forced orientation for the region; DIRECTION-ONLY's reference | Done |
| 111.2 | Region: two forced halves joined, the tie cut, the crack members, the per-half checks | Done |
| 111.3 | GUT: `test_stage111_orientation.gd`; the Stage 87 fork test and the spent-host test re-derived; the scene corpus regenerated | Done |
| 111.4 | Goldens: judged (`make judge RULES=stage111`: 32,742 sweep combos, 612 fields -- corpus 568, fixtures 33, regions 11 -- 0 findings); re-blessed on the user's instruction to merge (2026-10-18) | Done |
| 111.5 | Interactive sign-off | Pending |
| 111.6 | The region's cost (Workflow W2.0): the two halves share a solve memo (`vis::SolveMemo`: a half's tie cast is the other half's own cast of the same cursor); the per-cast cost is back to Stage 110's, the cast count is the two halves' (1.7x); digest unchanged | Done |

**Workflow:** branch `orientation-by-entry`, small green commits; merged with `workflow-native` (the rework of the gates, `docs/WORKFLOW.md`): `make gate` before every commit, `make gates` before the merge.

## The finding

Four F12 captures on `scenes/test_levels/mirror_and_wall.tscn` (the player at (1306.67, 540) on the reflective side of the vertical mirror 5 at x = 800; the level open to the right, pass edges). With plan [5 L] a cursor beyond the mirror in front of the player's image (480.5, 525) was reached (aim at the mirror, reflect, wrap, cursor), one behind the image (166.2, 536) was not: the aim went *away* from the mirror, toward the cursor's image, which lies behind the player. With [5 L, ∞] the two swapped roles. The rule of Stages 88 and 101 oriented `B_0` toward the image `X_0` and reversed it for an ∞ entry anywhere in the first epoch; which side of the player `X_0` falls on is an accident of the geometry, not a property of the plan (user, 2026-10-18). The same defect with a circle carrier: an inversion arc seen from outside with the cursor inside it (`room_with_inversion` [5 L]).

## Design decisions

**The rule (GAME_SPEC §13.1).** Let `e` be the first planned entry of the first epoch that names a present surface. R1: when only one orientation of `B_0` crosses `e`'s carrier from `e`'s side, that one (a straight `B_0` and a line carrier, always: on the planned side aim at the carrier, on the other side aim away and come back through infinity). R2: when both fire `e`, the one whose planned walk consumes the plan and reaches the cursor; when both or neither do and the two fire `e` on different sides of the passage of `Q_0`, the one that fires `e` before `Q_0` -- after it when an ∞ entry precedes `e`. R3: otherwise today's rule (toward `X_0`, reversed by an ∞ entry in the first epoch).

**The walker is unchanged.** The cursor is a candidate only once the plan is consumed, a beam's loop is walked once, an unplanned passage of infinity is not a divergence: with the right orientation the captures come out as wanted, and a cursor on the player's side with [5 L, ∞] is simply passed with the entry pending (unreached), which is what "planning infinity treats direct paths as divergent" asks.

**Decided by the walker's predicates.** The scan of `e`'s carrier on `B_0` in both orientations uses the carrier's roots on the beam (a root at t = 0, the player on the carrier, met last; one at t = ∞, the antipode of a circle beam, at the wrap), `side_left` at each crossing, the projective order with the pole candidate (the hit before the pole at a tie), the exact pole record of a pre-transform start (a carrier through `Q_0` is crossed exactly there; tangent there, a touch). For a straight `B_0` and a line carrier it is the one sign `dot(X_0 − P, grad)`, from the homogeneous aim image with rigorous bounds (`exact_image_approx`) and exactly (`exact_image_cached`) when they straddle zero; the tier's value where no exact image exists (a rotated portal, a projective's target). R2's walk runs with a wide budget (8 × `MAX_HITS`): the plan's order is a question of geometry, not of the event limit (with the limit, the realization of a dense scene flipped from member to member).

**The region: two forced halves.** The realization is per cursor, so a region is the union of two: every member cast with the orientation forced direct, and again forced reversed (`Tracer::solve(..., force_reversed)`; `Solver::force_reversed`, in the chain cache's key). Each half is today's machinery (`via_` is the half). Where a half is not the member's tie-choice (`Solver::tie_choice`: what a cursor both planned walks reach gets), the other orientation's planned reach is the plan's realization there -- its whole loop from its departure, or, after a planned ∞ last, from `Q_K` round to its departure -- and the cast keeps only what lies outside it (the `TieNear` mark, a departure arc on the last entry's carrier). The aims parallel to a line first carrier are members of both halves (no orientation fires the entry there: the crack between the halves). The joined result tags every cell and loop by its half; VIS-ORIENTED and VIS-DISJOINT run per half (the halves wind opposite ways and share the crack), VIS-CELL-CONSTANT probes in the cell's half, VIS-ITERATION-CHAIN builds the prefix in the cell's orientation, VIS-BOUNDARY-DECIDED reads the half the solver picks (`Cast::chosen`, every copy of the point pooled). A half's member whisker inside the other half's area is dropped (one member's cast landed on the other side of the switch); VIS-REGULAR allows a whisker along the straight member through `P_K` and a finite `Q_K` (an exact line among circles: its own realization's reach, `tangent_infinity` [6 R]). `intervals`: after a planned ∞ last, the departure re-hit is a stop at the departure point, not a full loop (the pieces passed before the wrap were not reached; today that cast was dead).

**What changes visibly.** Captures 2 and 3 are ALIGNED through infinity; [M L, ∞] leaves the player's side unreached; [∞, M L] from the reflective side diverges at M; a circle carrier seen from outside is aimed at whatever the cursor's image; the region of a line-first plan covers both sides of the player's image, with the parallel aims as a crack; a projective first from its wrong side is reached through infinity; the Stage 87 far click fork is realized the other way round (the clicked candidate is the first crossing met after the wrap); nothing changes for direction shots, the UI, or plans without a surface entry.

## Evidence

- Oracle: `docs/research/oracle/test_trace2.py` (the four captures, the pass-side mirror, the inversion arc from outside, the player inside a circle, the fork); fixtures `stage88_infinity.json` (22 of 26 unchanged; the two misplaced-order cases are now [∞, M] realized at the far face / unmet at the near one; the two [inv, ∞] cases aim at the circle), `stage87_click_forks.json` regenerated (the far fork), `stage111_orientation.json` new (16 pairs); `stage87_projective_plans.json` unchanged once its generator prefers physically realizable side combinations (K10); `stage101`, `stage105`, `stage106`, `stage107`, `stage108`, `stage110*`, `perf_circle_ties` unchanged.
- Native: `test_infinity.cpp` "Stage 111" at f64, Dual, 4 and 64 limbs; every non-golden test green; the region pins on all shipped levels (100 regions, 13,019 cursors, 0 mismatches) and the structure checks (93 regions, 0 violations).
- GUT: `test_stage111_orientation.gd`; `test_stage87_click_forks.gd` (the far fork) and `test_stage107_world.gd` (the spent host: the side facing the player) re-derived; the scene corpus regenerated (`gen_scene_corpus.py`: 85 expectations changed, 24 scenarios resampled as unstable at double precision under the new rule); the invariant and visibility sweeps green with empty `KNOWN_FINDINGS`.
- Judge (`docs/research/stage111_judge.py`, old build from `main` in a worktree, `RC_SWEEP_DUMP` of every mismatching combo): see §Goldens.
- Region digest: `8de50477871503e8` → `7bfc2086b59d959c`; 11 of 264 regions changed: `mirror_and_wall` [5 L] (both players), `three_mirrors` [4 L], `two_sided` [5 L], `portals` [8 L], `crossings` [16 L], `ghost_room` [6 L], [12 L], [11 R, 6 L], `pre_inverted` [4 R, 3 L], and `tangent_infinity` [∞] (a loop that began at `Q_K` now ends there: `vertex_on` resolved a `Loop` end to the near point even when the near end was a pole).

## Goldens

The judge (`docs/research/stage111_judge.py`) compares every changed trace of the old build (`main`, a git worktree built with `-DRC_BUILD_EXTENSION=OFF`) and the new one: the new aim must be the old aim run the other way round (the same start, the tangent and curvature negated), and the first planned entry `e` must be fired by the new realization where the old never fired it (R1-fix), or both fired it and only the new one reaches the cursor (R2-reach), both reach (R2-choice: the order against `Q_0` decided) or neither (R2-neither); a physical trace follows its planned twin's aim. Anything else is a finding.

| Set | Changed | R1-fix | R2-reach | R2-choice | R2-neither | Findings |
|-----|---------|--------|----------|-----------|------------|----------|
| Sweep goldens (`sweeps.json`, 284,096 combos) | 32,742 combos (every physical follows its planned) | 30,046 | 2,400 | 181 | 115 | 0 |
| Fixture and corpus goldens (`fixtures.json`, `corpus.json`) | 601 fields (222 physical follow; 142 are error bounds and tiers) | 198 | 33 | 3 | 3 | 0 |

By group the sweep changes are `with_plans/*` (line and circle mirrors, R1 and R2-choice), `projective_plans/*` (R1 on the line projectives, R2-reach on the arcs), `combos/*`, `precision/plans/*`; the full report is the judge's output. Since the workflow rework the judge runs as `make judge RULES=stage111` (`docs/research/judge.py` with `stage111_judge.py` as its rules module: the same classes, plus the generic ones and the 11 changed regions of `bench/goldens/regions.json`), the report lands in `native/build-ref/judge/report.md`, and `make rebless APPROVED=1` applies it. Applied on 2026-10-18 on the user's instruction to merge both branches to main.

**The region's cost (Workflow W2.0).** On a split plan every cast ran up to two ladder solves (its own and, where the tie-choice is the other orientation, the tie cast) and two realizations, and the second half re-solved what the first had: `mirror_and_wall` [5 L] built in 35 ms against 11.5 ms before the stage (442 casts against 237, 14,650 ladder walks against 5,521), and the GUT visibility sweep went from 580 to over 2,000 s of CPU. The two halves now share a `vis::SolveMemo` (the ladder solve by cursor and orientation, the tie-choice and the realization by cursor; pure functions, the cast threads share it under a mutex): 19.9 ms, 10,338 walks; per cast 45 µs against Stage 110's 48 µs. What remains is the cast count of two halves. VIS-DETERMINISTIC rebuilds every region once more by design (42 % of a sweep's time), ITERATION-CHAIN 12 %.

**Debugging (Workflow W5).** `RC_VIS_TRACE=1` prints one line per cast (the half, the cursor, the aim, the tie-choice, chosen, valid / alive / met, the consuming event, both ends, a tie cut); `join_halves` asserts every member reference of the joined region in range, and a cast asserts a tie cut only where the tie-choice is the other orientation (`RC_ASSERT`: abort under ASan and Debug).

## Known findings

| Id | Finding | Status |
|----|---------|--------|
| K10 | A planned walk through a projective reached in the reversed first epoch: on `bench/fixtures/stage87_projective_plans.json` `mixed_chain` (player (200, −200), cursor (250, 300), plan [1 L, 2 R, 3 R]) forced the other way round, the native beam after the reflection at 3 carries a tilt of 2⁻⁵⁴ where the oracle's exact line is vertical, so the walk crosses the parallel carriers 1 and 11 at y ≈ 2⁶¹ before the wrap (the oracle has no such crossings). The fixture generators now prefer physically realizable side combinations (the plan's far side reaches the cursor the long way round but the room's walls stop the shot), so no fixture pins the path; `BeamSolver.planned` on those inputs with the plan's first side LEFT reproduces it. | Open |

## Debug

- `RC_VIS_TIE=1`: the tie-choice of every cast of a split region (half, cursor, aim, angle, choice) on stderr; `RC_VIS_HALF=1|2` with `RC_VIS_PROBE`: the probe cast in that forced orientation; `RC_REALIZE_TRACE=1`: the solver's scan (both orientations' candidates and sides) on stderr.
