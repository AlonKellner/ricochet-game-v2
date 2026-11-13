# TDD 99: A grapple stopped at a solid mirror (Stage 221)

Your report (2026-11-12, F12 dumps in grapple_mirror_2): "Why does the player stop at the mirror? They should keep
following the arrow path", then (2026-11-13) "it's a pixel perfect issue ... It's actually more common for this issue
to not happen. Why?" The player on the platform at (1337.92, 213.92), the plan [mirror_1 left, grapple_1 left], the
arrow bouncing off mirror_1 (`body` `block`, x 500) and landing on grapple_1 (x 700): two cursors a pixel apart, one
grapple ending at the mirror, the other at the grapple. Then (2026-11-13, grapple_mirror_3, testing the first fix):
"Why does the player stop at the mirror? ... There are no obstructions in between" -- a solid mirror lying flat
(y 500) under the arrow, down from the platform to it and up to a grapple (y 200), the grapple stopping at the mirror
at every aim.

## What happened

A journey's guide moves along the arrow's path in strides of 15 px (900 px/s at the 1/60 s tick) while the traveller
holds it -- the guide within the traveller's radius (12 for the player) of its centre -- and waits otherwise
(GAME_SPEC 25.6). So it waits at multiples of 15 px along the path. A traveller pressed against a solid surface
rests a skin (0.08 px) off it: its centre 12.08 px from the surface's line. At a bounce the guide waits on the path,
which touches the solid mirror's line there; a wait within 0.08 px of that line was out of the traveller's reach,
it could get no closer, and the journey ended at the mirror. The dumps' waits: 0.068 px past the bounce and 0.027 px
short of it (stuck), 0.41 px short (followed on) -- about 0.16 px in every 15 px stride, roughly one aim in a hundred.
A pull's guide walks the same path back and met the same.

**The tall player.** The guide's reach is measured from the traveller's position, the player's head (the arrow's
origin, Stage 173), but under gravity the player is three circles down to its feet, 24 px below. On a flat solid
mirror its feet rest on the mirror and its head is 36.08 px above it; a guide waiting at the bounce, on the mirror,
was out of the head's reach (12.16) whatever the stride's phase -- every aim.

## The fix

| Phase | Topic | Status |
|---|---|---|
| 221.1 | The guide's reach: the traveller's radius and two skins (the skin it rests off what it touches, and a margin) | Done |
| 221.2 | Tests; the session record re-recorded after your approval; `make gate` | Done |
| 221.3 | The tall player: a traveller no closer to a waiting guide within its whole shape (head to feet, and the reach's skins) holds it; the guide moves on the next tick. Tests; no session record changes | Done |
| 221.4 | The user's interactive sign-off | Done ("It works perfectly, approved, proceed", 2026-11-13) |

## Decisions

- **The reach, not the bounce.** A rule for bounces (the guide moves on when the traveller is pressed against the
  surface it bounced off) would need the contact's identity; the guide's reach a skin wider covers every waiting
  guide on a line the traveller rests against, and a guide a little further than the traveller's radius is still
  held by it. A fence or a wall the arrow does not bounce off still stops the traveller: the guide waits beyond it.
- **The whole shape, only when stalled.** Holding the guide anywhere in the player's tall shape at every tick would
  let it run up to 48 px ahead of the head on a way down, every grapple's ticks changing; held this way only when the
  traveller can get no closer (where the journey would have ended), with the path not yet at its end, a journey that
  never stalled is tick for tick as before -- the session record is unchanged. An object is one circle: the same
  reach as before.
- **Evidence (your approval, 2026-11-13).** Two of 62 session records changed, each in one shot's journey:
  objectives__circuit__play (a pull) 33 flight frames instead of 34, its preview trail one point fewer and within
  0.94 px of the old one along its 123 px; objectives__platforms__play (a grapple) 16 frames instead of 17, within
  0.004 px along its 159 px. Both end where they did; the player, the world and the flags after each shot unchanged.

## Tests

`rcg_tests` "grapple: the user's grapple_mirror_2 ...": the dumps' state and three cursors -- the preview's trip ends
at the grapple (x 687.92, not the mirror's 512.08) and the shot where the preview said. "pull and grapple: off a solid
mirror the traveller follows the bounce ...": the path's length to the bounce swept through a whole stride, 0.03 px
at a time, a grapple's player and a pull's box (500 each; before the fix 6 of each stopped at the mirror). "grapple:
off a solid floor the tall player follows the bounce ...": grapple_mirror_3's dump -- the trip ends under the grapple
(y 212.08, not on the mirror at 463.92), the shot where the preview said -- and forty players standing on a platform
over a flat solid mirror, a grapple above: each ends under the grapple (before the fix all forty on the mirror). The
user's levels are in the tests as in their dumps (the levels have moved on since).
