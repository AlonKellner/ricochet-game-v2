# TDD 25: Measure, then the bit-exact speedups (Stage 120)

The first stage of the smooth-frames program (plan approved by the user, 2026-10-20). It does two things:

- measures what a frame costs the game and the Godot host;
- removes the work that can go without changing a single bit.

Every result is unchanged: the session golden, the goldens and the region digests are byte-identical. Timing is
evidence, never a gate, because machines and their load differ. The gates check counts.

The following stages build on this one:

- Stage N+1: asynchrony;
- Stage N+2: presentation (crisp geometric morphs);
- Stage N+3: the deeper algorithmic work.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 120.1 | The measurement harness: `rc_tool bench` (`native/hosts/headless/bench.cpp`), `make perf`, `make perf-diff`, the Godot frame probe | Done |
| 120.2 | The session's bookkeeping: typed keys, versions, pole memo, one display conversion when shared, the tracer's pair cache off | Done |
| 120.3 | The solver: the exact pole predicates memoized across walks (`rc::PoleMemo`); the ladder moves events instead of copying them | Done |
| 120.4 | A persistent thread pool (`rc::ThreadPool`) for the region's cast batches | Done |
| 120.5 | The draw list tracks changes: cached painters, node ids and versions, `rc_game_draw_changes` (C API 1.1); the Godot host skips unchanged nodes | Done |
| 120.6 | Docs, `make gates` (1013 s, green, ASan included), sign-off | Done (user, 2026-10-21) |

## 120.1 The harness

**`rc_tool bench`.** It runs these phases per level (`--all <dir>` for every level):

- static;
- cursor orbit;
- a two-entry plan, still and with the cursor orbiting;
- hover;
- player orbit;
- `plan_click`, the click frame;
- `walk`, the physics ticks plus the derived state;
- `flight`;
- `capi`, a host's C API frame: `advance(0)` plus the draw list.

**What it reports for each phase:**

- timings: mean, p50, p95, p99 and max;
- counts: previews and regions recomputed, ladder climbs of the preview and the region, and why the preview
  declined float64 (an unreliable decision, or an estimate over tolerance).

**Its outputs:**

- `--json` and `--md` write the reports;
- `make perf` writes `build/perf/perf.{json,md}`, gitignored;
- `make perf-diff A= B=` compares two reports;
- `engines/godot/tools/frame_probe.gd` times the Godot host's frames, phase by phase.

**The baseline** (main at 257476d, 120 frames per phase, ms of game time per frame):

| Level | Player orbit (mean / max) | Walk | Plan click |
|---|---|---|---|
| hit_effects | 73 / 90 | 72 / 102 | 1247 / 2089 |
| tangent_infinity | 23 / 56 | 20 / 48 | 42 / 108 |
| crossings | 15 / 23 | 19 / 30 | 59 / 130 |
| inversion_grid | 6.4 / 13 | 3.8 / 16 | 56 / 64 |

- Every other level's moving player is under 3 ms.
- Every level's click costs at least 12 ms.
- Cursor frames are 0.02–0.5 ms, except hit_effects at 4.5 ms, where every frame's preview declines float64.

**The Godot probe agrees with the core** (crossings walk 20.7 ms, hit_effects 31 ms mean and 163 ms max): the
host's own work is small next to the game's.

## 120.2–120.5 What changed

**The session** (`native/game/src/session.cpp`):

- The derived parts are keyed by typed structs instead of `nlohmann::json`. Positions are compared by value, as
  the JSON did; the plan and the flags by version (`flags_version_` bumps when the flags' encoding changes).
- `pole(n)` is memoized per (plan, flags, world) version. It was asked 3+ times a settle.
- `prepare_for_display` runs once when the pair is shared.
- The ladder result is moved, not copied.
- The edge marks are a struct.
- `rc::Tracer::set_pair_cache(false)`: the single-entry cache never hit in a session but copied every pair.

**The solver** (`native/core`):

- `rc::PoleMemo`: `pole_on` and the Q-part of `pole_invert` (`exact::pole_invert_from`), keyed on Q's exact
  representation, the scene and the surface. They depend on Q alone, not on the cursor.
- The ladder moves the walks' events and gains, and builds the physical climb once.
- `rc::ThreadPool` replaces the threads `cast_batch` made per batch.

**The draw list** (`Session::draw_frame`):

- The painters' calls are shared and cached by their inputs: a surface piece by the flags' version and its
  overlays; the path by the preview's version and the display mode; the region by its version; the spawn
  preview by its key; the background and the cursor once.
- Every node has a `NodeKey` (its kind and ids, no JSON) and a content version.
- `Session::draw` (the digest) is built from it, so the golden is unchanged.

**C API 1.1** (compatible: fields appended to the size-prefixed list):

- `node_ids`, `node_versions`, `node_unchanged`;
- `rc_game_draw_changes`, which sends commands only for nodes that changed.

**The Godot host** draws from `rc_game_draw_changes` and sets z, position and modulate only when they change.

## Evidence: paired runs

The machine was in use while measuring (the user's manual testing, other applications). Runs taken at different
times differed by up to 70% on the same build, so the comparison is paired:

- the commit before the optimizations (1ba9ec4, harness included) and this stage's head, built side by side;
- run alternately, three rounds a level, 150 frames a phase;
- the best mean of each kept.

| Level | Phase | Before (ms) | After (ms) | After / before |
|---|---|---|---|---|
| inversion_grid | static | 0.106 | 0.082 | 0.78 |
| inversion_grid | walk | 4.102 | 3.665 | 0.89 |
| inversion_grid | player_orbit | 5.707 | 5.420 | 0.95 |
| inversion_grid | plan_click | 59.6 | 54.0 | 0.91 |
| projective | static | 0.029 | 0.023 | 0.79 |
| tangent_infinity | static | 0.104 | 0.088 | 0.85 |
| tangent_infinity | capi | 0.118 | 0.102 | 0.86 |
| tangent_infinity | plan_click | 50.3 | 46.7 | 0.93 |
| crossings | capi | 0.052 | 0.035 | 0.68 |
| crossings | player_orbit | 15.47 | 15.43 | 1.00 |
| crossings | walk | 19.46 | 19.65 | 1.01 |
| hit_effects | player_orbit | 75.2 | 74.6 | 0.99 |
| hit_effects | plan_click | 1334 | 1325 | 0.99 |
| main | capi | 0.028 | 0.022 | 0.80 |
| main | walk | 0.755 | 0.707 | 0.94 |

Cursor-orbit frames moved by −6% to +9%, which is within the shared machine's noise.

**What it shows:**

- The work around the solver went down: still frames 15–20%, the C API's frame 10–30%, and inversion_grid's
  walk and player orbit 5–11%.
- The Godot host's own frame went down about half (frame probe, cursor orbit: main 0.077 → 0.045 ms,
  crossings 0.207 → 0.097 ms).
- The solver's frames, the moving player and the clicks, are within a few percent.

**Why.** The profiles show where those frames go (Stage 120's survey):

- the exact ties: casts aimed exactly at points of interest climb the BigFloat ladder to 64 limbs without
  converging;
- the region's critical path is its slowest such cast. It stops scaling at 4 threads (crossings 15.4 ms).

Bit-exact bookkeeping cannot remove that work. Only two things can: taking it off the frame (Stage N+1), or
deciding those ties by provenance (Stage N+3, which is judged).

**Left for later:**

- integer frame keys in `to_path`;
- flat maps in `Walk`;
- a BigFloat arena;
- concurrent refinements.

Each was estimated at 5–10% of the preview's work, and the refinements are order-dependent: their 64-refinement
cap is consumed in sequence. They wait until the frames they touch matter, after N+1.

## Findings

| # | Finding | Status |
|---|---|---|
| F6 | The region is computed in the frame (TDD_24) | Measured here; Stage N+1 moves it off the frame |
| F7 | hit_effects: every cursor frame's preview declines float64 on an unreliable decision (a tie), and its region climbs about 460 times per plan change (1.1–1.3 s) | Resolved by Stage 123 (ties by provenance, TDD_28): its previews never climb; a plan click 1270 → 14 ms |
