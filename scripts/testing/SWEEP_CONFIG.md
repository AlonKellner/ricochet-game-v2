# Invariant sweeps

What the suite sweeps (Stage 103). Every sweep runs natively (`SweepKernel`,
`rc/sweep.hpp`): positions × positions × plans, each combo traced by the
tracer the game uses and checked by the invariant kernel (`rc/invariants.hpp`);
one combo per scene also runs through the live `PathRenderer` and
`InvariantChecker` ("LIVE"). `SWEEP_SHARD=i/n` keeps the player positions
with index % n == i (`run_tests_parallel.sh` shards each sweep file over the
performance cores). Scenes, plans and positions come from `SweepPlans`.

| Test | Scenes | Plans | Positions |
|---|---|---|---|
| `test_invariant_sweep.gd::test_sweep_all_scenes` | every `scenes/test_levels` scene | none | grid 5×5, 10 fuzz, points of interest |
| `::test_sweep_with_plans` | `SweepPlans.plan_scenes()` (8 mirror / inversion levels) | `reflective_plans` + `inversion_plans` | grid 5×5, 10 fuzz, points of interest |
| `::test_sweep_projective_plans` | `projective_scenes()` (5 projective playgrounds) | `projective_plans` | grid 4×4, 6 fuzz |
| `::test_sweep_playground_plans` | `playground_plan_scenes()`: plannable_inversion, portals, two_sided, pre_inverted | none + `scene_plans` (mirrors, inversions, portals, projectives) | grid 4×4, 6 fuzz |
| `::test_sweep_infinity_plans` | the plan scenes and those playgrounds | `infinity_plans`: [∞], [∞, ∞], [∞, X], [X, ∞] | grid 4×4, 6 fuzz |
| `test_invariant_combos.gd` | `combo_base` with generated surface combinations; every level for segment intersections | none | per test |
| `test_precision_invariant_sweep.gd` | `inversion_grid` | `precision_plans` (up to 7 entries) | 10 seeded positions outside the circles |

The sweeps' traces at tag `pre-native` are pinned in `bench/goldens/sweeps.json`
(284,096 combos, replayed bit for bit by `native/tests/test_goldens.cpp` with
the infinity switch off; `RC_SWEEP_REBLESS=1` prints the replacement of a
combo an approved B-item changes).

**Known findings** (reported, not failed; the sweep fails if one stops
occurring): K1, a tangency at the inverted frame's infinity point decided by
rounding on the portals playground (`KNOWN_FINDINGS` in
`tests/test_invariant_sweep.gd`, docs/TDD_10_NATIVE_MIGRATION.md).

The invariants are the checks of `rc/invariants.hpp` (`check_all`): UX7,
PREVIEW-NOGAPS, S16, GREEN-FROM-PLAYER, ORIGIN-NOT-REHIT, SINGLE-DIVERGENCE,
NO-POST-CURSOR-DIVERGENCE, PHYSICAL-PREVIEW-MATCH, PHYSICAL-CONTINUITY,
SOLID-PATH-TO-CURSOR, TRACE-ENDS-AT-SURFACE-OR-BOUNDS, PLAN-EFFECTS-APPLIED,
PHYSICS-COMPLIANCE, HITPOINT-ON-CARRIER, ON-SEGMENT-CONSISTENCY, VIA-ON-ARC,
VISUAL-ON-CARRIER, DIRECTION-ONLY, and the event checks EFFECT-LAW,
EVENT-ON-BEAM, BEAM-ORDER.
