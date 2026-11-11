# TDD 92: A face by flag and hits by flag -- the switch and armed features (Stage 214)

Your request (2026-11-11): plan and do the queue -- the second item, a "face by flag" feature folding switchable and
conditional_button into the surface element; your choice: retire toggle_surface and freezable. The plan you approved
(2026-11-11): Stages 213-220.

The rules are GAME_SPEC 9.13 (the switch and armed rows, the Stage 214 note).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 214.1 | `switch` (`when`, `then_face`, `then_back`) and `armed` (`armed`): composed as one resolver on the flag, switchable's row order | Done |
| 214.2 | `decompose` reads that resolver back (two rows, `true` and `DEFAULT`; hits on both, or on `true` only: armed) | Done |
| 214.3 | switchable and conditional_button presets; flag_switches rewritten; toggle_surface and freezable retired | Done |
| 214.4 | The editor's `+ switch` and `+ armed` chips (their starting values from the presets) | Done |
| 214.5 | Goldens: none move (lowerings byte-identical, the session golden unchanged) | Done |
| 214.6 | The user's interactive sign-off | Pending |

## Decisions

**One resolver, the switchable's.** A switchable lowered to `left`/`right` null and a resolver on its flag, the `true`
row first. `compose` writes the same for `when` -- so test_levels/flag_switches.json, rewritten to the new spelling
(`flag` -> `when`, `on` -> `then_face`, `off` -> `face`), lowers byte for byte. The hits ride on both rows; `armed`
keeps them off the `DEFAULT` row (the conditional button's). The conditional button's template listed `DEFAULT` first;
with one row of each, the order says nothing (model.cpp: a matching row wins, else `DEFAULT`), and no level used it.

**One flag per surface.** A resolver reads one key, so `when` and `armed` both set must be the same expression (a wall
that turns mirror, and presses, only while the arrow is charged); different ones are an error naming the element.

**`then_face` defaults to pass**, switchable's `on`; `then_back` unset is `then_face` (not `back`), as the `on` sides
were both sides.

**The label by the face.** A preset's `label` may `$switch` on a parameter as a template's does (it already could):
switchable is `FlagMirror` when it turns mirror, else `Gate`.

**Retired.** toggle_surface (two surfaces on one geometry, each writing the other -- the surface element is one
surface) and freezable (every wall has reflected a frosted arrow once since Stage 156) -- no level used either. Their
templates are kept in native/tests/game/retired_templates; the toggle's kit test went with it, the freezable's now
fires at a plain wall.

**Starting values.** `+ switch` takes switchable's example (`when` "switch"); `+ armed` the conditional button's
default (`shot:charge`) -- a feature no preset is about alone is found among the presets about it with others.

## Tests

- `rcg_tests`, `test_surface_element.cpp`: the equivalence grid now 17 kinds -- switchable and conditional_button over
  every value of their old parameters, against their templates as Stage 213 left them (rows compared by value); the
  read-back grid with switch and armed (1024 feature sets sampled); "a face by flag and hits by flag": a wall that turns
  mirror and presses on both rows, a conditional button's hits on `true` only, `when` and `armed` differing refused,
  migrate finding both presets.
- `test_level2.cpp`: 31 builtin kinds build; `test_kit.cpp`: frost at a plain wall; the conditional button presses only
  charged (unchanged).
- `make lowered dumps`: no diff; the session golden unchanged.
