# TDD 103: Shields stop the journey (Stage 225)

Your request (2026-11-15), with two F12 dumps of shield_5: "These captures show how a player might use shots to make an
object go through a vertical shield. This should not be possible, shots which pull objects make shields physical to the
object pulled, same for grapples. Shots are blocked by shields, but pulling and grappling are strange interactions
between shots and the player/objects, and in those situations I want to be perfectly clear - shots are blocked by
shields, and that include pulls and grapples. Pulls and grapples should be simulated with shields as physical objects
as long as they are pulled or grappled. After the shot animation is through, the object or player may move, fall or
pass through shields by any means freely."

The rules are GAME_SPEC 25.6 (the pull and the grapple); the code `Session::colliders_except` (`session.cpp`).

## What happened

In the first dump the player stood 5.5 px left of shield_1 (x 950) and pulled the box (radius 16) from the left. A
pull ends at the arrow's origin, the player's head, so the box ended with its centre at x 944.5, reaching 10.5 px past
the shield: the journey's colliders were the player-solid surfaces only, and a shield is not one. Free again, the box
straddled the shield; in the second dump, a pull from the right brought it on, through, onto the detector that opens
the door to the goal. The arrow never crossed the shield -- the traveller's body did.

## The fix

| Phase | Topic | Status |
|---|---|---|
| 225.1 | A journey's colliders (`colliders_except(..., journey = true)`, every era of flags) include every present surface with a side that `shields()` under those flags; the free bodies, the player's walk and the journey's end settle unchanged | Done |
| 225.2 | Tests: the user's shield_5 as dumped (the box stops against the shield, the preview agrees, it then rests on the left, `open` stays unset); a grapple whose arrow passes over a low shield stops at the tall player's feet, and the player then walks through the shield | Done |
| 225.3 | The user's interactive sign-off | Pending |

## Decisions

- **Shields only.** "Shots are blocked by shields": a surface is solid to the journey when a side is a shield as the
  game draws one (`shields()`: not player-solid, not a platform, a plain terminal side, not interactive, no hits). Other
  non-solid surfaces that stop the arrow (a target with body `pass`, a button with face `stop`) are unchanged.
- **Both ways.** A surface with one shield side is solid from either side during a journey (the colliders have no
  one-way kind but the platform's). Every builtin shield shields both sides.
- **In the flags of the moment.** A switch that turns a shield into a pass (or back) is read per era of the journey
  (Stage 223: a detector that changes the flags changes what blocks the traveller from the next tick).
- **Only during the journey.** The free bodies' fall and the player's movement never meet a shield; the end of a
  journey settles onto platforms only, as before (it cannot end across a shield: the run stops at it).
- **Evidence.** No session record, region digest or golden changes (the commit gate).

## Tests

`rcg_tests` "pull: the user's shield_5 -- a box pulled toward a player beside a shield stops at the shield ...": the
dump's player and aim; the preview's trip and the shot end at x 950 - 16 - 0.08; after 90 ticks the box rests there on
the floor; `open` unset (before the fix: x 944.5). "grapple: a shield the arrow passes over stops the tall player's
feet ...": a grapple at x 1200, a shield from the floor up to y 815, the head at 803.92 -- without it the player reaches
the grapple, with it the player stops at x 900 - 12 - 0.08, and then walks right through the shield.
