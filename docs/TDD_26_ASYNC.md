# TDD 26: Asynchrony — the frame never waits for the solver (Stage 121)

The second stage of the smooth-frames program (plan approved by the user, 2026-10-20). Stage 120 measured the
frame (TDD_25) and found that its cost is the solver: the visibility region, recomputed on every frame the
player moves (F6), and the preview's exact precision climbs, which on hit_effects run every cursor frame
(F7). This stage takes both out of the frame. A live game shows what it has and computes the rest on its
own threads.

Nothing about the game's results changes. Replays and tests run synchronously, as before, and the session
golden is byte-identical. An asynchronous game, once its work is done, reaches the synchronous game's record
byte for byte on all 48 sessions. Timing is evidence, never a gate.

The decisions this stage follows (user, 2026-10-20):

- **The preview tier.** When the float64 pass cannot decide the path exactly, show it provisionally, then
  the exact one. Firing and the rules use exact results only.
- **The order.** Asynchrony comes before presentation. The provisional-to-exact replacement and a region
  that arrives late are animated in Stage N+2 (crisp geometric morphs, no crossfades).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 121.1 | `rcg::LatestJob` (`rcg/jobs.hpp`); the region's cancel flag (`rc::vis::compute`) | Done |
| 121.2 | The region as a job (`SessionConfig::async`, `region_pending`) | Done |
| 121.3 | The preview's two tiers (`rc::Ladder::provisional`, `Tracer::trace_pair_provisional`, `preview_exact`); a fire reuses the exact preview; C API 1.2; the Godot host | Done |
| 121.4 | Speculative prefetch: the hovered side's region, computed while idle, shown at its click | Done |
| 121.5 | A click on a side whose prefetch still runs adopts it; `SessionConfig::prefetch`; the bench's click-to-region time | Done |
| 121.6 | Docs, `make gates` (1602 s, green, ASan included), sign-off | Sign-off pending |

## 121.1 The job

`rcg::LatestJob<In, Out>` is one persistent worker thread that computes `fn(input, cancel)` for the newest
input it was given.

- **Newest wins.** A submit replaces any input still waiting, so the frame never queues stale work.
- **Cancellation, by choice.** With `cancel_running`, a submit also cancels the running computation, and a
  finished result that was not taken yet is dropped. Without it, the running computation finishes and its
  result is kept.
- **One thread touches the game.** A finished result waits until the session takes it (`take`), on the
  game's thread, in `settle`. Results carry the ticket of their input.
- **Its own thread, not a pool thread.** `fn` uses the shared pool (`rc::ThreadPool`, Stage 120) for the
  region's casts without nesting in it.

`rc::vis::compute` takes an optional `std::atomic<bool>` cancel flag. The builder polls it before each cast
on the caller's thread and stops by an exception it catches itself (`Result::cancelled`). Pool threads never
throw: their bodies skip once the flag is set.

## 121.2 The region

`SessionConfig::async` (C API `rc_game_config.async`) makes a live game. Off, the default, everything is
computed in the frame, in order, as before.

- **A request per input change.** When the region's inputs change (the player, the plan, the flags, the
  world, the switches), the session takes a snapshot (the scene defs as a shared pointer, the player, the
  plan items, the flags, the bounds) and submits it with a new ticket. The frame keeps showing the last
  region.
- **A running region is kept.** The region job does not cancel the running computation. The first version
  did, and a player moving every frame never saw a region, because each one was cancelled by the next frame.
  Now a running region finishes and shows (a few frames behind the player) while the newest one computes. A
  result applies when its ticket is newer than the one shown.
- **Switching visibility off** marks every older ticket as applied, so no region still computing can show
  after it.
- **`region_pending()`** reports a request not shown yet (`rc_frame.region_pending`).

## 121.3 The preview's two tiers

On each cursor or plan change the frame runs the float64 tier alone (`Tracer::trace_pair_provisional`, the
ladder with `provisional = true`).

- **When the ladder accepts float64**, the result is the exact one (the ladder would have stopped there) and
  becomes the preview at once.
- **When it would climb**, the float64 events become the *provisional* preview, and the exact climb runs as a
  job. Only the current inputs' exact result applies; a stale one is dropped. `preview_exact()` is false
  meanwhile (`rc_frame.preview_exact`).
- **Only the path is drawn from the provisional preview** (`Session::shown`), and the player faces it.
  Everything the rules read uses the exact preview: the hover and plan overlays, the spawn preview, the
  truncation markers, the record.
- **A fire reuses the exact preview** when its inputs equal the fire's, instead of tracing again. When the
  preview is still provisional, the fire waits for its climb. A fire never uses a provisional result.

**The Godot host** creates its game asynchronous (the `asynchronous` property, on by default). Its
session-replay test turns it off.

## 121.4–121.5 The click

A plan change used to hitch for one frame: its region computed synchronously (TDD_25: 1.26 s on hit_effects).
With 121.2 the frame no longer hitches, but the click's region still shows only when it is done.

- **Prefetch.** While the cursor rests on a plannable side and no region is computing, `update_prefetch`
  submits the region that the side's click would make (the plan plus `entry_for(detect_click())`) on a third
  job. A newer prefetch cancels the running one. It is keyed on the cursor, the player, the plan, the flags,
  the world and the region shown.
- **At the click**, the region's request is compared with the prefetch's input, bit for bit
  (`same_inputs`):
  - when the prefetch has finished, its region shows on the click's frame;
  - when it is still running, the session adopts it as the click's region (121.5) instead of starting the
    same computation again;
  - otherwise the prefetch is cancelled and the region job submitted as before.
- **`SessionConfig::prefetch`** (default on) turns it off; the bench's `--no-prefetch` uses it.
- **`Session::drain()`** (tests, the async replay) waits for every job, including the prefetch it submits
  itself.

## The C API (1.2)

Appended to size-prefixed structs, so older hosts keep working:

| Field | Meaning |
|---|---|
| `rc_game_config.async` | 1: a live game (the region and the preview's climbs in the background); 0: everything in the frame (replays) |
| `rc_frame.region_pending` | a newer visibility region is being computed |
| `rc_frame.preview_exact` | 0 while the path shown is provisional |

`docs/PORTING.md` ("Background work") tells a host what to expect.

## Tests

| Test | What it shows |
|---|---|
| `async: a job's newest input wins; the superseded one is cancelled` | `LatestJob`'s replacement and cancellation |
| `async: the region stops when cancelled` | `rc::vis::compute` honours the flag |
| `async: without cancellation the running computation finishes and is kept` | the region job's policy |
| `async: a player moving faster than its regions drains to the synchronous session` | regions show while the player moves; the drained record equals the synchronous one |
| `async: a preview that must climb shows provisionally, then the exact one` | hit_effects: provisional frames happen; each drained preview equals the synchronous one |
| `async: the hovered side's region is prefetched, shown at the click` | the finished-prefetch path |
| `async: a click on a side whose prefetch still runs shows that prefetch's region` | the adoption path |
| `sessions: the asynchronous session, drained, replays byte for byte` | all 48 sessions, in the `rcg_sessions_async_*` ctest entries (12 parts of their own) |

The async tests and an async session part run clean under ThreadSanitizer (`native/build-tsan`,
`RC_SANITIZE=thread`).

## Evidence

**The Godot host's frame** (the frame probe, `engines/godot/tools/frame_probe.gd`; ms per frame, mean):

| Level | Phase | Synchronous | Asynchronous |
|---|---|---|---|
| crossings | player orbit | 22 | 0.06 |
| crossings | walk | 20.7 | 0.013 |
| hit_effects | player orbit | 86 | 0.13 |
| hit_effects | walk | 31 (163 max) | 0.02 (0.3 max) |

**A click's region** (`rc_tool bench --async`, the `plan_click` phase, 6 clicks; ms from the click until its
region shows). "Until idle" rests the cursor on the side until nothing computes. "300 ms" rests it for 300 ms
right after the plan is cleared, so the cleared plan's own region may still be computing:

| Level | Cursor rests | Prefetch | No prefetch |
|---|---|---|---|
| crossings | until idle | 0.18 | 50 |
| crossings | 300 ms | 0.73 | 70 |
| hit_effects | until idle | 0.19 | 1454 |
| hit_effects | 300 ms | 1204 | 1458 |

On hit_effects a region costs about 1.3 s (F7). A click soon after another plan change still waits for most
of one; Stage N+3's ties by provenance are the cure.

**Every level's frame** (`rc_tool bench --all data/levels --frames 60 --click-frames 4`, synchronous → `--async`;
ms of game time per frame, one run each on the same machine). The plan's exit criterion was no walk or click
frame over 8 ms on any level. The worst asynchronous frame on any level and phase is 1.98 ms (inversion_grid's
cursor orbit: the float64 preview tier itself). The plan click's synchronous worst is the hitch the user saw
(2.0 s on hit_effects):

| Level | player orbit, mean | walk, max | plan click, max | cursor orbit, max | worst async frame |
|---|---|---|---|---|---|
| gravity_test | 0.00 → 0.00 | 0.00 → 0.00 | 0.00 → 0.00 | 0.00 → 0.00 | 0.06 |
| main | 0.79 → 0.05 | 0.86 → 0.12 | 0.03 → 0.03 | 0.71 → 0.14 | 0.14 |
| open_room_playable | 1.09 → 0.05 | 1.26 → 0.08 | 0.07 → 0.06 | 2.53 → 0.95 | 0.95 |
| playground/directional_arcs | 0.61 → 0.03 | 0.83 → 0.06 | 6.03 → 0.21 | 0.67 → 0.10 | 0.25 |
| playground/directional_lines | 0.43 → 0.04 | 0.52 → 0.05 | 15 → 0.15 | 0.53 → 0.08 | 0.24 |
| playground/inversion_grid | 5.28 → 0.40 | 13 → 0.30 | 61 → 0.44 | 14 → 1.98 | 1.98 |
| playground/plannable_inversion | 0.36 → 0.03 | 0.37 → 0.03 | 0.01 → 0.02 | 0.37 → 0.06 | 0.09 |
| playground/portals | 1.89 → 0.05 | 2.97 → 0.07 | 32 → 0.16 | 2.64 → 0.36 | 0.60 |
| playground/pre_inverted | 1.82 → 0.05 | 1.82 → 0.07 | 0.07 → 0.07 | 1.74 → 0.18 | 0.20 |
| playground/projective | 1.69 → 0.18 | 3.45 → 0.25 | 27 → 0.31 | 1.42 → 0.24 | 0.39 |
| playground/projective_arcs | 0.49 → 0.04 | 0.62 → 0.09 | 5.43 → 0.24 | 0.58 → 0.13 | 0.30 |
| playground/projective_concentric | 1.79 → 0.13 | 3.25 → 0.27 | 11 → 0.86 | 1.78 → 0.33 | 0.86 |
| playground/projective_inversion | 0.95 → 0.02 | 1.95 → 0.03 | 27 → 0.18 | 0.80 → 0.14 | 0.28 |
| playground/projective_lines | 0.43 → 0.02 | 0.45 → 0.04 | 15 → 0.17 | 0.49 → 0.09 | 0.17 |
| playground/tangent_infinity | 23 → 0.64 | 47 → 0.81 | 99 → 0.78 | 11 → 0.77 | 1.24 |
| playground/two_sided | 0.86 → 0.04 | 1.10 → 0.05 | 21 → 0.15 | 0.84 → 0.17 | 0.31 |
| test_levels/circle_no_walls | 1.28 → 0.17 | 1.85 → 0.20 | 11 → 0.05 | 1.38 → 0.20 | 0.65 |
| test_levels/combo_base | 0.26 → 0.02 | 0.31 → 0.03 | 0.00 → 0.00 | 0.31 → 0.04 | 0.04 |
| test_levels/crossings | 15 → 0.05 | 25 → 0.07 | 49 → 0.23 | 19 → 0.08 | 0.23 |
| test_levels/divergence_room | 0.32 → 0.02 | 0.36 → 0.03 | 0.01 → 0.01 | 0.38 → 0.05 | 0.11 |
| test_levels/empty_room | 0.26 → 0.02 | 0.29 → 0.02 | 0.00 → 0.00 | 0.29 → 0.05 | 0.05 |
| test_levels/flag_switches | 0.46 → 0.02 | 0.49 → 0.04 | 14 → 0.08 | 0.44 → 0.04 | 0.12 |
| test_levels/full_circle_mirror | 0.66 → 0.13 | 1.50 → 0.20 | 13 → 0.06 | 0.55 → 0.18 | 0.27 |
| test_levels/ghost_room | 0.88 → 0.07 | 1.01 → 0.10 | 31 → 0.15 | 0.88 → 0.12 | 0.16 |
| test_levels/hit_effects | 72 → 0.14 | 107 → 0.34 | 2038 → 0.23 | 65 → 0.23 | 0.34 |
| test_levels/mirror_and_wall | 0.55 → 0.03 | 0.64 → 0.04 | 0.02 → 0.02 | 0.65 → 0.07 | 0.07 |
| test_levels/open_room | 0.34 → 0.03 | 0.39 → 0.04 | 0.00 → 0.00 | 0.37 → 0.06 | 0.11 |
| test_levels/parallel_mirrors | 0.38 → 0.02 | 0.42 → 0.03 | 0.01 → 0.02 | 0.43 → 0.06 | 0.06 |
| test_levels/portals | 1.05 → 0.07 | 2.15 → 0.10 | 16 → 0.79 | 1.77 → 0.85 | 0.85 |
| test_levels/room_boundaries | 0.27 → 0.02 | 0.30 → 0.08 | 0.00 → 0.00 | 0.33 → 0.05 | 0.08 |
| test_levels/room_with_inversion | 0.32 → 0.02 | 0.37 → 0.03 | 0.01 → 0.01 | 0.36 → 0.05 | 0.09 |
| test_levels/room_with_mirror | 0.32 → 0.02 | 0.38 → 0.03 | 0.01 → 0.01 | 0.34 → 0.04 | 0.08 |
| test_levels/room_with_passthroughs | 0.35 → 0.02 | 0.40 → 0.03 | 0.00 → 0.00 | 0.40 → 0.06 | 0.06 |
| test_levels/semi_circle_mirror | 0.67 → 0.14 | 1.70 → 0.29 | 15 → 0.11 | 0.52 → 0.14 | 0.42 |
| test_levels/three_mirrors | 1.00 → 0.05 | 1.80 → 0.06 | 0.05 → 0.05 | 2.46 → 0.83 | 0.83 |
| test_levels/two_mirrors | 0.39 → 0.02 | 0.43 → 0.03 | 0.01 → 0.02 | 0.48 → 0.04 | 0.05 |
| test_levels/two_sided_mirror | 0.34 → 0.02 | 0.39 → 0.04 | 7.95 → 0.09 | 0.39 → 0.06 | 0.09 |

## Findings

| # | Finding | Status |
|---|---|---|
| F6 | The region is computed in the frame (TDD_24, TDD_25) | Resolved: the region is a job |
| F7 | hit_effects: exact ties climb (the preview every cursor frame, the region about 460 times) | Hidden from the frame: the provisional preview, the background region. The cost remains, for Stage N+3 |
| F8 | A region and the preview arrive later than the input that asked for them, and replace what was shown at once | For Stage N+2: the geometric morph and the path morph |
