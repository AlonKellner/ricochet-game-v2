# TDD 79: Climbing platforms (Stage 176)

Your request (2026-10-27): "When I press up and the player touches a platform, I want the player to have a minimum
upwards speed, as if climbing the platform. Same should be true for vertical platforms as if they are ladders or
ropes." The spec is GAME_SPEC 25.1 (with 4.1 and 25.2); the platform itself is Stage 172 (docs/TDD_76_PLATFORMS.md).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 176.1 | The kinematics: in `step_body`, with gravity and up held, a platform that does not hold the body this step, touched along its length by one of its circles or its hands (`REACH` above the head), sets the upward speed to at least `CLIMB_SPEED` (150); `rcg_tests` "climbing: ..." | Done |
| 176.2 | Docs (GAME_SPEC 2.80.0, CHANGELOG); `make gate`; the session golden unchanged | Done |
| 176.3 | Your interactive sign-off (`platforms_1`: the stacks and the ropes) | Done (the user's sign-off 2026-10-28) |

## Decisions

- **Only a platform that does not hold the body.** It is one whose upper side the feet are not on (the player is
  below it, or passing through), or one steeper than 45 degrees, which never holds anything: a ladder, a rope. A
  platform the player stands on is no climb, so standing on one with up held stays put and does not hover.
- **Along its length, not at an end.** A circle touches when it is within the radius plus the skin of the platform,
  nearest a point between its ends. A rope's top end alone is no hold. So the climb stops once the feet pass the
  rope's top, and the player rises the last few units (150²/2g, about 11.5) onto what is there. A platform at a
  rope's top is landed on, and up still held stays there. A bare rope's top is a small hop while up is held.
- **The hands reach one radius above the head.** The player is 48 tall. Standing under a stack 50 apart (as in
  `platforms_1`), the next platform is 2 units over its head and the body never touches it. The hands reach it,
  so up held climbs a stack platform after platform without a fresh jump. A platform further up is out of reach.
- **A minimum, not a set speed.** A jump's 400 is kept until it slows to 150, so a pressed and held jump that
  reaches a platform from below goes on up through it and lands on its top. Horizontal movement is unchanged
  while climbing.
- **Gravity only; the player only.** Without gravity, up already moves the player up. Objects hold no input, so
  they never climb.
- **The arrow and the preview are unchanged.** Climbing is walking, which no preview shows. The solver, the
  oracle and every dump are untouched.

## Tests

`rcg_tests` "climbing: up held lifts a jump that touches a platform from below through it, onto its top", and
"climbing: the player climbs a stack of platforms, a rope to the platform at its top; down or nothing passes" (a
stack 50 apart, a platform out of reach, a rope with a platform at its top, a bare rope, the rope not held or down
held, no gravity).
