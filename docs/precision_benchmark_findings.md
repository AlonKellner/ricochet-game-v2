# Trajectory Precision Under Deep Circle-Inversion Chains: Benchmark Findings

Goal: a trajectory computation that converges to the true solution, arbitrarily
closely given enough compute, including plans with 10+ consecutive circle
inversions (failure report: plan `7,4,1,3,6,6,6` in the 3x3 inversion grid).

**Result:** `BeamTracer` (cursor-anchored planned path + dynamic-precision
physical continuation + tangent-linear error estimate) matches an
arbitrary-precision oracle to ≤ 0.01 px on **200/200** benchmark scenarios
(plan depth 1–25, physical traces up to 32 hits, sensitivities up to 1e44),
median **4.3 ms** per frame on depth ≤ 15. The previous `Tracer` passes 6/160.

## 1. Root causes (why every previous strategy failed)

1. **All points were float32.** Godot's official build is single precision:
   every `Vector2` (hit points, ray origins, normalized segment endpoints, aim
   image, `Direction`) carries ~7 digits. Stage 77–79 only moved the Möbius
   *coefficients* to float64. The current tracer already loses circle 9 at
   depth 2 (verified from first principles, §6).
2. **The aim direction is the wrong unknown.** The sensitivity of the trace to
   the aim angle grows ~30–100× per inversion (oracle, px per radian):

   | plan depth | median | max |
   |---|---|---|
   | 4 | 2e9 | 1e11 |
   | 8 | 2e14 | 2e18 |
   | 10 | 3e16 | 5e21 |
   | 15 | 4e23 | 2e31 |
   | 25 | — | 1e44 |

   No float64 direction can reproduce a depth-10 plan by forward shooting; at
   depth 15 even double-double (106 bits) is not enough.
3. **But the planned path itself is well-conditioned** when anchored at the
   cursor instead of the direction (§3.2) — a boundary-value problem whose
   errors shrink going backward. Only the *physical continuation after the
   first divergence* is genuinely sensitive (an initial-value problem); that is
   where precision must scale.

## 2. Ground truth

`docs/research/oracle/` (Python, `mpmath`, venv at `docs/research/.venv`):

- **Visual-space model** (equivalent to `Tracer`'s normalized-frame semantics
  for full inversion circles + effect-free surfaces): the beam is a
  generalized circle that is inverted in the surface circle at every applied
  hit. Beam state `(p, τ, κ)`; local projective parametrization
  `z(t) = p + tτ/(1 − iκt/2)` reproduces the tracer's `t` order (wrap through ∞).
  Inversion at `h` on `(c, R)` (derived, verified to 1e-57):
  `τ' = −conj(τ_h)·((h−c)/R)²`, `κ' = −κ − 2·cross(τ_h, h−c)/R²`.
- Hits: `a₂t² − 2(q·τ)t + P = 0`, `q = c−p`, `P = |q|²−R²`, `a₂ = 1 − κ·cross(τ,q) + κ²P/4`.
- Side LEFT ⇔ the beam enters the circle; ordering and plan gating mirror `Tracer`.
- **Every plan is realizable** in exact planned-mode arithmetic (each beam
  passes through a point inside the next plan circle) — failures are purely
  numerical.
- Corpus: `bench/scenarios`, `bench/expected` (161: grid radii 30/60/90, depth
  1–15, 75% non-repeating chains, + the user's dump in decimal and float32
  form) and `bench/stress` (40: depth 16–25, 32-hit physical traces).
  120–200 digits, self-checked by recomputing at double precision.

## 3. Strategies

| ID | Strategy | Outcome |
|----|----------|---------|
| S0 | Current `Tracer` (normalized frame, float32 points) | planned 6/160, physical 4/160; 0/40 stress |
| S2 | Visual-space forward shooting, float64 | planned 66/160 (0/41 at depth 13–15) |
| S3 | Forward shooting on a precision ladder | needs 212 bits at depth 13–15 for planned |
| **S4** | **Cursor-anchored planned path** | **160/160 and 40/40 in plain float64, ~1e-13 px** |
| S5a | Single-shooting Newton on the aim angle | stalls (f64 θ) / lost at depth ≥14 (basin ~1/S) |
| **S5b** | **Multiple-shooting Newton (all hit points unknown)** | **1e-13 px in f64 at all depths; mixed precision → 1e-50 px** |
| S3+S4 | Anchored planned + ladder for the physical continuation (**selected**) | **200/200** |

### 3.1 Forward shooting (S2/S3)
Python prototype (`strategies.py`, `evaluate.py`) at 53/106/212/424 bits:
forward planned passes 66/160 → 157/160 → 160/160 → 160/160. Correct but the
precision requirement grows with depth, and the planned display would need
BigFloat every frame.

### 3.2 Cursor-anchored construction (S4)
Beam `B_j` (after `j` plan entries) is the circle through
`P_j = F_j(player)`, `X_j = F_j(F_K⁻¹(cursor))`, `Q_j = F_j(∞)`, oriented
`P → X → Q`. `P_j, Q_j` are pushed forward and `X_j` pulled back from the
cursor — all contractive maps — and `δ_j = P_j − Q_j` is tracked as an exact
difference (`δ' = −R²·conj(δ/((P−c)(Q−c)))`), because `P_j, Q_j` converge to
within 1e-30 of each other. Every `B_j` is built **independently**, so errors
never accumulate along the chain. Hit `h_j` = the root of `B_{j−1} ∩ S_j` with
the planned side. Pops (`6,6`) reuse memoized frame data exactly.

### 3.3 Iterative refinement (S5)
- **Single shooting** (Newton on θ, residual = cursor miss): with θ in float64
  the residual stalls at 49–110 px at depth 10 regardless of residual
  precision; at depth 14–15 the float64 start is already on a different hit
  sequence (basin of attraction ~1/S). Needs a high-precision start, at which
  point refinement is unnecessary. Dominated.
- **Multiple shooting** (user's proposal — every hit point a parameter;
  residuals = local reflection-law and curvature-law errors; block-banded
  Jacobian): converges to ~1e-13 px in pure float64 at every depth, from
  1e-3 rad of noise on every angle. With high-precision residuals and an f64
  Jacobian (mixed-precision iterative refinement) it gains ~7 digits per
  iteration: 3e-5 → 3e-11 → 6e-18 → … → 4e-50 px at depth 15. It is the
  general form of S4 (which is its closed-form solution for inversion chains)
  and the route to effects without a closed form (line reflections, portals,
  partial arcs). Prototype: `docs/research/oracle/multishoot.py`.

### 3.4 Physical continuation and the precision ladder
After the first event the planned trace passes through, the physical trace
applies it and continues forward. That is an initial-value problem with
inherent sensitivity (up to ~1e40 per 30 hits); only precision helps.
`PrecisionLadder`:
1. One raw-float pass (`FastSolver`) yields the float64 trace **and** its
   tangent-linear error estimate (§4) plus a reliability flag.
2. Accept float64 if the estimate ≤ 0.01 px and the trace is reliable.
3. Otherwise compute at the BigFloat tier the estimate asks for, re-estimate
   **along that high-precision trajectory** (replay, §4), accept when it meets
   tolerance and is reliable, else jump to the tier the replay requests.
   Tiers: 53, 112, 224, 448, 896, 1792 bits.
4. `verify = true` adds an a-posteriori check (two consecutive tiers agree).

At every tier all candidate roots are computed and ordered at that tier's
precision — no threshold-based shortcuts. The only tolerance in the system is
the accuracy target the ladder converges to (0.01 px); it chooses precision,
never topology.

### 3.5 Provenance (exact by construction, no tolerances)
- **Cancelling re-hits** (Tracer's source-based stack pop): applying the
  inversion of the surface whose inversion produced the current beam restores
  the earlier beam *object* exactly (`I_S∘I_S = id`), and the trace resumes at
  the shared intersection point actually hit (`B ∩ S = I_S(B) ∩ S`; if the
  current beam was born at the original hit its departure root is excluded,
  so it is the other point by construction; otherwise the two distinct points
  are identified by comparison). A restored straight beam stays exactly
  straight. Stack entries carry over across the planned prefix, the
  divergence point and the cursor.
- **Departure roots** are excluded by exact parameter equality (same
  deterministic computation) after a pop, or by the exact `P = 0` form for a
  beam born at a hit point.
- **Power of a point** `P = |q|² − R²` (no square root): exactly 0 for a point
  exactly on a circle, at every precision tier.
- **Unrealizable plans** (e.g. a cancelling pair `5,5` aims straight at the
  cursor and the line misses circle 5): planned mode applies nothing more, the
  beam passes every surface, the cursor is not reached (Tracer semantics).
- Inputs the beam model leaves undefined (player or cursor exactly at an
  inversion center, player == cursor) fall back to `Tracer`.

## 4. Accumulated-error estimation (`ErrorEstimate`)

- **Tangent-linear gain:** a unit perturbation is seeded on the beam's tangent
  angle where the sensitive continuation starts (divergence point / cursor /
  player) and propagated as dual numbers (`DualNum`, inline in `FastSolver`).
  `G_i = |∂p_i/∂φ|`.
- **Envelope:** `est_i = C·eps·max_{j≤i} G_j`. A plain `G_i` underestimates
  palindromic paths (the beam retraces; the seeded perturbation is undone, but
  rounding injected at the peak is not).
- **Calibration** (float64 events vs oracle, 946 significant pairs):

  | safety factor | coverage (est ≥ actual) | missed (est ok, actual > 0.01) | false alarms |
  |---|---|---|---|
  | C = 64 | 59.6% | 9 | 26 |
  | C = 1024 | 95.5% | 0 | 75 |
  | **C = 4096** | **98.8%** | **0** | 113 |

- **Reliability:** each ordering/root decision records its margin (gap to the
  runner-up). If a margin is not ≥ 16× the error estimate, the trajectory may
  be on another branch and its gains are meaningless beyond that event.
  (Stress-corpus failure that motivated this: float64 hit circle 9 twice — a
  cancelling pair — where the truth goes 9 → 7, then retraced palindromically,
  so its gains collapsed while the true sensitivity kept growing to 1e41.)
- **Replay:** gains along a high-precision trajectory are computed in float64
  by resetting the dual beam's *values* to the rounding of the high-precision
  beam at each step while the *derivatives* keep propagating.
- Planned (anchored) prefix: local a-posteriori residuals (each point vs. its
  circle and the independently built next beam).

## 5. Benchmark (GDScript, `make bench`)

Pass = every event (surface, side, applied, kind) matches and max point error
≤ 0.01 px. Times per scenario (planned + physical), Apple Silicon, headless.

Main corpus (depth 1–15):

| strategy | planned | physical | median | max |
|---|---|---|---|---|
| s0_tracer | 6/160 | 4/160 | 4.7 ms | 11 ms |
| beam_f64 | 160/160 | 131/160 | 23 ms* | 31 ms* |
| beam_big112 | 160/160 | 160/160 | 93 ms | 130 ms |
| **ladder** | **161/161** | **161/161** | **4.3 ms** | **81 ms** |
| ladder_verify | 160/160 | 160/160 | 113 ms | 315 ms |

Stress corpus (depth 16–25, 32-hit physical):

| strategy | planned | physical | median | max |
|---|---|---|---|---|
| s0_tracer | 0/40 | 0/40 | 5 ms | 9 ms |
| beam_f64 | 40/40 | 9/40 | 45 ms* | 51 ms* |
| **ladder** | **40/40** | **40/40** | **118 ms** | **325 ms** |
| ladder_verify | 40/40 | 40/40 | 335 ms | 744 ms |

\* generic object-wrapped float64 path; the ladder's float64 tier uses the
bit-identical raw-float `FastSolver` (~50× faster).

GDScript BigFloat (base 2^28 limbs, ≤ 4 ulp, `tests/test_precision_*`):
add/mul 4–9 µs, div/sqrt 46–190 µs (112–448 bits); float64 ops ~9 ns.

## 6. Invariant coverage of the game output
`tests/test_precision_invariant_sweep.gd` runs the full `InvariantChecker`
(NOGAPS, PHYSICAL-CONTINUITY, TRACE-ENDS, SINGLE-DIVERGENCE, NO-POST-CURSOR-
DIVERGENCE, PHYSICAL-PREVIEW-MATCH, S16, HITPOINT-ON-CARRIER, …) on the
rendered inversion-grid traces: 5386 no-plan runs over the sweep grid plus
points of interest (positions exactly on circles, screen corners) and 540
runs over six plans including `7,4,1,3,6,6,6`. **Zero violations** for every
run handled by the beam solver; the 146 degenerate inputs that fall back to
`Tracer` are counted, not asserted. (The generic sweep only covers
`scenes/test_levels/`, which all fall back to `Tracer` — that gap let the two
bugs below ship in the first playable version.)

Normalized-frame representation checks (BACK-TRANSFORM, RAY-ALIGNMENT,
ARC-MIDPOINT, S18 frame determinant, PARAM-MONOTONICITY) validate Tracer's
internal parametrization along the normalized ray; beam steps have no
normalized frame (`frame = null`) and these checks skip them. All steps of a
trace share one aim `Ray` (SHARED-RAY).

Bugs found by manual testing and the sweep, now covered by tests:
- Escape steps drawn bent: `via` must hold the travel *direction* for steps
  to/from infinity (Tracer's `_add_escape_steps` convention); the converter's
  other branch normalized a point as a direction. Both fixed.
- Flaky ping-pong loop after a cancelling re-hit: the popped beam was
  recomputed numerically (curvature ~1e-17 instead of 0); its point at
  infinity ~1e17 px away made the renderer draw the wrap-through-infinity as
  a short segment back. Fixed by provenance pops (§3.5). The periodic orbit
  itself (in, out, through infinity, in again) is correct per GAME_SPEC §6.3
  and identical to Tracer's.
- Long-way arcs drawn as chords: the renderer draws arcs with radius above
  `VisualConverter.MAX_ARC_RADIUS` as the chord start → end, which is the
  *other* arc when the path goes the long way round (via on the far arc, i.e.
  `(start−via)·(end−via) > 0`). Such steps are now emitted like a straight
  beam wrapping through infinity (out along the exact start tangent, in along
  the exact end tangent); the sweep asserts no chord is drawn for a long-way
  arc (LONG-ARC-CHORD).
- Crashes / NaN on inputs exactly on circles; post-cursor tail mismatch
  between planned and physical; zero-length steps; unrealizable plans.

## 7. Notable verifications
- `Tracer` vs oracle at depth 2 (`grid60_chain_d02_141`): along the tracer's
  own normalized line the order after the 3-hit is circle 9 (t=0.99798, 0.99890)
  then the cursor (t=1.0). The oracle is right; the tracer skips circle 9.
- Depth-1 agreement of `Tracer` and oracle (sides, orientation, post-cursor
  hits) confirms the oracle's conventions.
- The user's dump: the true path is 7,4,1,3,6, passes 9 twice, 6, passes 3
  twice, 6 — and the third 6-hit lands *exactly* on the first (I₆∘I₆ = id).
  These exact coincidences are structural; any noise turns them into t≈0
  ambiguities, which is what the dump shows.

## 8. Scope and limits
- **Stage 84: the legacy solver is deleted.** `Tracer`, `Planner`, `TransformCache`, `TrackedTransform`, `MobiusTransform` and the legacy intersection module no longer exist. `BeamTracer` is the only solver, with no fallback. The `s0_tracer` strategy has been removed from the bench; the S0 rows in §§1–7 are historical. Rows that mention a fallback to `Tracer` describe the solver before Stage 83.
- **Updated in Stage 81 (`docs/TDD_09_PRECISION_INTEGRATION.md`).** `BeamTracer` now handles every scene made of mirrors (lines and arcs), inversions (full circles and partial or one-sided arcs), terminals and pass-throughs. Portal and projective scenes fall back to `Tracer` until Stages 82 and 83.
- **The rules are oracle v2 (`docs/research/oracle/trace2.py`):**
  - walls are events;
  - the departure point is re-hit as an ordinary hit;
  - MAX_HITS = 32 counts every event.

  The corpora in `bench/` were regenerated under v2 (`regen_bench_v2.py`). The v1 results above (§1–7) describe the circle-only solver.
- **Exactness.** Endpoints, incidences, tangency and roots at the antipode (t = ∞) are decided by provenance and exact predicates (`Exact`), never by rounding.
- Planned and physical traces share one climb when they agree through the cursor. Results are cached per (player, cursor, plan), and the scene per surface set.

## 9. Reproduction
```bash
python3 -m venv docs/research/.venv && docs/research/.venv/bin/pip install -r docs/research/requirements.txt
docs/research/.venv/bin/python docs/research/oracle/gen_corpus.py                       # main corpus
docs/research/.venv/bin/python docs/research/oracle/gen_corpus.py --count 40 --seed 777 \
    --min-depth 16 --max-depth 25 --physical 32 --dps 200 --subdir stress --no-dump      # stress
docs/research/.venv/bin/python docs/research/oracle/evaluate.py --bits 53,106,212       # Python prototypes
make bench            # GDScript strategies vs oracle (main corpus)
make bench-stress     # stress corpus
./run_tests.sh -gselect=test_precision
```
