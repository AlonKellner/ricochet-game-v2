# TDD 102: Slippery wall ends (Stage 224)

Your request (2026-11-14): "Currently it's possible for the player to stand on vertical wall edges. I want vertical wall
edges to be slippery, such that there is no stable position for a player to stand on, even if it's pixel perfect --
there should be a default tie breaking behavior which makes the player slip from the wall in a timely manner, such
that no position on the edge of the wall allows the player to stay for more than a second or so", then "The same
should be true for objects on wall edges".

The rules are GAME_SPEC 25.1 (slippery wall ends); the code `rcg/kinematics.hpp` (`step_body`).

## What happened

A floor is a contact whose normal is within 45 degrees of up, and a segment's end was no exception: the feet resting
on a wall's top end stood there with their centre anywhere within about 8.5 px of it. Exactly over it, gravity
pressed straight into the point, and the slide along the contact was zero; a little off it, the slide was slow and
the air's deceleration took it back. Objects the same.

## The fix

| Phase | Topic | Status |
|---|---|---|
| 224.1 | The end of a line steeper than 45 degrees is never a floor: in the sweep (a contact marked `slip`; of two at once, one that may be a floor first), the push out of an overlap, and the floor snap | Done |
| 224.2 | A body resting on such an end (within the skin and SLIP_REACH, the end within 45 degrees under one of its circles) is pushed off at SLIP_SPEED (90 px/s) a tick, to its centre's side; exactly over, the way it moves, still the right | Done |
| 224.3 | Tests: the player and an object dropped on a wall's end at 81 offsets; a floor's end, a floor's corner on a wall, a wall's end under a floor still hold; sliding down a wall's side past its lower end no push | Done |
| 224.4 | The user's interactive sign-off | Pending |

## Decisions

- **Steep, not only vertical.** A line steeper than 45 degrees is no floor anywhere along it; its ends are now no
  floor either. A gentler line's end (a ledge) is a floor's end and holds as before.
- **The push, not the floor alone.** Without a floor the body still balanced exactly over the end (no slide), and a
  little off it the player's air deceleration undid the slide. A push of 90 px/s, applied to the tick's move (the
  objects keep no horizontal velocity of their own), takes the 12 px off the end in about 8 ticks; measured over the
  81 offsets, every drop from 30 px above is off the end within 32 ticks, the fall included.
- **Where it used to stand only.** The push applies where the end is within 45 degrees under one of its circles --
  exactly where it used to be a floor. A body sliding down a wall's side brushes its lower end nearly level and is
  not pushed (the first version pushed it 1.5 px: the grapple_gap session record showed it).
- **The tie.** Exactly over the end, the push follows the body's horizontal motion, and with none goes right: a fixed,
  deterministic default.
- **Evidence.** No session record, region digest or golden changes.

## Tests

`rcg_tests` "slippery ends: the player and an object slip off a vertical wall's top end ...": dropped from 30 px above
at offsets -10 to 10 px (0.25 apart), the player's feet and an object (radius 16) are below the end within 60 ticks,
to their centre's side (the right exactly over it). "slippery ends: a floor's end still holds ...": a ledge, a floor's
corner on a wall and a wall's end under a floor hold the player 5 px past, over and short of the end; a player falling
down a wall's side past its lower end is not pushed.
