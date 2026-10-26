# TDD 75: Flag expressions (Stage 171)

Your request (2026-10-26): "Let's add the option for parameters which perform boolean logic operations over flags,
like: x && y; (!x) || (!y) || (!z); x ^ y ^ z; (x && y) || (w && z); etc." (after: "I want to be able to create
levels where detectors close doors"). The spec is GAME_SPEC 9.11; the level format docs/LEVEL_FORMAT.md, the
elements docs/ELEMENTS.md.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 171.1 | The spec (GAME_SPEC 9.11) and the oracle: `trace2.py`'s `compile_expression`, `cond_holds` (presence, `when`) and the resolver's row; fixtures `bench/fixtures/stage171_flag_logic.json` (`gen_fixtures_stage171.py`: presence appearing mid-shot and held back, the door form, a resolver switching and a `false` row, `when` skipped and applied, planned and physical) | Done |
| 171.2 | The core: `rc::FlagExpr` (compiled once to postfix, evaluated on a bit stack, no allocation), `FlagCond` compiling its key (`PresenceCond` the same type), the resolver's compiled key; `rc_tests` "flag logic: ..." (the fixtures at f64, Dual and 4 limbs; precedence, truths, errors) | Done |
| 171.3 | The game: presence, resolvers, the goal and shades read expressions (`rc::key_holds`, `rc::key_true`); the reader checks every read key parses and refuses an expression where a flag is written; the demo level `objectives/logic_doors` and its session | Done |
| 171.4 | Docs; `make gate` | Done |
| 171.5 | The user's interactive sign-off | Pending |

## Decisions

- **Anywhere a flag is read, nowhere it is written.** The key of a presence condition, a resolver, a `when`
  condition, the goal's `requires` and a shade's `when` may be an expression; so may every template parameter that
  becomes one of those (a door's or a switchable's `flag`, a conditional button's `when`). A state change's key, a
  detector's and a spread rule's flag and the level's `flags` stay names (a level error otherwise).
- **C's precedence**: `!`, then `^`, then `&&`, then `||` -- `x && y ^ z` is `x && (y ^ z)`. Parentheses group.
- **A flag's truth**: true, a non-zero number, a non-empty string (GDScript's `bool`); a missing flag is false.
- **An expression is true or false; false reads as a missing flag.** True matches `"true"`, false matches `"false"`
  and `"DEFAULT"`. So every template keeps working unchanged: a door (present while `"DEFAULT"` or `"false"`) is
  closed while its expression is false, a switchable takes its `on` sides while it is true and its `off` sides (the
  `DEFAULT` row) otherwise.
- **An expression is spotted by its characters**: an operator (`& | ^ !`), a parenthesis or a space. No existing
  flag name has one (all 29 checked), so no level reads differently.
- **The solver evaluates them itself**, under the walk's current flags, so a hit that writes one of an expression's
  flags switches it mid-shot and the preview reads the plan's simulated flags (principle: every state change
  previewed). The oracle does the same; the fixtures hold them together.

## Tests

`rc_tests` "flag logic: the Stage 171 fixtures match the oracle ...", "... expressions -- the operators, C's
precedence ...", "... a condition on an expression ...". `rcg_tests` "flag logic: a door closed while the player
stands on a plate ..." (a `while` plate and `"!det:plate"`; a goal on an expression), "flag logic: the schema ..."
(errors with their paths), and "objective: the demo levels play to their goals" (`objectives/logic_doors`: the vault,
the trap, the odd door, the win). The session golden `bench/sessions/objectives__logic_doors__play.jsonl`. Every
existing record, dump, lowering, manifest and golden unchanged.
