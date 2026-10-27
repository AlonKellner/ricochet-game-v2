# TDD 76: Platforms (Stage 172)

Your request (2026-10-27): "I want to add a new type of surface. It should allow arrows to go through it, and it
should generally stop objects and the player from falling, but if the player presses down while standing on it, they
should fall through the surface, and if an object is pulled by the player or the player is pulled, this surface
should not stop the player or object from going through it." Your choices: one-way (passed from below and the side),
down held to drop, wood brown. And: "if a pulled object or grappled player end up with their center point above the
platform but with some overlap, they should be moved to be on top of the platform ... if their midpoint is above it
they should move on top of it, if it's below they should fall through." The spec is GAME_SPEC 25.1 (with 4.1, 9.1,
22.2, 25.5, 25.6); the level format docs/LEVEL_FORMAT.md, the elements docs/ELEMENTS.md.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 172.1 | The data: the surface key `platform` (`SurfaceSpec`, `BuiltSurface`, spawned surfaces; never with `player_solid`), written into dumps and digests only when set; the oracle's builder (`level_json.py`) the same; the builtin element `platform` (priority 14, after `passthrough`) | Done |
| 172.2 | The kinematics: `Collider::platform`; in `step_body` a platform is off for a step unless the body's centre is above it at the step's start and down is not held; then floor-like contacts only (the sweep, the push-out, the snap), and no push-out while rising; `kinematics::settle`; `rcg_tests` "platforms: ..." | Done |
| 172.3 | The session: `colliders_except(..., journey)` leaves platforms out of journeys (live and the preview's trip); the traveller settled at the journey's end, live and in the preview alike; the draw (wood brown in the game and the editor); the demo level `objectives/platforms` | Done |
| 172.4 | Docs; `make gate`; the session `objectives__platforms__play` (its golden after your approval) | In progress |
| 172.5 | Your interactive sign-off | Open |

## Decisions

- **A surface key, not a new path effect.** The arrow, visibility and planning see a platform's sides only: the
  builtin's are passes, so the solver never computes it (`never_candidate`) and neither the solver nor the oracle's
  walker changes. The physics reads the key. A template may put `platform` on any surface.
- **Not also `player_solid`.** A platform is its own collision class; both together are a level error.
- **The centre decides.** A platform takes part in a step only while the body's centre is above it at the step's
  start: a line on the side its up-facing normal points to, along its whole carrier (so a fast diagonal fall past
  an end cannot tunnel); an arc where the normal from its nearest point is within 45 degrees of up. A line steeper
  than 45 degrees has no upper side and holds nothing. Then only floor-like contacts count, so it is never met
  from below or the side. A body half through it with its centre above is pushed up onto it; with its centre
  below, it passes on.
- **A rising body is not pushed up.** A jump through a platform would otherwise be lifted as its centre crossed
  the line (a pop of up to the radius) and stopped there. It carries on, keeps its whole height, and lands on the
  platform. (The plan had the push without the floor; no push at all is smoother and lands the same.)
- **Down held, not a tap.** Every host already sends down; with gravity it did nothing. While it is held platforms
  are off for the player, so it falls through, and in the air falls past them. Released before the centre is
  past the line (about 10 ticks from standing at gravity 980), the player is put back on top: your rule, applied
  the same everywhere. Objects hold no down. Without gravity the rule lets the player pass both ways.
- **Journeys pass platforms; their end is settled.** The live journey and the preview's trip leave platforms out of
  what the traveller meets, then settle it where it ends (`kinematics::settle`): above, on top and on the floor;
  below, left to fall. The trip's trail ends at the settled point, so the preview shows where the shot leaves it.
- **No existing output changes.** `platform` is written only when true (as `pre_transform`), so every dump,
  lowering, digest, session record and golden is byte-identical.

## Tests

`rcg_tests` "platforms: a body falling onto one rests on its top", "... a jump from below passes up through one,
keeps its whole height and lands on top", "... down held drops through; released before the centre passes, back on
top; after, on down", "... half through one, the centre decides", "... from below and the side never blocks; without
gravity it passes both ways", "... a circle holds on its top and passes from below; a slope over 45 degrees never
holds", "... a platform surface is a platform collider", "... an object falls onto one and rests; a pull brings it
down through it", "... a grapple takes the player up through one", "... a pull ending half through one", "... the
demo level plays to its goal". The random-walk invariant counts no platform in its clearance. The Python host:
`test_platforms_are_drawn_wood_brown`. The oracle: `test_oracle_v2` (the new level's dump).
