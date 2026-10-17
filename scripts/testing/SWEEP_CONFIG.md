# Invariant sweeps

What the suite sweeps (Stages 103, 105). Every sweep runs natively (`SweepKernel`,
`rc/sweep.hpp`): positions × positions × plans, each combo traced by the
tracer the game uses and checked by the invariant kernel (`rc/invariants.hpp`);
one combo per scene also runs through the live `PathRenderer` and
`InvariantChecker` ("LIVE"). `SWEEP_SHARD=i/n` keeps the player positions
with index % n == i (`run_tests_parallel.sh` shards each sweep file over every
core, the visibility sweep into twice as many shards, longest first). Scenes,
plans and positions come from `SweepPlans`.

| Test | Scenes | Plans | Positions |
|---|---|---|---|
| `test_invariant_sweep.gd::test_sweep_all_scenes` | every `scenes/test_levels` scene | none | grid 5×5, 10 fuzz, points of interest |
| `::test_sweep_with_plans` | `SweepPlans.plan_scenes()` (8 mirror / inversion levels) | `reflective_plans` + `inversion_plans` | grid 5×5, 10 fuzz, points of interest |
| `::test_sweep_projective_plans` | `projective_scenes()` (5 projective playgrounds) | `projective_plans` | grid 4×4, 6 fuzz |
| `::test_sweep_playground_plans` | `playground_plan_scenes()`: plannable_inversion, portals, two_sided, pre_inverted, tangent_infinity | none + `scene_plans` (mirrors, inversions, portals, projectives) | grid 4×4, 6 fuzz, in pre-transform scenes `q0_positions` (aligned with Q0: exact grazes at the frame's infinity point, Stage 105), and `on_surface_positions` (the four extreme points of every full circle: pops at known points, Stage 106) |
| `::test_sweep_infinity_plans` | the plan scenes and those playgrounds | `infinity_plans`: [∞], [∞, ∞], [∞, X], [X, ∞] | grid 4×4, 6 fuzz, `q0_positions`, `on_surface_positions` |
| `test_visibility_sweep.gd` (Stage 110) | every test level and playground (no plan); the plan scenes; the playgrounds' plans; infinity plans | none, `reflective_plans` + `inversion_plans`, `scene_plans`, `infinity_plans` | grid 4×4, 6 fuzz, `q0_positions`, `on_surface_positions`; each player's visibility region, its VIS-* checks (reported at the player), and VISIBILITY-EQUALS-NON-DIVERGENCE at every cursor |
| `test_invariant_combos.gd` | `combo_base` with generated surface combinations; every level for segment intersections (pre-transform circles are not segments) | none | per test |
| `test_precision_invariant_sweep.gd` | `inversion_grid` | `precision_plans` (up to 7 entries) | 10 seeded positions outside the circles |

The sweeps' traces at tag `pre-native` are pinned in `bench/goldens/sweeps.json`
(284,096 combos, replayed bit for bit by `native/tests/test_goldens.cpp` with
the infinity switch off; `RC_SWEEP_REBLESS=1` prints the replacement of a
combo an approved behaviour change alters, `RC_GOLDEN_REBLESS=1` the same for
the fixture and corpus goldens, and `docs/research/apply_rebless.py` applies
those lines; `RC_SWEEP_DUMP=<list>` dumps listed combos with every event's
beam for `docs/research/stage106_judge.py`, the independent judge of changed
traces).

**Carrier cursors** (Stage 110): the plan sweeps (`test_invariant_sweep.gd`'s
`_sweep_scenes`, and the visibility sweep) also put cursors on the planned
surfaces' carriers beyond their segments (`sweep::carrier_extension_cursors`:
S + k (E - S), k in {-1, -1/2, -1/8, 9/8, 3/2, 2}, exact in float32), where a
walk departs from a planned surface crossed off its segment. Cursor-only: the
player positions, and so the goldens' inputs, are unchanged.

**Known findings** (`KNOWN_FINDINGS` in `tests/test_invariant_sweep.gd` and
`tests/test_visibility_sweep.gd`: reported, not failed; the sweep fails if one
stops occurring). K1 was resolved by Stage 105, K2 and K3 by Stage 106
(docs/TDD_10_NATIVE_MIGRATION.md), K8 by Stage 110's exact member cursors.
Open (docs/TDD_14_VISIBILITY.md): K7 (visibility, measure zero: a cursor
exactly on a planned carrier) and K9 (pre_inverted, plan [2 L, 3 L, 4 R], a
carrier cursor: DIRECTION-ONLY and PHYSICS-COMPLIANCE).

The invariants are the checks of `rc/invariants.hpp` (`check_all`): UX7,
PREVIEW-NOGAPS, S16, GREEN-FROM-PLAYER, ORIGIN-NOT-REHIT, SINGLE-DIVERGENCE,
NO-POST-CURSOR-DIVERGENCE (Stage 110: the cursor is the planned path's
cursor_index, and the check holds when both walks leave the cursor's point in
one frame), PHYSICAL-PREVIEW-MATCH, PHYSICAL-CONTINUITY,
SOLID-PATH-TO-CURSOR, TRACE-ENDS-AT-SURFACE-OR-BOUNDS, PLAN-EFFECTS-APPLIED,
PHYSICS-COMPLIANCE, HITPOINT-ON-CARRIER, ON-SEGMENT-CONSISTENCY, VIA-ON-ARC,
VISUAL-ON-CARRIER, DIRECTION-ONLY, and the event checks EFFECT-LAW,
EVENT-ON-BEAM, BEAM-ORDER, REAPPLY-IN-PLACE (Stage 106: no zero-length loop).

The visibility sweep's checks are the region's (`rc/visibility_checks.hpp`,
`inv::check_visibility`): VIS-REGULAR, VIS-CLOSED, VIS-SIMPLE, VIS-ORIENTED,
VIS-DISJOINT, VIS-FINITE, VIS-EDGE-ON-GEOMETRY, VIS-VERTEX-PROVENANCE,
VIS-SHADOW-ANCHORED, VIS-NO-BLOCKER-INSIDE, VIS-SOURCE-ON-BOUNDARY, VIS-STAR,
VIS-CELL-CONSTANT, VIS-ITERATION-CHAIN, VIS-BOUNDARY-DECIDED,
VIS-CONTAINS-MATCHES-POLYGON, VIS-DRAWN-MATCHES, VIS-DETERMINISTIC; and per
cursor VISIBILITY-EQUALS-NON-DIVERGENCE.
