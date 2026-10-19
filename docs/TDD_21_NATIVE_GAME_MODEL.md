# TDD 21: The native game model (Stage 116)

The second stage of the engine-agnostic port (the plan: TDD_20's introduction). The game's model and play
logic -- surfaces, sides, resolvers, hit effects, presence, the world over time, pieces and crossings, the
landing, the plan, clicks, overlays, checkpoints and the controller state machine of `game_manager.gd` --
move to the engine-free game layer (`native/game`, `rcg::`) as `rcg::Session`, bit-identical to the
GDScript game. The oracle of the port is the session replay golden (`bench/sessions`, TDD_20): `rc_tool
replay` plays every teleport session natively and must reproduce every record byte for byte. The GDScript game
keeps running unchanged during this stage (no facades); differential probes compare the ported units with
their GDScript originals on every level's surfaces and seeded random inputs. No behaviour changes.

Background: TDD_20 (the session golden, the level data, `rc_tool`), docs/research/session_digest.md.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 116.0 | Branch `agnostic-model`, this doc | Done |
| 116.1 | Core gains the logic that lived only in the bindings (the bindings delegate; the bindings' pure duplicates of core die with `native/gd` in the last stage): dashing (`VisualConverter.dash_arc` / `dash_line`), the display pieces of `prepare_for_display`, `TracePath.physical_hits`, `BeamGeom.hit_records`' provenance snap; one `events_of`. As surveyed: core already held the preview pipeline (`rc::sweep::prepare_for_display`, the pieces, `merge`); of the binding-only logic the game calls only `physical_hits` and the dashing -- now `rc::physical_hits` (the surfaces in first-hit order, as the Dictionary kept them) and `rc::arc::dash_arc` / `dash_line`, the bindings delegate; the rest (`hit_records`, `seg_from_*`, the raw-beam math) only the binding tests use: it goes with `native/gd` | Done |
| 116.2 | The numerics contract: `rc/xf2.hpp` (Transform2D in float32), the Godot helpers the model uses, Godot-recorded references; the differential probe binding (`RcgProbe`) | |
| 116.3 | The surface model: side configs, resolvers, presence, state changes, hit effects, channels, side links (overlay links); the single defs reader (`json_defs.hpp` -> `rcg`) | |
| 116.4 | Geometry and the world: crossings (float64), pieces, the spawn frame, the landing fold (place, trim), the world (groups, generations, snapshots), checkpoints | |
| 116.5 | Play: the plan (`snap_click`), the click detector, the overlays, the HUD text, `rcg::Session` (the controller), the session digest, `rc_tool replay`, ctest `rcg_sessions_{k}`: every teleport session byte-identical | |
| 116.6 | The sweep manifests natively (from 115.4): Godot's RandomNumberGenerator reproduced (a recorded reference first), the sweep positions and plans, `rc_tool manifests`, the manifests gate without Godot | |
| 116.7 | Docs, `make gates`, interactive sign-off (the game unchanged) | |
