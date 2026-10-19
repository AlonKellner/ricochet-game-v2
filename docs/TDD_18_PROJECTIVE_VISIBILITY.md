# TDD Document 18: The Visibility Region Through Projective Plan Entries

**Stage 113** | The white region under the preview (GAME_SPEC §15) for every plan with projective entries: line normal, circle normal, line directional and circle directional projections, through or back, from the outer or the inner side, with transformatives before and after them, and two projectives in one plan. The construction stays Stage 110's: casts (ordinary pair traces) read by their events, cells between members, clipping, merging, one mesh. What changes is the family of members.

Specification: GAME_SPEC §15 (§15.1 the pencil's poles, §15.2 the pencil, §15.3 cracks), §13.3 (B8, K11, K12), §10.7 (K14), §31.3 and §31.4 (K11's known points, K14). Background: TDD_14 (the region), TDD_16 (the two halves), TDD_17 (the exact exit line).

### Stage Status

| Phase | Topic | Status |
|-------|-------|--------|
| 113.0 | Oracle and fixtures: two playgrounds (`projective_inversion`, `directional_degenerate`), the labels with clicks and projective cursors (43 cases), K11, K12 and K14 in the oracle (`test_stage113.py`, `stage113_exact_hits.json`), §13.3 amended | Done |
| 113.1 | The `Pencil` parameter behind today's direction arithmetic, per-cell winding, bit-identical (`regions.json` unchanged) | Done |
| 113.2 | The parallel pencils (line normal, line directional, circle directional) | Done |
| 113.3 | The radial pencil (circle normal): two sheets, the fork line as two members | Done |
| 113.4 | Compositions: a mirror or an inversion after the projective, before it, two projectives, projective-first plans (two halves) | Done |
| 113.5 | GUT: the visibility sweep without its projective filter plus a projective sweep; the native level loops with projective plans; the Stage 113 GUT test (`tests/test_stage113_projective_visibility.gd`); the seven new sweep manifests | Done |
| 113.6 | Goldens: `regions.json` grown (26 projective regions, 12 for the new dumps; no existing hash changed); the K11 sweep and corpus goldens and K14's frame-id relabelling judged (`make judge RULES=stage113`: 22 exact-exit combos, 5702 frame-id combos, 42 corpus fields, 0 findings) -- re-blessed with the user's approval (2026-10-19), B11's four digests too; docs | Done |
| 113.7 | Interactive sign-off (user, 2026-10-19) | Done |

**Workflow:** branch `projective-visibility`, `make gate` per commit, `make gates` per phase (docs/WORKFLOW.md).

## Design decisions

**One pencil per epoch, cast-driven as today.** After a projective at `H` the beam is straight: `p' = H`, `τ' = ±axis(H)`, `κ' = 0` (`BeamEffects::project`). In the last epoch's own frame the members are lines, one per hit point on the projective's carrier; the epoch's transformatives map them to the final frame. For a line normal and the directionals they are all parallel (a *parabolic* pencil: one pole, the frame's infinity `B = F'(∞)`); for a circle normal they all pass the centre (an *elliptic* pencil with poles `A = F'(c)` and `B`, the shape of Stage 110's `P_K`/`Q_K` pencil with `A` as `P_K`). `Builder::PencilKind` (Aim, Parallel, Radial) selects the arithmetic the construction makes on member positions (`Param`: the aim direction, or a real key -- an offset across the family's direction, or the line's folded diamond pseudo-angle), and everything else (`collect_members`' dedup, the side casts, `refine`, `build_cells`) runs on that interface. The aim pencil wraps today's functions unchanged (113.1 was bit-identical).

**The members through a cursor and through a point of interest.** A cursor's member is the line through its back-propagated hit point (`key_of`: pull the cursor back to the epoch's frame, `BeamEffects::back_propagate` with the solver's own `x_on_carrier` rule, the key of the hit). The member's beam is built from its key's hit point along the exit (`member_from_key`), so that every cast of a member reads one walk: the candidate of a circle is a property of the line (the pair `c ± R·u` is the same for every cursor on a radial line; every chord of a directional circle shares one perpendicular bisector). A point of interest exactly on the projective's carrier aims at the other candidate (B6): its own cast decides a cursor exactly there (`Member.poi_ok`), and the member's walk is cast from a cursor along its exit (`cursor_at`). A member through the frame's finite infinity (the epoch's line through an inversion's centre) is built exactly straight among circles.

**Critical members.** The last epoch's own points of interest as in Stage 110 (endpoints, tangencies computed on the carrier pulled back to the epoch frame, crossings, piece ends); the earlier epochs' points pushed through the projectives (`keys_from_point`, `keys_from_hit`: the earlier epoch's member through the point, mapped to the frame of the next projective, cut by its carrier, and so on); the fork line of a circle normal, as two members with their own candidate (`Member.sigma`, `sub`: the sheet of the keys just below it, then the other); the tangency points of a directional's normal on its circle; the axes of the radial pencil; the domain's extent for a full-line carrier. One line's two sheets never merge (`same_point`).

**One walk owns its whole member.** There is no opposite aim: the walk from `H` runs along the exit, through the frame's infinity, and round to `H`. `intervals` drops the owned-arc filter and takes the pencil's poles as breakpoints; the walk order along a member starts at its departure parameter (`Member.t0`, a computed value: parameters within rounding of it at the coordinates' scale are the start, `at_start`; the native sweep found a refinement member whose near vertex sat 3e-9 px from its start and wrapped to the far end of the loop, a pinch at the centre), and the member's own walk is the one that fired the projective at the member's hit point -- a cast whose planned walk fired the entry at another crossing left the plan there (GAME_SPEC §13.3) and runs another line: not this member's (`cast_core`'s hit-point check).

**Two sheets.** On the radial pencil a cell before the centre (the walk converging toward `A`) and a cell beyond it (or beyond the wrap through `B`) wind opposite ways. Cells carry `wind` (`wind_at`: the exit against the radius, or against the family's direction, times the parity of the last epoch's reflections and inversions -- never the first epoch's realization), `clip_loop` and `chain` take it, `merge_cells` merges per winding, VIS-ORIENTED and VIS-DISJOINT run per `(half, wind)` part (`parts_of`, `part_of`). Gaps narrower than the side casts resolve (`8 · p_eps_max`) are no cell, and members that close are one.

**Cracks.** A dead member between two live cells -- the aim exactly tangent to the projective's circle at the member's hit point, say -- is a slit of no width the walks on it never reach: its cancelled pieces stay as a boundary, one edge each way, never inclusive (`Edge.crack`); VIS-SIMPLE, VIS-DISJOINT and VIS-REGULAR let a crack's two edges lie on each other. A shape switch that lies closer to a bounding member than the members resolve (between it and its side cast) is not a cell's inconsistency (`refine` returns -1, no note).

**The casts' frames.** `cast_events` gives the projective's own event the epoch it starts (as `Tracer::event_frames` reads it): before, the first event after a projective departure looked like a frame change, a stop.

**What the oracle decided on the way (K11, K12).** The labels' exact-data cursors (an arc's endpoints and via, the radials through them) disagreed between the oracle and the native walker: at an endpoint the oracle fired the projective physically (its hit point `c + R·u` computed a rounding off the endpoint), the native did not (by arithmetic luck: the radii of the test arcs are axis-aligned). GAME_SPEC §11.1 blocks an endpoint on one side only, so the physical hit must not fire; the identity of the hit point with a data point is now an exact predicate on the data (`_exact_data_hit`, `Solver::exact_data_hit`): the foot of the target on the line, a data point collinear with the target and the exact centre, a point `T + s·n`. Such a hit point is exact data: the aim image behind it stays exact (`exact_image` runs through it), its exit line is exact, and it is a known point of the beams whose exact line or circle passes it. K12: a cursor exactly on a line projective's segment is its own hit point; the native chain built a zero-length sub-chain for it and never reached the cursor; now the sub-chain is the exit beam alone.

**K14: an epoch is its firings.** The native sweep on the mixed playground (`projective` [9 R]) found cursors the solver "reached" along line 6's carrier, which runs through arc 9's centre and is the fork line of the default click: at the crossing of the two carriers the physical walk fires 6 and the planned walk 9, and both frames read as "epoch 1, empty stack". The spec's words were already "the same sequence of applied effects"; the frame key now carries the firings (surface and side) in place of the count, in the oracle (`_frames_after`), the tracer (`firing_key`, `frame_key`, `frame_id`) and the cast (`cast_events`). The solver's `epoch` counter and the goldens' events are untouched; the labels gain one case. The sweep goldens hash the display canon, which carries each step's interned frame id: a pair whose walks fire different projectives (the physical one 5 then 6, the planned one 6, say) interned one key per epoch count before and one per firing sequence now, so 5702 combos change their hash with identical events, points and tiers. `tools/judge.sh` now prints the reference's canon of the changed combos too (`old_canon.raw`: the reference binary run against a goldens copy with blank hashes in its worktree, restored after), and the rule classes a combo `frame-id` only when the two canons differ in the frame-id field alone (`docs/research/stage113_canon_diff.py`).

## What Is Introduced

- `native/core/include/rc/visibility.hpp`: `Param`, `Member.key/sub/sigma/crack/t0/poi_ok`, `Result.pk_none/pencil`, `Cell.wind`/`Loop.wind`, `Builder::PencilKind` and the `p_*` operations, `prepare_projective`, `backward_to`/`pull`/`push`, `hit_of`/`exit_at`/`key_of_hit`/`key_of`/`hit_at_key`/`cursor_at`/`member_from_key`/`member_through`/`wind_at`, `keys_from_hit`/`keys_from_point`, `epoch_carrier`/`epoch_tangent_points`, `collect_projective_members`, `merge_cells_of`, `part_of`/`parts_of`, `Edge.crack`, `at_start`, `RC_VIS_MERGE`.
- `native/core/include/rc/visibility_checks.hpp`: the checks per `(half, wind)` part, the crack exemptions, the pole guards, VIS-ITERATION-CHAIN's tolerance at a tangency departure, `pin_tag` (K13).
- `native/core/include/rc/solver.hpp`: `exact_step_h`, `exact_data_hit`, `backward_pass` (`Backward`), `AnchorChain.exact_hits`, `exact_hits_on_beam`, exit lines for every exact projective, the K12 sub-chain, `initial_line` through exact projectives.
- `native/core/include/rc/beam_effects.hpp`: unchanged (`back_propagate` is the pencil's primitive).
- Oracle: `trace2._exact_step`, `_exact_data_hit`, `_backward` (exact tracking), `projective_hits` (a triple: `H`, exact exit line, exact-data `H`), `exact_image(..., hints)`; `gen_fixtures_stage110_visibility.py` (hints, `projective_cursors`, `best_case`, the projective cases), `gen_fixtures_stage113_exact_hits.py`, `test_stage113.py`, `stage113_judge.py`, `stage113_canon_diff.py`; `tools/judge.sh` (the reference's canon).
- Scenes: `scenes/playground/projective_inversion.tscn` (a mirror, an inversion circle, a line normal and a circle normal, back), `directional_degenerate.tscn`; their dumps; the corpus scenarios of both.
- Tests: `native/tests/test_exact_hits.cpp`; `test_visibility.cpp` (the labels through `plan_from_json`, every case's region required to build, the level loops' projective plans, the debug case's keys/sides/windings/cracks and the pin-edge print); `tests/test_visibility_sweep.gd` (`test_visibility_projective_plans`); `scripts/testing/sweep_plans.gd` (`projective_playground_scenes`, `projective_composition_plans`).
- GAME_SPEC §13.3, §15, §29.3 (VIS_REGULAR), §31.3, §31.4; CHANGELOG 2.25.0.

## Invariants

Unchanged in meaning: the pin (VISIBILITY-EQUALS-NON-DIVERGENCE) and every VIS-* check of TDD_14. Adapted: VIS-ORIENTED and VIS-DISJOINT per `(half, wind)`; VIS-EDGE-ON-GEOMETRY's pole test skips an absent `P` pole; VIS-SIMPLE, VIS-DISJOINT and VIS-REGULAR allow a crack's two edges; VIS-ITERATION-CHAIN tolerates a departure at a tangency of the previous region's member a rounding off its boundary; VIS-CELL-CONSTANT probes by the pencil's key (`probe_between`); `pin_tag` names K13.

## Unit Tests Added

| File | Covers |
|---|---|
| `docs/research/oracle/test_stage113.py` | K11 at an arc endpoint whose radius is not representable (the mixed playground's arc 9), the exact image through it, a line normal's via, a directional arc's tangency endpoint; K12; K14 (two projectives fired at one point) |
| `bench/fixtures/stage113_exact_hits.json`, `native/tests/test_exact_hits.cpp` | 10 fixtures (through and back arcs, a line endpoint, a directional tangency, the cursor at the departure) at f64, Dual, 4 and 64 limbs; the exact image through an exact hit point |
| `bench/fixtures/stage110_visibility.json` (43 cases, ~6,400 cursors) | every kind, both sides, inside and outside, the forks by click, the degenerate directional, a mirror or an inversion before and after a projective, two projectives, cursors on the segments and at the centre |
| `native/tests/test_visibility.cpp` level loops and digest | every projective side of the five projective playgrounds as a plan, the first two projectives as one, players (500, 300) and (960, 540): the pin, the structure checks, the digest |
| `tests/test_visibility_sweep.gd` | the projective playgrounds: the empty plan, every projective side, the first two, each with the scene's first transformative after and before it |
| `tests/test_stage113_projective_visibility.gd` | B9's load rejection, B7's click snap, the concentric inversion's region; through the binding on hand-built rooms: a line normal's band of parallel rays pinned against the preview on a grid; the circle normal fork moved by the click (both regions pinned); the degenerate directional's supported empty region; the renderer recomputes when a plan entry's click changes (`projective_arcs`) |

## B-list (behaviour changes)

| # | Change | Evidence | Status |
|---|---|---|---|
| B6 | A bare cursor on a circle projective's carrier aims at the other candidate (`x_on_carrier`); the region follows the solver at its points of interest only: a carrier point that is no data point sits in a live cell while the solver aims away from it (`projective_concentric` [6 R, 7 L], cursor (1020, 460)) | the labels at the arcs' endpoints and via; the generator skips the bare carrier cursors (`bare_carrier_cursor`, 21 of them) | Kept (user, 2026-10-18); superseded by Stage 114's fork rule (TDD_19): the target on the carrier is its own candidate, exactly, and the labels' filter is gone |
| B7 | A click exactly on the diameter across a directional circle's normal: float32 noise picks the chord end per member | the UI moves a click within 8 px of that diameter off it, to the via's side (`PlanManager.snap_click`, `test_b7_...`); the solver's default click (the via) is untouched | Done (follow-up, 2026-10-19) |
| B8 | §13.3's segment bound is not enforced: an off-segment hit point is a planned-only firing | no fixture or golden changed | Spec amended (user, 2026-10-18) |
| B9 | A directional normal along its line carrier: a supported empty region, and rejected at load (§23, `EffectSpec.validate` with the carrier's direction) | the former playground's dump kept as `bench/fixtures/stage113_degenerate_directional.json`: the native and the oracle pin its empty region (`test_b9_...`); its labels case, corpus scenarios, manifest and digests dropped (none changed) | Done (follow-up, 2026-10-19) |
| — | An inversion centred on the circle normal's centre after it (`A = F'(c) = ∞`) | supported: the pencil's poles swap (`prepare_projective`); the `projective_concentric` playground (labels, corpus, manifest, 10 digests, the GUT test) | Done (follow-up, 2026-10-19) |
| B11 | A tiny carrier arc whose via rounds onto an end reads as the whole circle (`contains`, the fill): the carrier edge takes the short arc's middle as its via, for the aim pencil too | four Stage 110 digests re-blessed (`crossings` [19 L], `circle_no_walls` [1 L], `inversion_grid` [1 L], `full_circle_mirror` [5 L]; their pins and structure checks clean) | Done (approved by the user, 2026-10-19) |
| K14 | An epoch is identified by its projective firings, not their count | `test_k14_...` (oracle), the labels case `projective_9R_fork_on_line_6`, the native sweep `projective__1` [9 R] (0 violations); the judge: 5702 sweep combos whose display canon differs in the steps' frame ids alone (class `frame-id`, verified against the reference's canon by `stage113_canon_diff.py`), no event, point or tier changed | Done (re-blessed, approved by the user 2026-10-19) |
| K11, K12 | see the design decisions | `stage113_exact_hits.json`; the judge: 22 sweep combos and 42 corpus fields, all `exact-exit` (same decisions, the ladder settling at f64 where it climbed to 4 limbs, points within 1e-6 px) | Done (re-blessed, approved by the user 2026-10-19) |

## Known findings

| # | Finding | Evidence | Status |
|---|---|---|---|
| K15 | A beam through an inversion's centre (the concentric playground: a circle normal's exit beam passes its centre by provenance, and the inversion 6 is centred there; plan [7 L, 6 L, 5 L], player (1100, 700), cursor (960, 840)): the walker inverts the rounded beam into a circle of enormous radius whose wrap overflows, and the solve returns nothing where the oracle traces (at 50 digits the oracle's circle is as good as the line). | `test_visibility.cpp` "K15" (doctest `may_fail`); the labels keep the two concentric cases both sides agree on | Open: provenance (the exit beam passes the centre; an inversion centred there keeps the line, the centre becoming the frame's infinity), oracle first, with the fork-rule change |
| K13 | Rounding cracks on a projective region: (a) a cursor exactly on a member tangent to a carrier beyond the touch (`projective_inversion` [7 L, 6 L], player (960, 480), cursor (1320, 720): the member's circle touches the bottom wall at (1200, 840) and the member's own walk fires there, the cursor's own beam a rounding off it passes); (b) the aim's tangency with a circle normal's circle (`projective_arcs` [6 L], player (1250, 300): the walks just below the via switch between the solver's forced orientations within 5e-6 of the key). | `pin_tag` tags (a); (b) is a sliver no cell holds. | Resolved in Stage 114's follow-up (TDD_19, "K13"): the boundary band of GAME_SPEC 15.4, the members placed at the solver's switches, the K13 test beside every member and around every key of six projective regions |

## Performance

`visibility bench` (12 player positions each, the median and the casts per region):

| Scene | Plan | Median | Max | Casts per region |
|---|---|---|---|---|
| `projective_arcs` | [5 L] (a circle normal, through) | 14.6 ms | 31 ms | 278 |
| `projective_arcs` | [6 R] (back, from inside) | 9.3 ms | 14 ms | 269 |
| `projective_arcs` | [5 L, 6 L] (two circle normals) | 23.7 ms | 34 ms | 388 |
| `projective_lines` | [5 L] (a line normal) | 24.6 ms | 39 ms | 132 |
| `projective_lines` | [5 L, 6 L] (two line normals) | 22.6 ms | 48 ms | 126 |
| `projective_inversion` | [7 L] (a line normal, back) | 26.2 ms | 42 ms | 208 |
| `projective_inversion` | [7 L, 8 L] (a line normal, then a circle normal) | 29.6 ms | 109 ms | 275 |
| `projective_inversion` | [6 L, 5 L] (the inversion, then the mirror: no projective) | 52.1 ms | 111 ms | 1,330 |

A single projective costs what a one-mirror plan did in Stage 110 (15-27 ms); a projective-first plan on a circle carrier builds two halves. The region is computed on the worker thread as before; the cursor never moves it.

## Interactive User Tests

1. **A band of parallel rays:** `scenes/playground/projective_lines.tscn`, plan [5 L] from the right of the line: the region is the band of rays from the lit piece of the line, its edges through the segment's ends; [6 R] the same for the back line.
2. **An expanding fan:** `projective_arcs.tscn`, plan [6 L] from above the arc (the back arc): a fan from the lit part of the arc outward to the walls; the region ends at the vertical through the top endpoint (the fork line of the default click).
3. **A converging fan:** `projective_arcs.tscn`, plan [5 L] from above: the fan converges through the centre and spreads again beyond it to the far wall, both sheets drawn.
4. **A click moves the region:** on `projective_arcs.tscn` with [5 L], clicking the top or the bottom of the arc changes which half of the arc the region hangs on.
5. **Directional arcs:** `directional_arcs.tscn`, plan [5 L]: the rays flip across the arc's top and bottom.
6. **Compositions:** `projective_inversion.tscn`: [7 L] then the mirror 5 ([7 L, 5 L]) reflects the band; [7 L, 6 L] bends it through the inversion circle; [6 L, 7 L] from inside the circle.
7. **Two projectives:** `projective.tscn`, plan [11 R, 6 R] from (300, 230): the band above the portals through both lines.
8. **The degenerate directional:** `directional_degenerate.tscn`, any plan through 5: nothing is drawn, the preview never reaches the cursor.
9. **The promise:** anywhere in the white the preview is all green to the cursor; just outside, red or yellow.
10. **Live update, V, layering, seams:** as TDD_14's tests 2, 3, 10 and 11, on the projective playgrounds.

## Regression Checklist

The full GUT suite, ctest (RelWithDebInfo and ASan), the oracle tests, the level parity, the goldens (re-blessed with the user's approval; otherwise byte-identical), the region digest (26 regions added, none changed).

## Files Modified

See the stage's commits on `projective-visibility`.
