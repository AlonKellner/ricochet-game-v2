# TDD Document 11: Hit Effects (spawn, reset and friends)

**Stage 107** | Surfaces get a second axis of effects. Besides the one **path effect** per side that acts on the beam, a side carries any number of **hit effects**: state changes, targets, spawn and reset. A **spawner** places a new surface at the hit, positioned and oriented by the beam or by the spawner. A **reset** takes it away again.

Specification: GAME_SPEC §9.1, §9.6 (presence) and §10.6 (hit effects, spawn placement).

### Stage Status

| Phase | Topic | Status |
|-------|-------|--------|
| 107.0 | Spec (§9.6, §10.6), this document, the level-defs parity pin | Done |
| 107.1 | Native + oracle: N1 a list of state changes per side, N2 presence, N3 hit provenance (`Event.fired`, `TracePath.hits`) | Done (K4 found on the way, pre-existing) |
| 107.2 | Game layer: `HitEffect` classes, `EffectSpec` / `SideSpec` / `SurfaceSpec`, `level_settings` desugared | Done: every level bit-identical (parity pin), level_settings 259 -> 210 lines, any combination via `surface_specs` |
| 107.3 | Landing: `SpawnFrame`, `SpawnTemplate`, `LandingFold`, `World`, game-manager wiring, checkpoints | Planned |
| 107.4 | Visuals (hit-effect markers, absent outlines, spawn preview), `hit_effects.tscn`, interactive tests | Planned |

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
- `scripts/visual/spawn_preview_renderer.gd`: ghost outlines of what would spawn. It is the same fold as landing, run on a copy of the world, and recomputes only when the inputs change.
- `scenes/test_levels/hit_effects.tscn`.

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
| `test_stage107_spawn_preview.gd` | PREVIEW-EQUALS-LANDING over a grid; the preview never writes |

Native: `test_hit_effects.cpp`. It runs the 11 oracle fixtures at f64, Dual, 4 and 64 limbs (Dual minus K4), plus unit cases for list order, ghost, presence, planning and `plan_refuses`, and the hit records through `Tracer`. Oracle: `gen_fixtures_stage107.py` (asserted generation) and 5 pytest cases. Every older fixture regenerates byte-identical. `tests/support/oracle_fixtures.hpp` now holds the fixture walker shared with `test_infinity.cpp`.

## Invariants

- **SAME-SHOT ISOLATION:** a shot's trace equals the trace of the same shot in a world without its landing parts.
- **PREVIEW-EQUALS-LANDING:** the preview's predicted groups are bit-equal to what landing creates from the fired trace.
- **WORLD-ROUNDTRIP:** `restore(snapshot())` gives the same surfaces (same instances, same ids) and the same flags.
- **Level parity:** the shipped levels' defs do not change in 107.2.

## Known findings

**K4 (pre-existing, found in 107.1, open).** This is a Dual-tier asymmetry.
- **Trigger:** on a physical path with a plan, an unplanned hit that only *writes* (a pass with a state change, reached after the plan is exhausted and before the cursor) diverges from the plan.
- **What differs:**
  - **Dual:** `Backend<Dual>::seed` returns a new beam, so the walker restarts the loop. The cursor candidate, listed while the beam was still on plan, is lost.
  - **f64 and the oracle:** `seed` returns the same beam, so the cursor is kept.
- **Cause:** independent of Stage 107. A single legacy `state` gives the same result.
- **Impact:** the game's f64 path matches the oracle. The Dual tier is the ladder's derivative tier, so the impact there is at most extra climbing.
- **Coverage:** pinned in `test_hit_effects.cpp` (`multi_state_planned_physical` at Dual), so a fix shows up there.
- **Decision:** left for the user, not fixed inside this stage.

## B-list (behaviour changes for approval)

| # | Change | Evidence | Status |
|---|---|---|---|
| H1 | Targets register by the side firing rule (fully blocked) instead of any on-segment hit. They differ only at a shared endpoint that does not fully block. | To collect: the sweep scenes with targets marked, counting differing combos | Pending |

## Interactive User Tests

On `scenes/test_levels/hit_effects.tscn`:
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

To be filled per phase.
