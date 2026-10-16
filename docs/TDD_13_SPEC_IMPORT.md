# TDD Document 13: The Oracle Reads Surface Specs

**Stage 109** | The oracle's `.tscn` importer reads `LevelSettings.surface_specs`, so levels built from surface specs join the shipped test levels:
- the scene dumps,
- the importer round trip,
- the level-parity pin,
- the scene corpus,
- the invariant sweeps.

Background: TDD_11 (Stage 107) and TDD_12 (Stage 108). Before this stage, their levels lived in `scenes/stage107/` and `scenes/stage108/`.

### Stage Status

| Phase | Topic | Status |
|-------|-------|--------|
| 109.1 | `tscn_import.py`: Godot's sub-resource syntax, and `SurfaceSpec.build` / `SceneDefs` replicated | Done |
| 109.2 | `hit_effects.tscn` and `crossings.tscn` moved to `scenes/test_levels/`: dumps, parity pin (additive), corpus (24 scenarios), sweeps | Done (K6 found and fixed) |

## What Is Introduced

- **`docs/research/oracle/tscn_import.py`:**
  - A parser for Godot's text Variant syntax: dicts, arrays, typed arrays (`Array[Dictionary]`, `Array[ExtResource(...)]`), constructors, `SubResource` and `ExtResource`.
  - `[sub_resource]` blocks, with the `SurfaceSpec` / `SideSpec` defaults Godot omits.
  - `build_spec`, which replicates `SurfaceSpec.build` and `SceneDefs.describe_surface`:
    - geometry: lines, arcs, full circles, straight lines;
    - path effects;
    - in-shot hit-effect writes (`states`; deferred ones omitted);
    - targets;
    - presence, plus a spawn host's channel presence;
    - categorical tables;
    - portal pairs.
  - It runs after the legacy arrays, in `LevelSettings`' order.
- **`gen_scene_corpus.py --scenes NAME...`:** generates only the named scenes. The others' files are untouched, and the per-scene seeds already guarantee it.
- **The two levels moved** to `scenes/test_levels/`, with their generators and tests following.
  - The general invariant sweep now covers them as built. Their own sweep files keep only the world after spawns.
  - The NO-PIECE-CROSSINGS check needs no extra folders.
- **The level-parity pin** gained the two levels. Additive only: no existing entry changed.

The existing dumps were not regenerated. Re-dumping them only reorders keys: since Stage 107, `type` comes before `interactive`, with the same content.

## Known findings

**K6 (pre-existing, found in 109.2, fixed with the user's approval on 2026-10-16).**
- **The bug:** surfaces sharing one carrier are crossed at one point, but each surface's root was computed from its own defining points. In the oracle they came out equal (at 120 digits, with integer data); natively, in float64, they were an ulp apart and came in either order.
- **The fix (oracle, then native):** surfaces on exactly the same carrier are grouped by the exact predicates (`same_carrier` / `carrier_groups`: lines by `orient_zero`; circles by `incircle_zero` / `on_circle_zero`, or an equal centre and radius).
  - Each loop computes a group's roots once and shares them. They are computed on the departing member's own segment when the beam departs that carrier, so a lone surface is computed exactly as before.
  - Simultaneous crossings therefore tie exactly, and the id breaks the tie (GAME_SPEC 9.3).
- **Evidence:**
  - every oracle fixture regenerates byte-identical;
  - all 284,096 pinned sweep traces and every golden file are unchanged, and ctest and ASan pass;
  - the scene corpus changed only for `hit_effects`. Before the fix its generator had rejected two draws as unstable (this tie); now they are stable, so its scenarios from 06 on are a new, fully matching set;
  - all 420 corpus scenarios match the oracle.
- **Tests:** oracle `test_trace2.py` (three collinear surfaces tie by id; circle groups) and native `test_crossings.cpp` (the same at f64, Dual and 4 limbs). The pin is removed.
