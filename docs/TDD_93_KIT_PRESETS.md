# TDD 93: The kit as presets -- tags on the surface element (Stage 215)

Your request (2026-11-11): plan and do the queue -- the third item, the fire, frost and charge kinds as presets of the
surface element with tags. The plan you approved (2026-11-11): Stages 213-220.

The rules are GAME_SPEC 9.13 (the Stage 215 note) and 10.6.3 (tags).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 215.1 | The eight kit kinds as presets with default tags (their templates kept in retired_templates) | Done |
| 215.2 | `elements::apply_tags`: what tags give surfaces, shared by the lowering and `match` | Done |
| 215.3 | `match` reads a tagged preset back: its tags' hits stripped (a resolver's missing sides restored), its name from them | Done |
| 215.4 | The editor's tag chips (several lit; the kind's own fixed); `+ feature` starting values from the first kind in the palette | Done |
| 215.5 | Goldens: none move (lowerings byte-identical, the session golden unchanged) | Done |
| 215.6 | The user's interactive sign-off | Pending |

## Decisions

**What each is.** torch and frost_barrier: a pass the plan can meet (`plannable` true), tagged `burning` / `frosted`;
conductor: a pass (or stop) tagged `conductive`; battery: a wall tagged `conductive`, `charged`, `powered`;
charged_door: a door on its own `el:<name>:charge`, tagged `conductive`; water and charger: buttons setting `shot:fire`
false and `shot:charge` true (their `fire` and `charge` parameters are the button's `flag` now -- no level used
either); burnable: a switch on `shot:fire` (then a pass) and a door on its own `el:<name>:fire`, tagged `flammable`.
The circuit and lantern levels lower byte for byte.

**Migration reads them back.** `expand` never applied tags (the lowering did), so `match` could not recover a tagged
kind: the circuit's battery, wires and door came back `raw`. Now the lowering's tagging is one function
(`elements::apply_tags`), `match` strips a preset's tags' hits from the run (the last hits of every side and row; a
side the tags gave a resolver's surface, a pass with only their hits, is missing again), reads the element's name from
them (`el:<name>:fire`, `el:<name>:charge` -- the circuit's `cell`, `wire1`, `gate`), and compares the expansion with
the tags applied. An element's own extra tags are not read back (it stays `raw`).

**Tags in the editor.** The `tags` field kept its typed JSON; under it six chips give or take a tag. A chip row lights
one option until now; it may light several (`Row::on`). The kind's own tags are lit and do nothing when clicked.

**Starting values.** A charged door is about the door alone too, so `+ door` would have written its `el:<name>:charge`;
the starting value now comes from the first such kind in the palette (the door's `door:<name>`; the button's "switch").

## Tests

- `rcg_tests`, `test_surface_element.cpp`: the equivalence grid now 25 kinds (the kit against its templates, its tags
  alike); "the kit as presets": every kit kind lowered and migrated back to itself, named, no level flags.
- `test_editor.cpp`: tag chips toggle a wall's tags, several lit; a torch's `burning` lit and fixed (no undo step).
- `make lowered dumps`: no diff; the session golden unchanged.
