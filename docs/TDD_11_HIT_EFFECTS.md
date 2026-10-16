# TDD Document 11: Hit Effects (spawn, reset and friends)

**Stage 107** | Surfaces get a second axis of effects. Besides the one **path effect** per side that acts on the beam, a side carries any number of **hit effects**: state changes, targets, spawn and reset. A **spawner** places a new surface at the hit, positioned and oriented by the beam or by the spawner. A **reset** takes it away again.

Specification: GAME_SPEC §9.1, §9.6 (presence) and §10.6 (hit effects, spawn placement).

### Stage Status

| Phase | Topic | Status |
|-------|-------|--------|
| 107.0 | Spec (§9.6, §10.6), this document, the level-defs parity pin | Done |
| 107.1 | Native + oracle: N1 a list of state changes per side, N2 presence, N3 hit provenance (`Event.fired`, `TracePath.hits`) | Done (K4 found on the way, pre-existing) |
| 107.2 | Game layer: `HitEffect` classes, `EffectSpec` / `SideSpec` / `SurfaceSpec`, `level_settings` desugared | Done: every level bit-identical (parity pin), level_settings 259 -> 210 lines, any combination via `surface_specs` |
| 107.3 | Landing: `SpawnFrame`, `SpawnTemplate`, `LandingFold`, `World`, game-manager wiring, checkpoints | Done |
| 107.4 | Visuals (hit-effect markers, absent outlines, spawn preview), `hit_effects.tscn`, interactive tests | Done pending the user's interactive sign-off (K5 found on the way, pre-existing) |

**Regression Test Policy:** as in TDD_10. Before every commit:
- the full GUT suite passes (`./run_tests_parallel.sh`);
- ctest passes in RelWithDebInfo and under ASan (`make native-asan`, run alone);
- `make oracle-test` passes;
- `git diff --stat bench/goldens` is empty.

**Workflow:** branch `stage107-hit-effects`, with small green commits. Any observable behaviour change is brought to the user with evidence before it is applied; see B-list items below.

## Overview

### The two axes

```
Surface
  presence: Array[PresenceCond]      # flag-gated existence (AND), empty = always (§9.6)
  left/right SideConfig:
    effect: Effect                   # path axis: exactly one (null = pass)
    hit_effects: Array[HitEffect]    # hit axis: ordered, any number, any mix
    interactive: bool
```

### Timing

- **IN_SHOT (the default).** A hit effect is IN_SHOT whenever it has an elegant in-shot form, which is a flag write the solver applies during the walk.
- **ON_LANDING.** This is only for what has none. Today that is the placement of a spawned surface: its geometry comes from the same shot, which is the interdependence the design forbids.
- **Deferral.** Any IN_SHOT effect may be `deferred` per instance. It then fires by the same rule but applies only when the arrow lands.

| HitEffect | IN_SHOT part | ON_LANDING part |
|---|---|---|
| `StateChangeHit(key, op, value)` | the write | the write, if deferred |
| `TargetHit` | `SET target:<id> = true` | the write, if deferred |
| `ResetHit(channels)` | `SET chan:<c> = "armed"` for each channel | the writes, if deferred |
| `SpawnHit(channel, template)` | `SET chan:<c> = "pending"`, which consumes the hosts | always: place the template at the firing hit |

### Channel lifecycle

The flag `chan:<c>` takes the values `missing | "armed" | "pending" | "g<N>"`.
- **Hosts:** presence `chan:<c> ∈ {DEFAULT, "armed"}`.
- **Spawned group N:** presence `chan:<c> == "g<N>"`.
- **At landing:** a channel still `"pending"` gets group N+1, placed at the hit that last fired its spawn. The flag becomes `"g<N+1>"`.

### Why the effects system needed this

The path axis was already clean: `SideConfig.effect` maps to the def `type`, and that maps to the native `EffectType`. The hit axis was ad hoc:
- One `state_change` per side, enforced natively (`SideDef::state` is `std::optional`) and in the oracle.
- `is_target` as a surface-level bool. It was collected natively and never read by the game.
- Ghost togglers as a reserved key plus a colour special case.
- The solver could only make a side *pass*, never make a surface *absent*. Categorical sides cannot be portals or projectives.
- In the game layer, every combination was a copy-paste loop in `level_settings.gd`, and `SceneDefs.describe_config` was an `if/elif is` chain.

## Prerequisites

Stages 85 (flags, resolvers), 86 (ghost), 100 (dirty keys, `SceneDefsCache`) and 101–106.

## What Is Introduced

### 107.1 Native and oracle

The oracle (`docs/research/oracle/trace2.py`) is the specification: it changes first, then C++.

- **N1: a list of state changes per side.**
  - The def becomes `states: [...]`; the legacy `state` still parses.
  - `SideDef`, `Surf` and the categorical `TableEntry` hold vectors, applied in order.
  - `ghosted()`: a side whose changes include a write to `ghost` is a ghost toggler.
- **N2: presence.**
  - The def gets `presence: [{key, values}]`.
  - An absent surface (under the walk's current flags) has no candidates, takes no part in endpoint blocking, and is refused by planning.
- **N3: hit provenance, recorded per hit event.** The tracer does not emit zero-length steps, so a per-step field could miss a hit. Each hit event therefore gets its own record.
  - `Event.fired` (oracle and native): the side fired. That means PHYSICAL fully blocked, or PLANNED the consumed entry, and not ghosted. It differs from `state`, which is false for a side with no in-shot writes.
  - `TracePath.hits`: one record per hit event, in order. It is `{surface_id, side, on_segment, fired, state, point, tangent, flags_before, step_index}`.
    - `tangent` is the beam's unit travel direction at the hit, from the exact beam.
    - `flags_before` are the flags the side was resolved under.
  - The landing fold reads these and never re-derives what the solver decided. Spawn placement uses `tangent`, so there is no reconstruction from step geometry and no zero-length fallback.

### 107.2 Game layer (no behaviour change)

- **`scripts/game/hit_effects/`:** `HitEffect`, `StateChangeHit`, `TargetHit`, `ResetHit`, `SpawnHit`, `SpawnTemplate`.
- **`SideConfig.hit_effects`** replaces `state_change`. `Surface.is_target` is derived from `TargetHit`.
- **`scripts/math/effects/effect_spec.gd`:** the registry of path effects keyed by def `type`, with each one's carrier requirement and builder. `Effect.to_spec()` replaces the `if/elif` chain.
- **`SideSpec` / `SurfaceSpec`:** Resources with `build()` / `of()` / `mapped()`.
- **`level_settings.gd`:** the legacy arrays become table rows that build `SurfaceSpec`s. The new `surface_specs` export allows any combination.

### 107.3 Landing

- **`scripts/math/spawn_frame.gd`:** the placement frame (GAME_SPEC §10.6.1).
- **`scripts/game/landing_fold.gd`:** a pure fold over the physical path.
- **`scripts/game/world.gd`:** the static surfaces plus hosts plus groups. `surfaces()` returns a new array on every structural change, `land()`, `snapshot()` / `restore()`.
- **`game_manager.gd`:** the landing order is fold, promote flags, targets, prune plans, nodes, redraw. `CheckpointData.world_state`.

### 107.4 Visuals

- Hit-effect markers on `SurfaceNode`, with deferred effects marked distinctly.
- Absent surfaces drawn as faint dashed outlines.
- `scripts/game/spawn_preview_node.gd` (`SpawnPreviewNode`). It lives in the game layer because it folds game-layer surfaces, and the visual layer must not depend on the game layer.
  - It draws ghost outlines of what would spawn, and red dashed marks on what would vanish.
  - It is the same fold as landing, over the path renderer's own trace (`PathRenderer.last_pair`). It builds no `Surface`, which would consume ids, and writes nothing.
  - It recomputes only when the trace or the world changes.
  - A level that can spawn adds it automatically.
- `scenes/stage107/hit_effects.tscn`, generated by `scripts/testing/make_hit_effects_level.gd`. It sits outside `test_levels/` and `playground/` because the oracle's `.tscn` importer and the scene dumps do not read `surface_specs` sub-resources yet (follow-up). Its own test sweeps it, both as built and after spawns.

## Unit Tests Added

| File | Covers |
|---|---|
| `test_stage107_level_parity.gd` | Every level's scene defs are bit-identical through the refactor (`tests/data/stage107_level_defs.json`, `DefsCanon`) |
| `test_stage107_multi_state.gd` | Two writes on one side, their order, ghost in a list, a planned multi-write entry |
| `test_stage107_presence.gd` | Vanishing and appearing mid-shot, endpoint blocking, plan refusal, a portal pair's presence |
| `test_stage107_provenance.gd` | `TracePath.hits` on presence, ghost and categorical scenes; `plan_refuses` on absent surfaces |
| `test_stage107_hit_effects.gd` | Spec round trips, the in-shot/deferred compile, spawn's landing part |
| `test_stage107_surface_spec.gd` | Round trips for every kind, carrier validation, transforms |
| `test_stage107_spawn_frame.gd` | Every mode on line and arc hosts, sign conventions, exact placement |
| `test_stage107_landing_fold.gd` | Spawn, re-hit, in-shot and deferred reset, respawn in one shot, shared channels, ghost, categorical resolution, order, purity |
| `test_stage107_world.gd` | Same-shot isolation, the next shot interacts, path effects plus spawn, undo/reset, plan pruning, cache invalidation, targets |
| `test_stage107_spawn_preview.gd` | PREVIEW-EQUALS-LANDING over a 30-aim grid × 2 shots; the preview never writes (world, flags, id counter) |
| `test_stage107_level.gd` | The interactive level builds and its scripted interactions work. It passes the invariant sweep as built and after spawns, with K5 pinned. K5 also has a minimal reproduction using only legacy exports |

Native: `test_hit_effects.cpp`. It runs the 11 oracle fixtures at f64, Dual, 4 and 64 limbs (Dual minus K4), plus unit cases for list order, ghost, presence, planning and `plan_refuses`, and the hit records through `Tracer`. Oracle: `gen_fixtures_stage107.py` (asserted generation) and 5 pytest cases. Every older fixture regenerates byte-identical. `tests/support/oracle_fixtures.hpp` now holds the fixture walker shared with `test_infinity.cpp`.

## Invariants

- **SAME-SHOT ISOLATION:** a shot's trace equals the trace of the same shot in a world without its landing parts.
- **PREVIEW-EQUALS-LANDING:** the preview's predicted groups are bit-equal to what landing creates from the fired trace.
- **WORLD-ROUNDTRIP:** `restore(snapshot())` gives the same surfaces (same instances, same ids) and the same flags.
- **Level parity:** the shipped levels' defs do not change in 107.2.

## Known findings

**K4 (pre-existing, found in 107.1, fixed with the user's approval on 2026-10-16).**
- **The bug:** at the Dual tier, a divergence that seeds the beam restarted the loop, and the recomputed candidates no longer listed the cursor, because the beam was now off plan. f64 and the oracle kept the cursor listed at the loop's start.
- **The fix:** a seed changes derivatives only, so it must not change a decision. The restarted loop recomputes its candidates on the seeded beam, so their derivatives are kept, and keeps a cursor the loop still listed (`candidates(..., keep_cursor)`).
- **Evidence:** all 178 oracle fixtures give identical results at Dual and f64. The goldens are byte-identical, and ctest, ASan and the full suite pass. The pin in `test_hit_effects.cpp` is now a plain Dual check.

**K5 (pre-existing, found in 107.4, fixed with the user's approval on 2026-10-16).**
- **The bug:** a carrier through a portal end's departure point (another surface's line or circle beyond its segment, not a crossing of segments) made the first event after a teleport zero-length. `to_path_events` reset its `gap` per event, so the step after the teleport lost its `after_portal` mark, and NOGAPS / PHYSICAL-CONTINUITY reported a gap.
- **The fix:** an unconsumed mark waits for the next drawn step (`gap = gap_pending`, then `gap || e.teleport`).
- **Evidence:** all 284,096 pinned sweep traces replay byte-identical (`step_canon` includes `after_portal`), and every golden file is unchanged.
- **Tests:** the minimal reproduction (legacy `portal_lines` and `passthrough_lines`) now asserts the mark. The level sweep tolerates nothing, and its after-spawns case contains the original y = 220 coincidence.

Separately, the user asked for two crossing mechanisms. They make crossing surfaces designable, which levels forbid today, and keep spawned surfaces from creating crossings. They are Stage 108.

## B-list (behaviour changes for approval)

| # | Change | Evidence | Status |
|---|---|---|---|
| H1 | Targets register by the side firing rule (fully blocked, not ghosted) instead of any on-segment hit. **Approved 2026-10-16.** | Every shipped level, every surface treated as a target, players and cursors on a 5x5 grid plus every segment endpoint: 275,767 hit records, 151,513 on segment. Of those, 15,020 are on segment but do not fire (a junction that does not fully block, e.g. an aim exactly at a room corner; the sample is biased toward endpoints on purpose) and 1,713 are ghosted passes (the old rule counted a ghost passing a target). The game never read `targets_hit` before Stage 107, so nothing observable changes today. The alternative is a `TargetHit` keeping the on-segment rule (an exception to the one firing rule). | Approved |

## Interactive User Tests

On `scenes/stage107/hit_effects.tscn` (open it in the editor and run the scene). The player starts at (300, 500).

Every spawn is centred on the hit point, except host A, which keeps an offset as the example (`SpawnTemplate.offset`). The level contains:
- **The column x = 600, one host per required mode:**
  - A: beam tangent, a mirror along the shot, offset 100–220 px ahead.
  - B: beam normal, a mirror across the shot.
  - C: carrier tangent, on a terminal host, a mirror along the host.
- **Arc D (bottom left):** carrier normal, spawns an inversion circle at the hit. It is a reflective host.
- **Portal E (x = 1000 to 1300, top):** spawns a one-sided mirror across the shot, mirrored by the side hit.
- **F1 / F2 (x = 1100):** share a channel and spawn a block circle at the hit.
- **G (x = 1100, bottom):** target + toggle `open` + spawn (a mirror along the host).
- **H (x = 1350):** blocks while closed, and spawns while `open` (a mirror across the reflected direction).
- **The floor line (y = 850):** resets A–D in the shot.
- **The right line (x = 1600, y 200–450):** resets E–H at landing (deferred). Shots below it miss it.

1. **Preview.** The ghost outline shows where the surface will spawn, for each orientation mode, and moves with the aim.
2. **Spawn.** Shooting through a pass spawner makes the surface appear when the arrow lands. The arrow itself ignored it, even when the new surface lies across its path.
3. **Next shot.** The next shot interacts with the spawned surface: it reflects, blocks or teleports as configured. A spawned mirror can be planned.
4. **Host effects.** A reflective spawner reflects *and* spawns. A terminal spawner stops the shot and spawns. A portal spawner teleports and spawns.
5. **In-shot reset.** With a spawned surface standing, a shot through a reset then through the spawned surface's position passes it. The spawner is back.
6. **Deferred reset.** It does not affect the shot that hits it, and takes effect when the arrow lands.
7. **Combinations.** A shared channel works, and so does a target + toggle + spawn on one side.
8. **Undo and reset.** Z restores the previous world and R restores the initial one. The plan survives.
9. **Markers** read clearly.

## Regression Checklist

- The full GUT suite, ctest (RelWithDebInfo and ASan), the oracle tests and the level parity all pass.
- Goldens are byte-identical.

## Files Modified

- **Spec and docs:** `GAME_SPEC.md` (§9.1, §9.6, §10.6, §24.2, the status table), this document.
- **Oracle:** `docs/research/oracle/trace2.py` (state lists, presence, `Event.fired`), `test_trace2.py`, `gen_fixtures_stage107.py` (new), `gen_fixtures_stage85.py` (an `event_extra` hook), `tscn_import.py` (`states`).
- **Native:**
  - `core/include/rc/{scene,flags,solver,tracer,trace_types}.hpp`
  - `gd/{gd_common,gd_scene,gd_solver,gd_tracer}.{hpp,cpp}`
  - `tests/test_hit_effects.cpp` (new), `tests/support/{json_defs,oracle_fixtures}.hpp`, `tests/test_infinity.cpp`
- **Math:** `scripts/math/effects/effect_spec.gd` (new); `to_spec()` on every effect; `scripts/math/spawn_frame.gd` (new).
- **Game:**
  - `scripts/game/hit_effects/` (new: `HitEffect`, `StateChangeHit`, `TargetHit`, `ResetHit`, `SpawnHit`, `SpawnTemplate`, `Channel`, `HitEffectSpec`)
  - `presence_cond.gd`, `side_spec.gd`, `surface_spec.gd`, `landing_fold.gd`, `world.gd`, `spawn_preview_node.gd` (new)
  - `side_config.gd`, `surface.gd`, `scene_defs.gd`, `scene_defs_cache.gd`, `level_settings.gd`, `game_manager.gd`, `checkpoint.gd`, `plan_manager.gd`, `surface_node.gd`
- **Visual:** `scripts/visual/path_renderer.gd` (`last_pair`).
- **Levels:**
  - `scenes/stage107/hit_effects.tscn` and its generator `scripts/testing/make_hit_effects_level.gd`
  - `bench/scenes/` (the two toggler dumps: `state` → `states`)
  - `bench/fixtures/stage107_hit_effects.json`, `tests/data/stage107_level_defs.json`
- **Tests:** `tests/test_stage107_*.gd` (12 files); the ports in `test_stage55_chevron_overlays.gd`, `test_stage85_state_changes.gd`, `test_stage86_ghost.gd`.
- **Runners:** `run_tests_parallel.sh` and `Makefile`. The level sweep is a sharded sweep file, which adds about 8 s of wall time to the parallel suite (17 s → 25 s).
