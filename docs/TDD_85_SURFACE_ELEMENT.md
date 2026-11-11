# TDD 85: The surface element -- face, body and features, the kinds presets of one element (Stage 207)

Your request: "Can I somehow make a button block the player and objects in the editor? Ideally I'd want all surfaces
to be optionally player or object blocking ... make all surfaces be editable with a `face` field, making 'fence' or
'shield' pure properties which change visuals as well. Then, things like making a surface into a button could just be
boolean flags or optional features, like motion ... How can we make the editor streamlined in a way which will allow
orthogonal features and properties be configured independently and consistently?"

Your decisions (2026-11-10): two orthogonal fields -- `face` (`stop` | `pass` | `mirror`, with `back` for the right
side) and `body` (`pass` | `block` | `platform`); the kind names presets of a face, a body and features; a native
composer, the presets data; the look from the pair, a steel band under a surface that blocks but does not stop (walls
unchanged); "If deprecating levels or updating them will make things easier, you may do so". The band approved after its
renders and the session report ("Works perfectly, approved").

The rules are GAME_SPEC 9.13 (the element, its features, their composition, the presets), 22.2 (the steel band), 25.5
(an object's face and body).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 207.1 | Spec: GAME_SPEC 9.13, 22.2, 25.5; no solver or oracle change (the element lowers to rc-level/1 surfaces) | Done |
| 207.2 | The composer: `rcg/surface_element.hpp` (compose, decompose, the parameters and features) | Done |
| 207.3 | The presets: 15 kinds as `{"extends": "surface", "defaults", "features"}`; the other templates' `body`; migrate's choice | Done |
| 207.4 | The levels rewritten to the new spelling; the lowerings and dumps byte-identical | Done |
| 207.5 | The editor's rows for a surface element (its sides, body, plannable, the features it has) | Done |
| 207.6 | The look: the steel band in the game and the editor (approved 2026-11-10) | Done |
| 207.7 | Goldens: the session golden's draw shas (17 sessions: main, objectives/circuit, the inversion playgrounds) | Done |
| 207.8 | The user's interactive sign-off | Pending |

## Decisions

**One element, presets as data.** The surface kinds (wall, shield, fence, passthrough, platform, mirror, one-sided
mirror, button, target, door, grapple, breakable, spawner, resetter, ghost gate) were 15 templates that each fixed some
of a surface's properties and exposed others under their own names (`face` spelled `wall|mirror|pass` or
`pass|terminal|reflection`, a door's `closed`, `solid` on a dozen, none on the wall). They are now presets of one native
element: `data/elements/<kind>.json` is `{"extends": "surface", "label", "defaults", "features", ...}`. `prepare()`
(elements.cpp) turns a preset into a template with the element's parameters and one composed surface, so `params_of`,
the checks, the editor's rows and the label rules are the templates' own. Templates stay for what the element does not
say (inversions, projections, portals, switchable and conditional sides, the kit) and take `body` for `solid`.

**The composer emits what the templates emitted.** `compose()` writes `player_solid` always, except for a platform
(`platform: true` and no `player_solid`, as before); the hits in the fixed order button, target, grapple, spawn, reset,
break, ghost; a side plannable when it reflects or has a hit (target's old `interactive` is `plannable`). The retired
templates are test data (`native/tests/game/retired_templates`): every combination of their parameters' values, flipped
or not, expands identically through the presets.

**A preset's own features are required.** `features` names what the kind is about: a button without `flag` or `opens`,
a spawner without `spawn`, a resetter without `channels` is an error, as before; the editor shows their rows.

**Migrate: the label first.** Every preset says every surface element, so the wall (priority 10) would say them all.
`migrate_level` tries the kinds of the run's label first, by priority; failing that (a label override: a button
labelled "Toggle") the kind writing the fewest parameters beyond its own features, priority breaking ties. The 37
levels Stage 142 migrated still migrate to their files' exact text.

**No aliases.** The user allowed rewriting the levels: 37 files were rewritten once (a script, not committed) and the
old spellings are errors -- `solid`, the `wall`/`terminal`/`reflection` faces, a door's `closed` and `flag` (now `open`;
a door's `flag` is a button's), a spawner's `surface` (`spawn`), a target's `interactive` (`plannable`), an inversion's
`sides` (`back`: `invert` | `pass`), a projection's `back` (`reverse`), a one-sided mirror's back `block` (`stop`), an
object's `face` `wall` and `solid`. An editor of an older build writes the old spelling back: reload it.

**The steel band.** `steel_band()` (build.hpp, beside `shields()`): player-solid, not a platform, and some side does not
stop (a null side passes). Drawn as the shield's band is, in the colliders' steel (0.6, 0.72, 0.85) at 30%. Inversions
block by default, so every inversion has it; walls, shields and platforms are unchanged.

Out of scope, noted (Stage 208 onward): the editor's chips for enums and `+ feature` / `remove` rows; folding switchable,
conditional button and the kit into the element ("face by flag", tag features); inversion, projection and portal as
faces; the door's look from its `open` feature rather than the label "Door".

## Tests

- `rcg_tests`, `test_surface_element.cpp`: the retired kinds expand as their presets over every value of their
  parameters (with defaults, flipped); a surface's parameters read back (decompose then compose) over the sides, bodies,
  plannable values and a sample of the 256 feature sets, and foreign surfaces refused; the features combine (a blocking
  button, a door that is a button, a breakable target with a door's presence, a platform mirror); a preset's own feature
  required; migrate picks the label's preset and writes only what differs.
- `test_bodies.cpp`: the steel band on a fence, a solid mirror, a blocking button and a blocking one-sided mirror, not on a
  wall, a shield, a platform or a passthrough.
- `test_editor.cpp`: a surface element's rows (its sides, body, plannable and its features; not the features it lacks);
  `body` cycles to `block`; an unset `back` cycles from the face.
- `test_level2.cpp`, `test_objective.cpp`, `test_spread.cpp`, `test_bodies.cpp`, `test_arrow_states_game.cpp`: the new
  spelling; every level's lowering and dump unchanged, the MIGRATED levels' exact text.
