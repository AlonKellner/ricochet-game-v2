# TDD 72: The player's light (Stage 168)

Your request (2026-10-25): "Around the player there should be a small circle of light at all times, such that they
can walk into darkness and see their character and immediate surroundings."

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 168.1 | The shadow's area kept (`shade::area`, `fill_of`; `fill` unchanged byte for byte); `shade::distance` (0 inside); `shade::around` (the area in bands round a point, then the rest) | Done |
| 168.2 | The draw: within 80 px of the shadow, the `["shadow"]` node draws the bands round the player (a veil to 44 px at alpha 0.15, then 56, 68, 80 px at 0.32, 0.5, 0.68, the full 0.82 beyond), cached by the player's position; elsewhere the plain fill, as before | Done |
| 168.3 | Tests; docs (GAME_SPEC 9.9, 2.75.0); `make gate` | Done |
| 168.4 | The user's interactive sign-off | Open |

## Decisions

- **Display only.** A light that counted for the arrow would add a moving edge round the player. That edge stops
  the arrow from the lit side, so a shot fired from inside the circle could never leave it into the dark. Shades
  are also static by design (their boundary's ids are fixed when the level starts). The light shows; the shadow
  still decides.
- **A veil, not a hole.** Inside the circle the shadow is thinned, not removed. The player and the floor show, and
  what lies in the shadow stays visibly dim: it is still a pass for the arrow.
- **Only near the dark.** Far from the shadow the node is the plain fill under the same key as before. Records
  change only in frames where the player is within 80 px of the shadow.

## Tests

`rcg_tests`:
- "shade: the player's light -- the distance to the shadow and its bands around a point": the distance inside the
  shadow, in a light hole, and outside; the band areas; no bands far away.
- "shade: the player's light -- drawn round them near the shadow ...": one triangle call far away, five inside the
  dark disk; the darkened button is unchanged.

The session golden: `objectives__lantern__play` changes its `draw` hash in one frame of twelve (the player beside
the shadow); every other field and frame is identical.
