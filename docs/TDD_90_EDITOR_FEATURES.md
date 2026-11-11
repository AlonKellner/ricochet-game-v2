# TDD 90: The editor shows every surface the same way -- what it does, its features added and removed, enums as chips (Stage 212)

Your request (2026-11-10): a button that blocks, and every surface optionally blocking, configured the same way; the
plan you approved for it (Stage 207's): "the editor shows every surface the same way" -- the rows grouped as geometry,
the arrow, the body and each feature, a `+ feature` chip row, enums as chips. Then (2026-11-11): "Proceed to the next
stage".

The rules are docs/EDITOR.md (the panel, the commands). The game does not change: no draw list, lowering or golden.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 212.1 | `surface_element::on`: whether an element has a feature (its key, or its kind's default; a button by `opens` too) | Done |
| 212.2 | The commands `add_feature` (a starting value from the feature's kind) and `remove_feature` (its parameters, but one another feature reads) | Done |
| 212.3 | The panel: after the geometry the arrow, the body, each feature under its name with `remove` (not the kind's own), then `+ feature` chips | Done |
| 212.4 | Enum chips: an enum's values (an item's options, up to six) as an actions row under it, the current lit, the kind's default unset | Done |
| 212.5 | The user's interactive sign-off | Pending |

## Decisions

**Grouped by what they say.** A surface element's parameters came last, after the motion, tags and marker rows, in
one list. They now follow the geometry -- the rows a designer reaches for most -- in the element's terms: what the
arrow does (`face`, `back`, `plannable`), what the player and objects do (`body`), then each feature it has under its
name. A template kind (portal, switchable, the kit) keeps its parameters in its own order, in the same place.

**Starting values from the data.** `+ <feature>` writes the feature's key with the value of the builtin kind that is
that feature (the preset whose `features` is just it): its `example`, else its parameter's default, else `true` --
button `flag` "switch", door `open` "door:<name>" (`${name}` filled in), spawn the spawner's example surface, reset
its channels, target, grapple, break and ghost `true`. A level's own template of that feature would be found too.

**Removing.** `remove_feature` erases the feature's parameters but one another feature the element has reads
(`deferred`: the button's, the spawn's and the reset's). The kind's own feature cannot be removed (its kind requires
it, Stage 207's "'flag' required"): picking another kind drops it.

**Chips beside the cycle.** The value row stays (its label, hint, typed text, click to cycle, shift+click back); the
chips under it set a value in one click and show the choices. An element's chip of the kind's default unsets the key,
the document saying only what differs (what the migration writes too); `back` has `= face` (unset). An item's
options set the value (as its cycle does). Five or six values take two rows of three (a spawn's orientations, an
object's shapes); more keep only the cycle.

**`(none)` and `(auto)`.** A parameter whose default is null showed `()`; it shows `(none)`, `plannable` `(auto)`.

## Tests

- `rcg_tests`, `test_editor.cpp` "an enum's values are chips": a wall's face, back (`= face`) and body chips, the
  current lit, a click sets, the default unsets, undo; an object's `kind` and `face` chips.
- "a surface element's features": the eight `+` chips on a wall; `+ button` (flag "switch", its rows under
  `button`, `remove button`), `+ door` (door:w), `+ spawn`; `deferred` kept while the button has it, removed with the
  last; undo; refused: one it has, one it lacks, the kind's own (a button's button, marked `(the kind's)`), a kind not
  of surface (a portal), an unknown feature.
