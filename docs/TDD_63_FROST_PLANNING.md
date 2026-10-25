# TDD 63: Planning with frost (Stage 158)

Your rule (2026-10-25): "With frost, walls and any blocking surface (not shadows) must be plannable while the frost is
planned and active. Notice that hitting exact shared surface points should always block, regardless of frost." The
rule is in GAME_SPEC 10.6 ("Planning with frost").

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 158.1 | The oracle: a planned frost barrier and a planned wall (the planned walk reflects off it like the physical walk); a frosted arrow at a corner and at a T-junction stops (fixtures `stage158_frost_planning.json`, matched natively at three tiers) | Done |
| 158.2 | The tracer: whether frost turns a side after the plan's entries, and per entry; the game: a stopping side (terminal, not rigid) is clickable while the plan's frost is active; hover and plan chevrons as a reflection | Done |
| 158.3 | Tests, docs, `make gate` | Done |
| 158.4 | The user's interactive sign-off | Open |

## Decisions

- **"Planned and active" is the plan's simulated state (GAME_SPEC 13.7).** The frost counts when a planned entry gave
  it (or the level starts frosted) and no earlier planned stopping side used it up. Frost the arrow would pick up from
  an unplanned barrier does not make walls plannable: the planned walk never has it.
- **The solver already plans it.** The planner resolves a planned side under the simulated flags, so a planned
  frosted wall is a reflection to it. Only the game's click rule and chevrons needed the change, the way ghost mode
  restricts clicks by the same simulation.
- **Shared points win over frost.** Stage 138's stop at a point two surfaces with a consequence share is decided
  before any side's effect, so frost cannot turn it: the arrow stops, nothing fires, the frost stays. The fixtures pin
  it for a corner and a T-junction.
- **The shadow's edge stays unplannable.** It is rigid (Stage 156), so frost never turns it.

## Tests

`rc_tests` "arrow states: the Stage 158 fixtures ...". `rcg_tests` "arrow states: a stopping side is plannable while the
plan's frost is active ...": a wall is not plannable without frost; after the planned barrier it is, and the preview
stays aligned through its reflection; after the frost is used up the next wall is not; a shadow's edge never is; a
frosted arrow at a corner stops there with nothing fired.
