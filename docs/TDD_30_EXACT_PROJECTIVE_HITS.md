# TDD 30: Exact projective hit points; the cast count measured (Stage 125)

The sixth stage of the smooth-frames program. Stage 124 left two findings open (TDD_29), and the user asked to
take them on together (2026-10-21): "Proceed with the number of casts and projective hit points at the same
time".

- **F14, the number of casts.** About 1400 casts per region on crossings with a plan.
- **F15, a projective's planned hit point.** H is exact data only when it is a data point of its segment
  (K11), so a tie there still climbed. projective_lines had 1212 climbs per 120 regions, 84% of its region
  time.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 125.0 | F14: side casts spared where a member's walk passes no point of interest -- tried, measured, reverted | Done (no change) |
| 125.1 | F15: K11 extended -- a projective's hit point computed exactly is exact data when its coordinates are floats exactly (spec, oracle, native; re-blessed, approved) | Done |
| 125.2 | Docs, `make gates`, sign-off | In progress |

## 125.0 The number of casts (F14): a negative result

Half of a region's casts are side casts: each member is cast just beside itself on both sides (±ε), to see
what changes there. A side cast can differ from its member's own cast only when the member's walk passes the
member's point of interest, since nothing else can change within ε. The test was simple: a walk with no event
on the point's surfaces at the point means the sides are the member's own shape at their parameters, so they
can be copied instead of cast. Tangencies (no event marks them) and members of several points kept their side
casts.

Measured, it spared 9% of side casts on crossings and none on inversion_grid. Each member's own cast aims
exactly at its point, so its walk reaches the point whenever the point is visible, and then the sides are
genuinely needed. The change was reverted. Fewer casts would need a different construction: which members can
matter at all.

## 125.1 Exact projective hit points (F15)

**The rule (GAME_SPEC 13.3, 31.4).** With no data point the hit point, H is computed exactly from the exact
target. For a line normal this is the foot of the target on its line; for a directional on a line carrier, the
crossing of the target's direction line with the carrier. Both are rationals. When both of H's coordinates are
binary floats exactly, H is exact data, as a data point would be:

- it is the hit point exactly;
- it is a known point of the beams through it;
- the image behind it stays exact.

A circle carrier is not extended, because its hit point is a root. A typical case: any cursor straight across
an axis-aligned line normal, whose foot `(750, cy)` is always floats.

**Spec first, then the oracle and native.**

- The oracle: `trace2._exact_float_hit` in rationals, `float(q) == q`. Five tests in `test_stage125.py`. The
  K11 control in `test_stage113.py` changed meaning: the foot (750, 541) is now exact data.
- Native: `Solver::exact_float_hit` in homogeneous BigFloats. The coordinate is BigFloat's truncated
  quotient, exact when the point is floats, then checked exactly.
- The regenerated oracle outputs changed one corpus scenario's planned walk (`playground__projective__08`),
  and native agrees on every corpus scenario.

**Judged and approved** (the user, 2026-10-21):

- **3843 sweep combos** on projective plans, all with the same aim. 230 planned walks no longer climb (206 of
  them had climbed to 64 limbs).
- **154 of them have a new event structure,** each decided by identity instead of rounding:
  - ties at H (a carrier through the firing point is no separate event);
  - a corner's id order (walls 1 and 4 at (560, 240));
  - a near-horizontal artefact gone: the beam is exactly horizontal, where it used to "hit" line 1 at
    x ≈ −5.8·10³⁵.
- **60 corpus and 19 fixture fields;** **one region** (two via points, 5·10⁻¹⁴ px).
- **44 GUT trace rows** (88 step coordinates, one ulp each). The GDScript game that recorded them is retired,
  so they are re-recorded natively: `RC_GUT_TRACES_WRITE=<dir>` (`test_gut_traces.cpp`).
- Sessions unchanged; the sweeps' invariants hold unsampled.

## Results

A region with a plan while walking (ms, 8 threads; the stage tool of TDD_29):

| Level | Stage 124 | Stage 125 |
|---|---|---|
| projective_lines | 7.7 | 2.1 |
| directional_lines | 7.5 | 1.9 |
| crossings | 17.6 | 17.9 |
| inversion_grid | 10.8 | 11.1 |
| hit_effects | 10.9 | 11.1 |
| projective_concentric | 8.8 | 9.7 |
| two_sided | 7.2 | 7.7 |
| semi_circle_mirror | 7.1 | 7.4 |
| full_circle_mirror, ghost_room, portals, projective_arcs | 4.8–6.3 | 5.0–5.9 |

The rows other than the two projective levels are unchanged within the tool's noise. Ten levels remain over
the 4 ms budget with a plan; those regions are smoothed only when they are slower than 30 Hz (Stage 124.6).

## Findings

| # | Finding | Status |
|---|---|---|
| F14 (TDD_29) | The number of casts | Measured (125.0): side casts are needed where they are made; fewer casts need a different member construction. Open |
| F15 (TDD_29) | A projective's hit point exact only as a data point | Resolved (125.1) for line carriers; circle carriers (roots) stay |

## Tests

| Test | What it checks |
|---|---|
| `test_stage125.py` (oracle) | an axis-aligned foot is exact data; a foot between floats is not; a directional's crossing; circles not extended; the backward pass keeps the image exact through such a hit |
| `corpus: every scene corpus scenario matches the oracle through the ladder` | native and the oracle agree on every corpus scenario after the rule |
| `gut traces: ...` | the 3600 GUT rows bit for bit (44 re-recorded, approved) |
| the sweeps, the goldens, the region digests | judged and approved |
