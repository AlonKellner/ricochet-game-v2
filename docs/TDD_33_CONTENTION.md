# TDD 33: A cast without contention (Stage 128)

The ninth stage of the smooth-frames program. Stage 127 left F20 open (TDD_32): planned walks were about 45% of
a region and physical walks about 30%. The two named next steps were a planned walk resumed from the physical one
where they diverge, or a region updated from the last one. The user approved (2026-10-22) "carefully attempt the
structural change".

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 128.0 | The planned walk resumed from the physical one: studied, not bit-exact (no change) | Done |
| 128.1 | A cast without contention: walks allocate less, the region memo sharded (results unchanged) | Done |
| 128.2 | A cast's two walks share their exact start in a pre-transform frame (results unchanged) | Done |
| 128.3 | Docs, `make gates`, sign-off | Done (user, 2026-10-22) |
| 128.4 | The F12 debug dump restored natively (C API 1.5 `rc_game_debug_dump`): a summary printed, the full dump saved | Done |
| 128.5 | A tiny arc on a known circle drawn the long way round: the disc of crossings' mirror circle in the region (the user's F12 report) | Done |

## 128.0 Resuming the planned walk: why not

The planned and physical walks of a cast share a prefix up to the first event where they diverge. Resuming one from
the other would skip that prefix.

It cannot be done bit for bit. The physical mode keeps state the planned mode does not:

- its point groups (B5, events at one drawn point), which start at the walk's first event;
- its seeded beams (`Backend::seed`), which differ from the planned walk's from the first beam on.

A planned walk's state at the divergence is therefore not a physical walk's state there, nor the reverse. Any
resumed walk would need its prefix's state rebuilt, which is the prefix's walk. The structural change was dropped
for the cost underneath it: the same walks, made cheaper.

## 128.1 A cast without contention

**Measured.** On crossings with its inversion circle planned (19L), 8 threads gave only 3.0× over one thread.
Six single-threaded processes run side by side each slowed only 17%, so the loss was inside the process:

- the process's CPU time for the same regions was 1.6× its single-thread time at 6 threads, 1.9× at 8;
- one mutex guarded the region's memo (`SolveMemo`), and its lock covered copying whole ladder results in and out
  (10% of the active samples were waits on it);
- malloc was 12% of a cast on one thread and 19% across threads (583 allocations a cast).

**Changes**, each the same operations with fewer copies or less sharing:

- **The memo** (`vis::SolveMemo`): sixteen shards, each under its own mutex. A solve's result is held by a shared
  pointer and read in place, never copied under a lock. Timed only under `RC_VIS_MEMO_STATS`.
- **The walk** (`Walk`):
  - its per-step vectors are leased from a thread-local pool (`StepScratch`);
  - its per-surface memos are inline (`SmallMap`) instead of `std::map`s;
  - its beam views are a flat vector.
- **The path conversion** (`Tracer::to_path_events`):
  - an event's at most two steps are inline;
  - the frame's key and id are rebuilt only when the frame changes (an applied hit or a flags change), not per
    event;
  - `TracePath::event_frames` index `frame_keys`. `cursor_point_agrees` compares the same strings.
- **The aim image's cache key** is the exact bits of its numbers (`SolveCache::ImageKey`). The `%.17g` text it
  replaces told every number apart the same way, at a `snprintf` per cast.

Allocations per cast on crossings 19L: 583 → 212.

## 128.2 A cast's two walks share their exact start

On a level that starts in a pre-transform frame (tangent_infinity, pre_inverted), an aimed walk starts with the
exact pole and circle through the player, the exact aim image and Q0 (Stage 105, K9). `exact::gc_through` alone was
40% of a tangent_infinity region: each cast computed it twice, once per walk. Both are pure functions of the three
points. `SolveCache` keeps the last few, keyed by the points' exact bits (sign, exponent and limbs), and the
realization reads the same entries.

## 128.4 The F12 debug dump

The user reported (2026-10-22) that F12 no longer dumps. The dump was the GDScript game's (`DebugDumper`). It was
deleted with that game in the engine switch (Stage 119.4), and the native host never got one; the GUT ledger
tracked tests, not tools. It is now engine-free:

- `Session::debug_dump()` (`session_debug.cpp`) gives JSON: the level, the player and cursor (hex floats too: a
  dump is replayed bit for bit), the switches, the flags, the plan and the solver's plan items, the scene's defs,
  the preview's ladder result (each walk's limbs, convergence and events), its traced pair and merged steps, and the
  region's summary.
- `Session::debug_summary()`: a few lines -- the inputs (hex floats too), the plan, the preview's walks, the merged
  steps as runs of one type, the cursor as the preview and the region classify it (flagged `DISAGREE` where they
  differ: VISIBILITY_EQUALS_NON_DIVERGENCE), the region.
- C API 1.5 `rc_game_debug_dump(game, flags, ...)`: the JSON, or with `RC_DEBUG_SUMMARY` the summary.
- The Godot host prints the summary on F12 and saves the full dump to `user://debug_dumps/` (the full dump in
  Godot's output overflowed it, the user, 2026-10-22).

## 128.5 A tiny arc drawn the long way round

**The report** (the user, 2026-10-22, with an F12 dump): on crossings with the mirror circle 17 planned from
outside (its western half), the region invaded the circle, where every point is divergent.

**Replayed exactly** from the dump: 1512 of 1513 points sampled in the disc were in the region and none was shown
by the preview. The same on main (Stage 127): the defect predates this stage. One cell of the reversed half (a
sliver between members 69, an endpoint of 16, and 70, a tangency to 16) closes on carrier 17 with an arc 1.5e-6 px
long. Its three points lie within rounding of a line: `cross(via - a, b - via)`, which `arc_geom` turned into the
arc's direction, was noise, and the wrong sign made the arc sweep -2π: the whole circle. `on_arc_side` had the same
weakness (the via's side of the chord).

**The fix** (`vis::ccw_about`): an arc on a known circle turns the way its via lies between its ends in angle about
the centre (1.7e-8 rad apart here, well above rounding); its side of the chord is the centre's, opposite for a minor
arc. Where the cross product is reliable the two agree. The region's edges are unchanged (the 2010-region set bit
for bit, the digests, the sweeps, the sessions); what changes is how such an arc is read -- its fill, its
membership. No arc of the 2010-region set was read the other way: the sweeps' players never met it.

## Results

**Results unchanged.** Every region of the 2010-region set of Stage 127 (every side and some two-entry plans of
every level, six players each) is bit for bit the same. So are the digests, the sweeps (unsampled) and the sessions.

**The worst side per level** (a region while walking, ms, 8 threads; Stage 127's tool, both builds measured
together):

| Level | Stage 127 | Stage 128 |
|---|---|---|
| crossings | 23.6 | 13.5 |
| tangent_infinity | 21.9 | 14.4 |
| hit_effects | 21.2 | 15.5 |
| playground/portals | 19.4 | 12.7 |
| projective | 18.7 | 16.3 (F21) |
| pre_inverted | 16.4 | 11.5 |
| two_sided | 15.4 | 10.0 |
| main | 13.6 | 8.6 |
| inversion_grid | 11.5 | 7.7 |
| projective_inversion | 8.9 | 6.6 |
| projective_concentric | 8.6 | 4.7 |
| semi_circle_mirror | 7.7 | 4.7 |
| plannable_inversion | 7.3 | 4.5 |
| room_with_inversion | 7.0 | 4.0 |
| full_circle_mirror | 5.4 | 3.4 |

Levels with a side over 4 ms: 18 of 37 → 14. On one thread a region is about 25% faster (crossings 19L: 61.5 → 45.5
ms). Threads gain more: the process's CPU time at 8 threads was 2.2× its single-thread time and is now 1.9×. The
measurements vary ±2 ms between runs.

## Findings

| # | Finding | Status |
|---|---|---|
| F20 (TDD_32) | The remaining cost per region | Reduced by contention and allocations (128.1, 128.2). The planned walk resumed from the physical one is not bit-exact (128.0). A region updated from the last one stays open |
| F21 | projective 9R: four casts a region (cursors at line 6's endpoints) pass exactly through the crossing of carrier 6 (the line x = 700) and carrier 9 (the projection circle), at (700, 750) and (700, 250). The two hits are one point, a margin of exactly 0: the ladder climbs all six tiers and takes the 64-limb walk unconverged, about 4 ms each, 17 ms of a 56 ms region on one thread. The slowest cast of a batch, so 4 ms of every region at 8 threads | Open: a tie at a crossing the beam passes exactly, decided by provenance (the known-equal points rule), would remove the climbs. A behaviour change: spec, oracle, judge |
| F22 | Threads still cost: the process's CPU time at 8 threads is 1.9× its single-thread time (separate processes: 1.17×). malloc remains (212 allocations a cast) | Open |

## Tests

| Test | What it checks |
|---|---|
| the region digests, the sweeps (unsampled), the sessions | unchanged |
| the 2010-region comparison against Stage 127 (scratch) | every region bit for bit |
| `debug dump: the inputs exactly, the scene, the preview's walks and the region` | the dump's exact inputs and its parts |
| `capi_c_smoke` | `rc_game_debug_dump` (1.5), the summary, the error path |
| `debug dump: the summary names the inputs and the cursor as the preview and the region classify it` | the summary |
| `visibility: a tiny arc on a known circle turns the short way` | the dump's arc: its sweep, its side (the old rule swept -2π) |
| `region reports: crossings [17 L] -- the mirror circle's disc is not in the region` | the report replayed: no point of the disc in the region |
