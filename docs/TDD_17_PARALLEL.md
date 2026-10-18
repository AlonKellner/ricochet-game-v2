# TDD Document 17: An Exact Line Never Meets a Parallel Carrier

**Stage 112** | K10 of Stage 111: a beam that carries an exact line decides its crossings with line carriers on that line, not on the tier's rounded tangent. Spec: GAME_SPEC §31.3 (the parallel-carriers rule), §13.3 (a projective's exit line).

### Stage Status

| Phase | Topic | Status |
|-------|-------|--------|
| 112.0 | Oracle: `trace2._parallel` in the candidate collection, beside the exact tangency; `test_trace2` (the rule, the three K10 scenes at three precisions); `gen_fixtures_stage112_parallel.py` → `bench/fixtures/stage112_parallel.json` (6 fixtures) | Done |
| 112.1 | Native: `exact::Line::parallel_to` (float bound, then exact), `Solver::parallel` cached per exact line like the tangencies, used in `candidates`; `test_exact_lines.cpp` (the fixtures at f64, Dual, 4 and 64 limbs; the predicate) | Done |
| 112.2 | Goldens: `make judge RULES=stage112`: no sweep combo, fixture, corpus or region changed (the phantom crossings never occurred in committed data); nothing to re-bless | Done |
| 112.3 | Docs: GAME_SPEC §31.3, CHANGELOG 2.24.0, TDD_16 K10 resolved | Done |

**Workflow:** branch `exact-subchains`, `make gate` before every commit, `make gates` before the merge (`docs/WORKFLOW.md`).

## The finding

`bench/fixtures/stage87_projective_plans.json` `mixed_chain` with the plan [1 L, 2 R, 3 R] (the generator skipped it: the far side of mirror 1 is reached the long way round): the planned chain is built backwards from the cursor, and the projective's exit point is the foot of the cursor's unfolded image on the carrier, `s + D·(dot(X′−s, D) / |D|²)`; with `D = (380, 0)` the quotient is not a binary fraction and the foot lands at `250 + 2⁻⁴⁵`. The beam after the reflection at 3 is built from two points an ulp apart over 500 px: its tangent is `(1.28·2⁻⁵⁴, 1)` instead of `(0, 1)`. Heading to the wrap through infinity it met the vertical walls at `y ≈ 2·10¹⁸` (at 4 limbs: `10³⁷`), three off-segment pass events the oracle does not have. The ladder climbs but cannot remove a rounding that happens at every tier.

The chain already carried the projective's exact exit line (`AnchorChain::exit_lines`, the normal through the exact image), and the walker set it on the beam (`w.line`, reflected exactly at 3); the exact line was only consulted for tangencies, concurrencies and known points. The oracle had the same gap, and showed it on a circle normal (its radial exit, a unit vector with a square root, carries a `10⁻⁸⁰` tilt).

## The rule

A beam with an exact line never meets a line carrier parallel to it: the crossing exists iff `a·Δx + b·Δy ≠ 0` with `(a, b)` the line's exact coefficients and `(Δx, Δy)` the segment's float64 endpoint difference. Natively a float bound selects the path (`|fa·Δx + fb·Δy| > ea·|Δx| + eb·|Δy| + rounding`: certainly not parallel) and the exact BigFloat sign decides the rest; in the oracle the exact rationals decide outright. The predicate is evaluated once per exact line for every line carrier (`Walk::parallel`), like the tangencies.

## Evidence

- The K10 trace: 30 → 27 events natively at every tier, equal to the oracle's 27 (the ladder no longer climbs: tier 0 agrees).
- `stage112_parallel.json`: the K10 scene through a line normal, a circle normal (the full circle centred (250, 240), met first at its bottom, left along the radial through its centre) and a directional projective, planned and physical; stable across the oracle's precisions 60, 80, 120 (a tie-rich arc variant was not, and was dropped).
- The judge (112.2): `make judge RULES=stage112` against main: 0 changed sweep combos, 0 changed golden fields; `make regen`: every fixture and scenario identical. The rule removes events only where a rounded tangent met a parallel carrier before the wrap, and no committed trace had one.

## Known findings

None.
