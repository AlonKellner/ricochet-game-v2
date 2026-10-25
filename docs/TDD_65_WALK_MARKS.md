# TDD 65: Path effects per walk (Stage 160)

Your rule (2026-10-25): "The preview should actually show the expected state of the planned walk and physical walk
separately, at least at the path level itself, the surfaces may conflict and that would clutter the preview
visualization, and so non path effects should be visible only if not after a divergence, and path effects should
always be visible, over the appropriate path sections." GAME_SPEC 14.11.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 160.1 | The marks: the physical walk's states over the common and its diverged steps, the planned walk's (from its own hits) over its diverged steps; no "began before the divergence" condition | Done |
| 160.2 | Tests, docs, `make gate` | Done |
| 160.3 | The user's interactive sign-off | Done (the user's sign-off 2026-10-25) |

## Decisions

- **Path effects belong to a walk; world effects to the one world.** The arrow's states are drawn on the walk that has
  them, so after the divergence the planned path shows what the planned walk's arrow carries and the physical path
  what the physical arrow carries. Surfaces, targets, the goal and journeys are one world; showing both walks'
  versions would conflict, so they stay limited to before the divergence (Stage 157, Stage 159).
- **The planned walk's states come from its own hits.** Its hits are placed on its steps by point, in walk order, as
  the physical walk's are. The common steps carry the physical walk's states (the walks agree there).

## Tests

`rcg_tests` "arrow states: the preview marks each walk's fire over its own steps ...": an unplanned mirror turns the
physical arrow through a torch -- its diverged steps carry fire, the planned walk's do not, and the floor it burns is
not a consequence (after the divergence).
