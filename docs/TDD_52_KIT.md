# TDD 52: The novel-element kit (Stage 147)

The editor program's sixth stage. You asked for a flexible way to design new elements and test them quickly,
without changing the code of the core elements or the core game logic. Stage 142 made every element kind a data
template. This stage adds the one primitive most novel elements still missed, **per-shot flags**, and ships nine
templates that use it: breakable, toggle, frost, fire and charge elements. docs/ELEMENTS.md ("The kit") is the
designer's guide to the patterns.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 147.1 | Per-shot flags: `shot:*` erased from the promoted flags when the arrow lands (GAME_SPEC 10.6) | Done |
| 147.2 | The templates: `breakable`, `toggle_surface`, `frost_barrier`, `freezable`, `torch`, `water`, `burnable`, `charger`, `conditional_button` | Done |
| 147.3 | Their behaviour in play (`rcg_tests` "kit: ..."), the docs, `make gate` | Done |
| 147.4 | The user's interactive sign-off (each element in a level of the user's) | Open |

## Decisions

- **One primitive, not one per element.** Fire, frost and charge are the same mechanism: a property the arrow
  picks up, loses, and is tested for. So they are templates over one rule: a `shot:` flag lasts one shot.
  `ghost` was already that rule, for one flag.
- **A breakable surface's hit still applies its face.** A breakable mirror reflects the arrow, then is gone for
  the rest of the shot: a later pass in the same shot (after another reflection) goes through. It is back at
  landing because its flag is a per-shot one.
- **The toggle surface is two surfaces on one geometry,** each present in one state and each writing the other.
  When the surface is hit, its twin pass-through appears at the very point the arrow leaves. The walk does not
  fire it there: the test checks that one hit removes the toggle and one pass brings it back.
- **The reactions are resolvers.** A freezable wall is a wall in its default row and a mirror that uses the frost
  up in its `true` row. Categorical sides (§9.4) are limited to pass, terminal, reflection and inversion, so a
  frozen portal or projection is not expressible without a primitive.

## What the kit cannot express: the next primitives (sketches)

| Element | Why it is not a template | The primitive it needs |
|---|---|---|
| Moving surfaces (a schedule or cycle; the player scrolls it; auto-resume after 10 s idle) | a surface's geometry changes with time, not with flags | a body with a `motion` (keyframes of position and rotation, a period). The world moves it like an object (Stage 146) while nothing is fired, and the scroll input sets its phase. The plan stays valid because a plan names surfaces, not positions |
| Light and shadow (arrows blocked where only dark regions cover) | membership in regions is not a flag, and the arrow's path decides it point by point | a region test in the walker: a crossing into a shadowed area is a terminal event. This touches the solver, so the oracle comes first (the costly kind of change) |
| Spreading fire or charge between surfaces (not carried by the arrow) | a surface's state depends on its neighbours' | a propagation step at landing: a contact graph over present surfaces, a flag flooding along it |
| Bubbles (floating circles that make objects float; pop on impact) | buoyancy is a force on bodies | an object kind with negative gravity plus `breakable`: the bodies' step (Stage 146) with a per-object gravity |
| Spawners placing at the hit (exists), frozen objects reflecting back | -- | already templates (`spawner`; an object with face mirror) |

## Tests

`rcg_tests` "kit: ..." (6): the per-shot flags are gone after landing; a breakable mirror reflects once, the arrow
passes its place later in the shot, and it is back after the shot; frost turns a wall into a mirror for one hit,
and an unfrosted wall stops the arrow; fire burns a barrier for good and water before it saves it; a toggle
surface goes when hit and comes back when passed; a conditional button presses only for a charged arrow. "rc-level/2:
every builtin template expands to surfaces that build" covers the nine new kinds, and docs/ELEMENTS.md lists them.
