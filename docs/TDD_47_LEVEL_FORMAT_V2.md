# TDD 47: The level format `rc-level/2` (Stage 142)

The editor program's first stage (the plan: a level editor that edits segments, their carriers and points,
each side's effects, the objective and everything else a level holds, visually and as text; the engine a thin
host). Before an editor can write levels, levels need a form worth writing: `rc-level/1` spells every surface
with both sides, lists the room's walls and the four edge walls, and has no initial flags. `rc-level/2` is a
list of **elements** with names and parameters, each kind a **data template**, so the text form is short and
new elements are designed without code (docs/ELEMENTS.md). The proof that nothing changed: every level
migrated, and its lowering, its dump, its sessions and its manifests are what they were, byte for byte.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 142.0 | The survey: the levels' surface shapes (44 distinct), the room's walls (always the generated ones), the edge walls' places (last, after the room in four levels, after two elements in `ghost_room`) | Done (below) |
| 142.1 | The template engine (`rcg/elements.hpp`): parameters, `$p`, `${p}`, `$switch`; expansion and its inverse (matching) | Done |
| 142.2 | The builtin kinds, `data/elements/*.json` (17), embedded at build (`native/cmake/embed_elements.cmake`) | Done |
| 142.3 | The lowering and the migration (`rcg/level2.hpp`), the reader on both schemas, the initial flags in the session | Done |
| 142.4 | `rc_tool lower`/`migrate`, `fmt` on `rc-level/2`; `bench/lowered` and its gate; the oracle and the kinematics judge read the lowerings | Done |
| 142.5 | The 37 levels migrated; the tests; the docs | Done (`make gate` ok) |
| 142.6 | `make gates`, the user's review of the format and sign-off | Done (`make gates` ok; the user's sign-off 2026-10-24) |

## 142.0 The survey

Over the 37 levels, the 398 surfaces fall into 44 shapes once their geometry is set aside. The four edge walls
of the bounds (148 surfaces) and the room's walls (in every built room exactly the generated ones: terminal,
player-solid, label `Surface`, top-right-bottom-left with the middle points as vias) are 60% of them. The rest
are mirrors (one-sided, `Mirror`/`MirrorR`; two-sided, `MirrorB`/`ReflArc`/`Spec`), blocks, inversions,
projections of four types, portals, ghost togglers, togglers, gates and the hit-effect showcases.

The edge walls are not always last: `crossings`, `flag_switches`, `hit_effects` and `room_with_passthroughs`
list them right after the room, `ghost_room` after two elements. Ids follow the order, so "the edge walls
last" would renumber those levels; an `edges` element places them where it stands.

## 142.1-142.3 The format

The template language and the kinds: docs/ELEMENTS.md. The format: docs/LEVEL_FORMAT.md. Decisions:

- **Ids are derived, names identify.** No ids and no id counter in the file (§19.2's counter dropped): the
  lowering numbers the surfaces in build order, exactly as `rc-level/1` lists them, which is what keeps every
  dump identical. An element's identity, in the editor and in derived flags (`door:<name>`), is its name.
- **Every surface kind is a template**, the core ones too, so the migration's recogniser is generic: `match`
  runs a template backwards against a run of surfaces (unification; `$switch` tries the default's case first;
  `${p}` splits strings), then expands the result and keeps it only if it is the run exactly. The parameters
  that the expansion does not need (at their defaults) are dropped one by one under the same check.
- **Labels are expressions.** `inversion` labels itself `FullInversion` or `Inversion` by `sides`, a projection by
  its type and `back`, a `switchable` `Gate` or `FlagMirror`; a label that differs is the element's `label`.
- **Raw is the escape hatch**, an `rc-level/1` surface as is. Three remain, all in `test_levels/hit_effects`: a
  portal that spawns, a target that also toggles and spawns, and a resolver whose open state spawns.
- **Initial flags** (`flags`) are new: the session starts with them and its initial checkpoint holds them, so a
  full reset restores them. No level used them before, so nothing recorded changes.
- **Reserved keys**: `carriers`, `detectors`, `objects`, `objective` are refused until their stages.
- **The oracle reads the lowerings** (`bench/lowered`, committed and gated current like the dumps), so its
  builder stays an independent check of `rc-level/1` -> defs, while the element expansion is pinned by the dumps.

Deviations from the program's plan: the editor's typed document (`rcg::Document`) moves to the editor stage,
which needs it; this stage works on the JSON. A spawner's placed surface is an `rc-level/1` surface parameter,
not a nested element (the migration matches it as data). `breakable` and `toggle_surface` wait for the
novel-element stage, where they are played, not only expanded; `door` (presence), `button`, `target`, `fence`
and `arrow_block` ship now (their expansions tested).

## 142.5 The results

| | Before | After |
|---|---|---|
| level files' lines | 9766 | 2506 |
| `test_levels/hit_effects` | 1051 | 529 |
| `playground/projective` | 368 | 91 |
| elements | -- | 132 (3 raw) |

The elements by kind: mirror 26, two_sided_mirror 21, inversion 17, projection 17, wall 15, spawner 7, portal 5,
edges 5, arrow_block 4, resetter 3, raw 3, ghost_gate 2, passthrough 2, pre_inversion 2, switchable 2, button 1.

Bit-exactness: `rc_tool lower` before the migration (on the `rc-level/1` files) and after it give the same
`bench/lowered`; `make dumps`, `make manifests` and the session replays (every host) are unchanged; `make gate`
ok (core, oracle, dumps, lowered, manifests, ctest, Python host, Godot suite, goldens).

## Tests

- `rcg_tests` "rc-level/2: every level lowers to its committed lowering, and migrates back from it" (and builds
  the same surfaces from either form).
- "rc-level/2: every builtin template expands to surfaces that build" (and docs/ELEMENTS.md lists every kind).
- "rc-level/2: the expansions": mirror and flip, door and button, labels, ids and the edges element, a level's
  own template and an override, migration with a raw run and its refusal of non-consecutive ids.
- "rc-level/2: the schema is strict, and names the element": unknown and reserved keys, unknown kinds and
  parameters, enums, required parameters, duplicate names, geometry, a raw surface's errors, a template's errors.
- "rc-level/2: the initial flags start the session, and a reset restores them".
- `rcg_levels_canonical` covers `data/elements`; the gate `lowered` keeps `bench/lowered` current.
