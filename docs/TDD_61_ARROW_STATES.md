# TDD 61: Fire, charge and frost as arrow states (Stage 156)

Your rules (2026-10-25), in short: the arrow, surfaces and objects are on fire or not; fire passes from a burning
arrow to a flammable thing and from a burning thing to an unlit arrow; water puts the arrow out. Arrows,
conductive surfaces and objects are charged or not, and a touch swaps the arrow's and the thing's charge. Arrows
are frosted or not; frosted things frost the arrow; a frosted arrow keeps its frost unless something would stop
it, which instead reflects (a line) or inverts (a circle) it and uses the frost up. The rules are in GAME_SPEC 10.6
("Fire, charge and frost"); the authoring guide in docs/ELEMENTS.md. The preview rules are Stage 157 (TDD_62).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 156.1 | The spec (GAME_SPEC 10.6), the oracle: conditional state changes (`when`, read under the side's flags before its changes), the frost rule in side resolution, `rigid` sides; fixtures `stage156_arrow_states.json` (9 walks) | Done |
| 156.2 | The core: `FlagCond`, `StateChangeDef::when`, `Flags::holds`, the frost cache; `Surf::frosted`, frosted effects and changes, `SideDef::rigid`; the fixtures at f64, Dual and 4 limbs | Done |
| 156.3 | The game: `when` on state hits and `rigid` on sides (parsed, built, described, in the scene defs and the oracle's level builder); the tags' writes on every side of an element (resolver rows included) and an object; `burning` and `charged` starting flags; frost-bearing checks; the shadow's edge rigid; touching rules opt-in | Done |
| 156.4 | Templates on the tags (`torch`, `burnable`, `frost_barrier`, `battery`); tests; docs; `make gate` | Done |
| 156.5 | The user's interactive sign-off | Done (the user's sign-off 2026-10-25) |

## Decisions

- **In the solver, not after the shot.** Fire, charge and frost change what the arrow does later in the same shot
  (a frosted arrow reflects; a lit flammable surface lights the next arrow), so the walker must play them. Fire and
  charge need conditions on two flags at once (the arrow's and the thing's), which no resolver (one key) can say:
  conditional state changes are the one new primitive. Frost changes every stopping side in the level, like ghost
  mode, so it is a rule of side resolution rather than a write on every wall.
- **A side's changes see one state.** Every condition of a side's changes is read under the flags the side fired
  under, before any of its changes apply. That makes the swap one rule (give and take cannot both fire).
- **Tags, not kinds.** Any element or object can be flammable, conductive or frosted: the lowering appends the tags'
  writes to every side (a missing side becomes a pass that carries them) and to every resolver row. The writes show
  in `bench/lowered`, so the oracle's independent builder checks them.
- **Touching rules are opt-in.** Stage 152's spread (fire and charge through touching things, no arrow) is not in
  your rules, so a level has none unless it names them. A recomputed rule (charge from `powered` sources) overrides
  what the arrow did to the things it covers.
- **A pass-through conductive circle swaps twice.** An arrow crossing it touches it entering and leaving. That is
  the rule as written; a conductor the arrow should leave charged is a line or stops the arrow.
- **The shadow's edge is rigid.** "Shadows stop arrows": frost does not turn the edge of a shadow.
- **A frosted reflection is plannable only on an interactive side (v1).** Superseded by Stage 158 (TDD_63): while
  the plan's frost is active every stopping side is plannable.

## Tests

`rc_tests` "arrow states: ..." (2): the fixtures match the oracle at three tiers; a side's conditional changes read
one state (the swap), the frost flag's cache. `rcg_tests` "arrow states: ..." (4): fire both ways and water; the
charge swap with a charger, a conductor, a battery and a conductive object; frost off a wall once, from a frosted
object, and a shadow's edge stopping a frosted arrow; the tags' errors. The kit and spread tests follow the new
flags and opt-in rules.
