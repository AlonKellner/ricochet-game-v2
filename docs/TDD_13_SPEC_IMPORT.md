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
| 109.2 | `hit_effects.tscn` and `crossings.tscn` moved to `scenes/test_levels/`: dumps, parity pin (additive), corpus (24 scenarios), sweeps | Done (K6 found, pending the user's decision) |

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

**K6 (pre-existing, found in 109.2, open).** Surfaces sharing one carrier are crossed at one point, but each surface's root is computed from its own defining points.
- **In the oracle**, the roots come out equal (at 120 digits, with integer data), so the lower id comes first.
- **Natively, in float64**, they are an ulp apart and may come in either order.
- **Seen in:** `test_levels__hit_effects__09`, where hosts F1 and G lie on the line x = 1100.
- **Effect:** physically harmless (the extra event is an off-segment pass), but the event order differs between the tiers.
- **Pinned in** `test_stage81_trace_rules.gd` (`K6_PINNED`).
- **Proposed fix (oracle, then native):** recognise coincident carriers exactly.
  - lines: both of one segment's defining points lie on the other's line (`orient_zero`);
  - circles: the same exact circle.
  - Their crossings are then computed once and shared, so they get identical parameters, and the tie is broken by id as the spec already says (§9.3).
- **Needs** the user's approval and a golden diff, because levels with collinear surfaces could change.
