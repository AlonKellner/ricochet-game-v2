# TDD 15: Performance — gate turnaround, frame cost, the visibility region

### Stage Status

| Phase | Topic | Status |
|-------|-------|--------|
| P0 | Benches: the region build per scene and plan, the ladder's census, the worker's region time in the frame bench | Done |
| P5 | Gate turnaround: scheduling, sharding, the ctest split, `make gates` | Done: about 9 min to 4.5 min |
| P1 | The exact-image cache key (a 6-decimal cursor key: a bug) | Not started |
| P2 | The ladder: BigFloat only where an exact decision needs it | Not started |
| P3 | The region build: cast reuse, cheaper casts, the fill | Not started |
| P4 | The frame path: defs handle, lean trace results, overlay keys | Not started |
| P6 | Docs | Not started |

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

