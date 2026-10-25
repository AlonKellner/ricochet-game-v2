# TDD 70: Journeys in three layers (Stage 166)

Your design (2026-10-25): a pull should bring the object to the arrow's hit point first, then along the arrow's
path; "we have 3 elements: the raw path logic, calculated by the pure solver; the moving point logic, which is
created over the raw path and might include animating considerations; the animating circle logic, which mainly
follows the moving point, but might create copies or follow specific logics at specific events" -- the point waits
for the circle; the circle stops after reaching the point at the final step, or when it cannot get closer to it;
a slide keeps the pull's speed; at a portal a copy of the object and the point at the other end, clipped, until the
object is through. The same for the grapple. GAME_SPEC 25.6.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 166.1 | The guide over the raw path: starts at the path's start (a pull: the hit; a grapple: the player), moves at the journey speed while the traveller holds it (inside its radius), waits otherwise | Done |
| 166.2 | The traveller: straight toward the guide at 1.25 times the guide's speed, swept, sliding along a contact at its full step up to its nearest approach to the guide; stops at the guide at the path's end or when no closer to a waiting guide | Done |
| 166.3 | Portals: the guide waits at the entry; the traveller crosses (no collision while in it) toward the far side until wholly through, its copy mapped by the portal's motion; drawn clipped at both portal lines (`["journey"]`), the body's own surface and the player's node hidden meanwhile | Done |
| 166.4 | The preview: the same tick (trail legs per side of a portal, the end ring orange only when stopped short of arriving); tests; docs; the session golden re-recorded | Done |
| 166.5 | The user's interactive sign-off | Open |

## Decisions

- **"Close enough" is the guide inside the traveller.** At the start of a pull the hit point is on the object's rim,
  so the guide moves at once and the object follows it off the floor's edge rather than into it.
- **Sliding converges.** A slide keeps the full remaining step (your rule), but stops at the point of the slide
  nearest the guide: past it, round a curved contact (the player), the object would come back and oscillate.
- **A portal crossing is kinematic.** While the traveller is in the portal it moves straight through, ignoring
  collisions (it is being carried through); its copy is the portal's motion of it, so the two stay in step. When the
  traveller is wholly past the entry line the copy becomes the traveller.
- **Evidence.** Four session records changed: grapple_gap (the player stops on the hook at y 450.0 instead of 454.5,
  then lands the same), pull_box, bubble_drop and circuit (the journeys' animation and the world's hash during them);
  every end state, flag and win is unchanged.

## Tests

`rcg_tests` "pull: the box follows a guide ...": a box resting on the floor hit on its top half and its bottom half comes
to the player, the preview's end equal to the shot's; "pull and grapple through a portal ...": the box comes back
through the portal to the player, the grappling player goes through to the grapple, the previews agree. The earlier
pull, grapple, fence, bubble and demo tests pass unchanged.
