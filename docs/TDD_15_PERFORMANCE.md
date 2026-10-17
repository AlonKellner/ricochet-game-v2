# TDD 15: Performance — gate turnaround, frame cost, the visibility region

### Stage Status

| Phase | Topic | Status |
|-------|-------|--------|
| P0 | Benches: the region build per scene and plan, the ladder's census, the worker's region time in the frame bench | Done |
| P5 | Gate turnaround: scheduling, sharding, the ctest split, `make gates` | Done: about 9 min to 4.5 min |
| P1 | The exact-image cache key (a 6-decimal cursor key: a bug) | Done: no region or golden changed |
| P2 | The ladder: BigFloat only where an exact decision needs it | Done: P2a (BigFloat arithmetic, bit-identical), P2b (exact circles: double roots, circular concurrency) |
| P3 | The region build: cast reuse, cheaper casts, the fill | P3a done (parallel casts); the cast memo and the fill not needed for the budgets |
| P4 | The frame path: defs handle, lean trace results, overlay keys | Done: P4a (native dashes, lazy hit records), P4b (defs key, lazy hit points, one call per arc step, memoized carriers) |
| P6 | Docs | Done; signed off (user, 2026-10-18) |

**Rules:** every phase is behaviour-neutral. Traces, regions and goldens stay bit-identical. A changed golden is a behaviour change, brought to the user with evidence. Coverage stays as it is: trims of duplicated checks come only as a separate proposal with numbers (user, 2026-10-17).

**Workflow:** branch `perf-1`, small green commits, with the full gates before every commit.

## Budgets (user, 2026-10-17)

| What | Budget |
|---|---|
| Region build on `playground/inversion_grid.tscn`, plans up to 4 entries | p95 ≤ 50 ms (worker time) |
| Region build on `main.tscn` | p95 ≤ 10 ms |
| Main-thread frame on inversion_grid (cursor and plan orbits) | mean ≤ 1 ms, max ≤ 4 ms |
| The gates (ctest, GUT, ASan, oracle, goldens) | ≤ 4 min total |

## Benches

- **`visibility bench`** (`native/tests/test_visibility.cpp`, skipped). Run it with `RC_VIS_SCENE=<dump> native/build/rc_tests -tc="visibility bench" --no-skip` and `RC_VIS_BENCH_PLANS=<n>`. It takes 12 players per plan and reports:
  - the build time (median, p95, max);
  - casts per region by kind (crit, side, mid, refine, probe), from `Result.casts_by_kind`;
  - the ladder's census (`Tracer::ladder_stats`): walks accepted at float64, declined for a margin or for the estimate, and BigFloat walks per tier with their time.
- **`make frame-bench SCENE=res://scenes/playground/inversion_grid.tscn`.** It reports the main-thread frame time per phase, plus the worker's build time of the regions finished in each phase (`VisibilityRegion.get_build_usec`).

## Baseline (2026-10-17, M-series, RelWithDebInfo)

**The region (`visibility bench`):**

| Scene, plan | Median | p95 | Max | Casts per region | BigFloat walks (4/8/16/32/64 limbs), time over 12 regions |
|---|---|---|---|---|---|
| main, none | 2.4 ms | 9.0 | 9.0 | 94 | 2/0/0/0/0, 0.6 ms |
| main, [5 L] | 17.8 ms | 33.5 | 33.5 | 373 | 138/9/9/9/9, 88 ms |
| main, [8 R, 5 L] | 25.5 ms | 44.0 | 44.0 | 415 | 180/11/11/11/11, 134 ms |
| inversion_grid, none | 24.3 ms | 60.4 | 60.4 | 102 | 150/1/0/0/0, 172 ms |
| inversion_grid, [1 L] | 297 ms | 459 | 459 | 439 | 732/79/38/33/33, 2.29 s |
| inversion_grid, [8 L] | 368 ms | 489 | 489 | 429 | 819/113/61/59/59, 3.16 s |

On inversion_grid, 7–8% of the walks are declined at float64 (mostly for a margin within its estimate). A few dozen climb every tier to 64 limbs: ties that a margin never settles. BigFloat takes about two thirds of the time. The float64 walks cost about 0.25 ms per cast there, against about 0.05 ms on main.

A profile of the native visibility sweep (`sample`) puts about 90% in `Ladder::replay_climb` (`BigFloat::mul`, `imul`, `normalize`, malloc).

**The frame (`frame-bench`), main thread, and the worker's region time:**

| Scene | Phase | Frame mean | Frame p95 | Frame max | Region p95 |
|---|---|---|---|---|---|
| main | cursor_orbit | 0.164 | 0.228 | 0.506 | — |
| main | plan_orbit | 0.327 | 0.463 | 0.630 | 12.0 |
| main | player_orbit | 0.167 | 0.257 | 0.276 | 2.1 |
| inversion_grid | cursor_orbit | 0.710 | 1.436 | 13.6 | 55.7 |
| inversion_grid | plan_orbit | 1.967 | 4.597 | 12.1 | — |
| inversion_grid | player_orbit | 1.986 | 3.426 | 16.8 | 526 (14 regions in 250 frames) |

**The gates (about 9 min: ctest 126 s, GUT 150 s, ASan 268 s with its build):**
- **ctest:** `rc_tests_visibility` is 126 s as one entry; the rest is 14 s plus 6 × 3.6 s golden parts.
- **GUT, 2.5 min:** `test_visibility_sweep.gd` shards take 95–115 s each, scheduled last, against ≤ 13 s for every other job.
- **ASan, 3.5 min:** visibility 177 s, `rc_tests` 107 s.

**Found:** `Solver::exact_image_cached` keys on `std::to_string(cursor)` (6 decimals). See P1.

## P5: the gates

**Changes:**
- **`make gates`** (`gates.sh`): the native build, oracle, ctest, GUT, ASan and the goldens diff, in order. Every gate runs even after a failure, and one timed summary is printed.
- **`run_tests_parallel.sh`:**
  - jobs start longest first: the visibility sweep, then the other files, then the shorter sweeps;
  - the visibility sweep runs in 2 × JOBS shards;
  - JOBS defaults to every core (the efficiency cores help: 133 s to 112 s);
  - `SKIP_ORACLE=1` skips its inner oracle run (`gates.sh` runs it once).
- **ctest entries:**
  - the visibility level loops (the plans' pin, the structure checks) run in `RC_VIS_PARTS` = 6 parts, taking every 6th region (`RC_VIS_PART`, `VisPart` in `test_visibility.cpp`). One costly level spreads too; a split by file left one 90 s part;
  - the slower single cases (VIS-REGULAR's allowance case, the oracle labels, the Stage 106 random scenes) have entries of their own;
  - `ctest -j` uses every core (`CTEST_JOBS`). `JOBS` still bounds compiles.
- **Kept as is:** the headless `godot --import` after every `make native` (2 s). There is no ccache on the machine; it would help only rebuilds.

**Result:**

| Gate | Before | After |
|---|---|---|
| ctest | 126 s | 39 s |
| GUT | 150 s | 124 s |
| ASan (build included) | 268 s | 98 s |
| Total | about 9 min | 271 s |

The rest is the region's cost: the GUT visibility shards and the ASan visibility cases (VIS-REGULAR's allowance case alone, 79 s under ASan). P2 and P3 attack it.

## P1: the exact-image cache key

`Solver::exact_image_cached` keyed the cursor with `std::to_string` (6 decimals), so cursors closer than 5e-7 px shared one cached exact image. It now keys by the exact bits (`%a`).

**Census:** a temporary counter of cache hits whose cursor differed from the stored entry's.

| Run | Wrong hits | Hits |
|---|---|---|
| Native visibility tests | about 1,600 | 92,000 |
| inversion_grid bench | 889 | 50,000 |
| Goldens | 0 (game cursors are float32) | 266,000 |

So the bug touched only region casts.

**Effect:** none observed. The new `visibility digest` (a skipped doctest that hashes every region on the shipped levels, 264 regions, exact bits; `--no-skip`) is unchanged: `21dafb508ef1834b`. The goldens are byte-identical. The digest is the reference that P2–P4 must keep.

## P2a: cheaper BigFloat, bit-identical

- **`BigFloat::mul`** sums its columns in int64 when both factors have fewer than 128 limbs. Each 28-bit product is below 2^56, so a column of at most 127 products plus a carry stays below 2^63. Longer factors keep the 128-bit sums of B1. The columns live in a per-thread buffer, so only the result allocates.
- **`BigFloat::Limbs`** is a `SmallVec<int64_t, 8>` (`rc/small_vec.hpp`): up to 8 limbs inline. The tier-4 values and the exact predicates' values (2 to 8 limbs) no longer allocate. Malloc and free were about 30% of the BigFloat profile.

**inversion_grid** (`visibility bench`):

| Plan | Median before | Median after | p95 before | p95 after | BigFloat time before | BigFloat time after (12 regions) |
|---|---|---|---|---|---|---|
| none | 24.3 ms | 17.7 ms | 60.4 | 35.5 | 172 ms | 92 ms |
| [1 L] | 297 ms | 220 ms | 459 | 325 | 2.29 s | 1.54 s |
| [4 R] | 365 ms | 264 ms | 475 | 348 | 2.69 s | 1.85 s |

The digest is unchanged (`21dafb508ef1834b`), the goldens are byte-identical, and the GUT gate went from 124 s to 96 s.

**What is left in BigFloat (P2b, a behaviour change to propose):** by cost, the walks that climb every tier to 64 limbs. On inversion_grid there are 33–52 per 12 regions; a walk's cost grows roughly as limbs², so they are about 85% of the BigFloat time. A temporary log of their declined decisions shows every one is a hit with a margin of exactly 0 on a bounds edge (surfaces 10–13): a double root, a beam exactly tangent to the edge, both roots of one surface at one point. The margin stays 0 at every tier, so the climb never converges and the 64-limb walk is used unconverged. In the golden sweeps, 652 of 468,727 walks climb and 311 reach 64 limbs.

## P3a: parallel casts

The members' own and side casts (`cast_members`), and the gaps' first middle casts (`build_cells`, keyed by direction so the loop takes each where it would cast), run in parallel batches (`Builder::cast_batch`).
- Each thread casts with a helper tracer of the caller's (`Tracer::helper`): the same defs and switches, its own solve caches (pure memos), kept across builds.
- Results land by index. The region is the casts made in order: the digest is the same with `RC_VIS_THREADS=1` and with 8.
- **Threads:** `RC_VIS_THREADS`, else every core up to 8. `run_tests_parallel.sh` sets 1: its processes are the parallelism, and 12 processes × 8 threads made the GUT gate slower (96 s to 116 s; 88 s with 1 thread).

**`visibility bench`:**

| Scene, plan | Median before (P2a) | Median after | p95 before | p95 after |
|---|---|---|---|---|
| inversion_grid, none | 17.7 ms | 6.3 ms | 35.5 | 13.7 |
| inversion_grid, [1 L] | 220 ms | 62.7 ms | 325 | 116 |
| inversion_grid, [4 R] | 264 ms | 69.3 ms | 348 | 91 |
| main, none | 2.4 ms (P0) | 0.9 ms | 9.0 | 2.6 |
| main, [5 L] | 17.8 ms (P0) | 5.5 ms | 33.5 | 11.1 |

**`frame-bench`, inversion_grid:** player_orbit finishes 56 regions in 250 frames (14 before), at a 10 ms p95.

The p95 on inversion_grid plans is still about twice the budget. The longest single cast bounds a batch: a walk that climbs to 64 limbs (P2b).

## P2b: exact circles (user decisions, 2026-10-17)

**User decisions:** "Exact double-root rule"; then, once the census showed the climbs were concurrencies, "Concurrency rule, oracle first", keeping the double-root rule.

- **The exact beam** (`exact::BeamRec`, `exact::GCircle`; the oracle's `walk.gc`). Every beam of an epoch is the image of a line under the frame's maps. The walk keeps its beam's exact generalized circle A(x² + y²) + Bx + Cy + D = 0: the last exact line and the maps since (reflection, inversion, portal translation). It is made exact only when a decision asks, within 320 limbs, and the stack saves and restores it. Each map is exact in coefficient form: reflection by the rotation trick of `reflect_point_h`; inversion by shifting to the center, (A, B, C, D) → (D, R²B, R²C, AR⁴), and shifting back.
- **Double roots** (`Solver::beam_tangent`, `gaps`). Two roots of one carrier a rounding apart, where the circle touches the carrier exactly (`gc_tangent_line`, `gc_tangent_circle`), are one decision: not each other's neighbour in the margins. It fires on no measured walk; the goldens and the digest are unchanged.
- **Concurrency on a circular beam** (`Solver::concurrent_ties_circle`, the oracle's `_concurrent_ties_gc`, GAME_SPEC §11.4). Adjacent crossings of two line carriers at their common point, which the exact circle passes, share one parameter, and the id orders them, as on an exact line. Before, rounding ordered them, and the ladder climbed every tier to 64 limbs without converging: the margin between the two hits was 0 at every tier.
  - **Fixtures** (`gen_fixtures_perf_ties.py` → `bench/fixtures/perf_circle_ties.json`). The inversion of y = 450 in the circle at (500, 500), r 100, passes X = (600, 400), where two pass lines cross. Both id assignments come in id order. With the rule off the oracle gives [3, 2] for one of them. A control moves one line by 1/16 px.
  - **Native** (`test_circle_ties.cpp`): f64, Dual, 4 and 64 limbs. The tests fail without the rule.
  - **Oracle tests:** `test_trace2.py` (the maps carry points onto the image; the tie).
  - **No other change:** regenerating every fixture and the corpus gives the same output under the old and the new oracle. The goldens replay with 0 mismatches over 284,096 combos, and the region digest is unchanged.

**inversion_grid** (`visibility bench`, 12 players):

| Plan | Median before (P3a) | Median after | p95 before | p95 after | 64-limb walks before | 64-limb walks after |
|---|---|---|---|---|---|---|
| [1 L] | 62.7 ms | 28.0 ms | 116 | 47.5 | 33 | 2 |
| [4 R] | 69.3 ms | 25.5 ms | 91 | 60.0 | 52 | 3 |
| [8 L] | — | 29.9 ms | — | 36.6 | 59 | 1 |

**What remains** are near-concurrencies: a beam within less than one float64 ulp of a corner, not through it. The ladder's margins are float64 distances, so the two hits round to one point at every tier. Only the 64-limb walk orders them correctly; that climb is right and stays.

**Note:** the oracle's generators do not reproduce 344 committed fixture and corpus files byte for byte, under either oracle (format or library drift, not a behaviour change). They were not re-committed.

## P4a: the frame path

**Survey:** temporary timers on inversion_grid, plan_orbit, per redraw or recompute:

| Part | Cost |
|---|---|
| PathRenderer `_draw` | 0.69–0.77 ms |
| `trace_pair` | 0.45 ms |
| merge plus hits | 0.055 ms |
| the two `prepare_for_display` calls | 0.045 ms |
| GameManager `_process` | 0.025 ms |

Of the draw, the dashed arcs were 58% and the dashed lines 13%: GDScript loops computing up to 267 dashes per step, with sin and cos per point. Batching their draw calls in GDScript gained nothing.

A cached `trace_pair`, with no solve, still cost 189 µs: about 34 µs converting the defs, the rest wrapping steps, hits and the hit Dictionaries into Godot objects.

**Changes:**
- `VisualConverter.dash_arc` and `dash_line` compute a dashed step's segments natively, the same dashes (each arc dash the points `draw_arc` takes for it). The renderer draws them in one `draw_multiline` per step.
- `TracePath.hits` (the Stage 107 records, a Dictionary per hit) is made from the native records on first read; the preview never reads them. A cached `trace_pair` went from 189 to 141 µs.

**Main-thread frame on inversion_grid:**

| Phase | Mean before | Mean after | Max after |
|---|---|---|---|
| cursor_orbit | 0.66 ms | 0.51 ms | 6.9 ms |
| plan_orbit | 1.65 ms | 1.20–1.27 ms | 4.8–5.1 ms |
| player_orbit | 1.78 ms | 1.15–1.23 ms | 7.4 ms |

**What is left:**
- **Spikes:** the maxima are the preview's own trace when the cursor is near a tie and the ladder climbs (`trace_pair` max 6.7 ms against 0.42 ms mean).
- **Per recompute:** about 0.1 ms of step wrapping and 0.03 ms of defs conversion.
- **Gates:** 231 s for a change outside the core headers (ASan 108 s); a core header change rebuilds both trees (about 290–340 s).

## P4b: the rest of the frame path

- **`BeamTracer.defs_key`:** the caller's key of the defs it passes. The PathRenderer passes its `SceneDefsCache` version, and the tracer converts the defs only when the key changes, instead of every call (about 30 µs).
- **`TraceHit.point`:** the hit's `Point` object is made on first read, from the stored coordinates. Every hit step made three Godot objects before.
- **`VisualConverter.arc_draw`:** `is_arc` and `arc_params` in one call, returning a packed array instead of a Dictionary.
- **`prepare_for_display`:** the carrier lookup reads each surface's properties once per call, not once per step.
- **Endpoint circles:** coloured without a Callable per step.

**Main-thread frame on inversion_grid** (two runs):

| Phase | Mean | Max |
|---|---|---|
| cursor_orbit | 0.44 ms | 6.1–6.4 ms |
| plan_orbit | 1.03 ms | 4.1–4.2 ms |
| player_orbit | 1.02–1.06 ms | 7.1–7.3 ms |

The maxima are the preview's own trace near a tie (the ladder climbing), left as they are (user, 2026-10-17).

## Summary (before, after)

| What | Before | After | Budget |
|---|---|---|---|
| Gates, a change outside the core headers | about 9 min | 202–231 s (`make gates`) | ≤ 240 s |
| Gates, a core header change | about 9 min | 290–340 s | — |
| Region, inversion_grid with a plan, median | 297–368 ms | 24–28 ms | — |
| Region, inversion_grid with a plan, p95 | 459–489 ms | 32–55 ms | ≤ 50 ms |
| Region, main, p95 | 9–44 ms | 3–15 ms | ≤ 10 ms |
| Frame mean, inversion_grid, cursor / plan orbit | 0.71 / 1.97 ms | 0.44 / 1.03 ms | ≤ 1 ms |
| Frame max, inversion_grid | 12–17 ms | 4–7 ms | ≤ 4 ms |

The budgets are met or within a few percent, except the frame maxima and the region tails. Both are near-ties, where the ladder rightly climbs: a beam within less than one float64 ulp of a tie. Removing them would mean changing the precision semantics or moving the preview off the main thread; the user chose neither.

## Interactive user tests

1. **The preview looks as before** (`main.tscn`, `playground/inversion_grid.tscn`): dashed lines and dashed arcs (the dashes, their spacing, where they stop), solid parts, the endpoint circles, ghost steps.
2. **The visibility region looks as before and follows the player sooner** while walking on inversion_grid with a plan; the cursor alone does not recompute it.
3. **Firing works:** the arrow flies the preview's path, and hit effects, spawns and targets still happen. They read the lazily made hit records.
4. **No new hitches** while orbiting the cursor with a plan on inversion_grid.

## Regression checklist

`make gates`: the native build, oracle, ctest (RelWithDebInfo), the GUT suite, ASan, the goldens unchanged. The region digest (`visibility digest`, `--no-skip`) stays `21dafb508ef1834b`.

