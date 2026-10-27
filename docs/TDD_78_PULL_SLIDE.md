# TDD 78: A pull stopped short of the player (Stage 174)

Your report (2026-10-27, an F12 dump in pulling_2): "Why is the pulling preview stopping before reaching the
player?" The player at (343.33, 1067.92), the cursor at (844.44, 1072.38), short of the box resting on the floor at
(890, 1063.92): the preview's box stopped at x 499 with an orange ring, about 130 px short of the player.

## What happened

Two faults in the journey's code, which the preview and the shot share (`Session::journey_of`,
`Session::journey_move`), so the live pull stopped there too.

- **The slide ran into the floor.** The traveller moves straight toward the guide, which is below the box's centre
  on the arrow's line, so it meets the floor at once and slides along it. The slide took its normal from the nearest
  point on the floor (`kinematics::distance`), and rounding that point tilts the normal by about 1e-16: the slide
  kept about 1e-13 px into the floor. The box rests a float32 ulp inside the sweep's skin (y 1063.920044 against
  1080 - 16.08), and a sweep from inside the skin that approaches at all is a contact at once (`t = 0`). Where the
  residue's sign pointed into the floor (x 499.458 here) the box did not move. The guide had already moved on, out
  of the box's reach, and waited; the box could get no closer, so the journey ended there, short of the player.
- **A portal at the cursor.** The path marked a jump (a portal crossing) where two neighbouring points came from
  different steps and were more than 1e-3 px apart. A step that starts where the last ended skips its first point,
  so its next point is a whole sample (about 4 px) on, and every step boundary -- the cursor's among them -- was
  taken for a portal: the box crossed it without collisions, its trail broke, and it slid back 16 px.

## The fix

| Phase | Topic | Status |
|---|---|---|
| 174.1 | The slide leaves its contact by a hair (a billionth of its length, outward): no rounding residue makes it a contact at once | Done |
| 174.2 | A jump is marked where the path is built: only a step that does not start where the last ended (a portal), not every step boundary | Done |
| 174.3 | Tests; the session record re-recorded after your approval; `make gate` | Done |
| 174.4 | The user's interactive sign-off | Done (the user's sign-off 2026-10-27) |

## Decisions

- **A hair, not an exact normal.** The player's own physics (`kinematics::step_body`) slides along the sweep's exact
  line normal and does not meet this; the journey keeps its nearest-collider normal (it also slides round the
  player's circle) and leaves the contact by a billionth of the slide: about 1.5e-8 px on a 15 px slide, far above
  the ~1e-13 residue and far below a float32 ulp at these coordinates, so stored positions do not drift. The fix
  stays in the journey's code: nothing else moves differently.
- **Evidence (your approval, 2026-10-27).** One of 59 session records changed, objectives__circuit__play. Its shot
  pops the bubble and pulls it back: before, the bubble stopped in mid-air at (885.7, 280.3) after 28 flight frames;
  now it follows the arrow's path down and rests on wire1 at (856.6, 355.9) after 33 frames, the arrow's gap there
  (x 900-940, 40 px) narrower than the bubble (48 px). The box's fall, every flag and the win are unchanged. With
  the slide fix alone, the false portal at the cursor let the bubble cross the wires (no collisions while crossing)
  to the player -- both fixes are needed.

## Tests

`rcg_tests` "pull: the user's pulling_2 ...": the dump's state -- the preview's box comes to the player, its trail in
one leg, and the live journey ends where the preview said. "pull: a box resting on the floor comes to the player
from anywhere ...": forty player positions, the cursor on the box (one step) and short of it (two): every preview
ends at the player, in one leg.
