# TDD 72: The player's light (Stage 168)

Your requests (2026-10-25): "Around the player there should be a small circle of light at all times, such that they
can walk into darkness and see their character and immediate surroundings." Then: "The light circle should actually
be a light circle barrier, meaning that crossing it into shadows (dark are with no light), it should be blocked."

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 168.1 | `shade::area` / `fill_of` (the fill byte for byte as before), `shade::distance`, `shade::light` (a light disk of `LIGHT_RADIUS` 64 round a point, none where it does not meet the shadow), `shade::capacity` (the pieces an arrangement with one more circle can have) | Done |
| 168.2 | The session: the boundary's ids reserved for `capacity`; `shadow()` adds the light round the player where it meets the shadow, arranges the regions with it, and builds the boundary, the darkened surfaces and the fill from them; it runs after every tick, a teleport and every step of the player's journey | Done |
| 168.3 | The draw: the fill without the light; the light's edge pieces that stop the arrow dashed in the light's colour | Done |
| 168.4 | Tests; docs (GAME_SPEC 9.9, 2.75.0); `make gate` | Done |
| 168.5 | The user's F12 dump: the light lagged behind the player (its edge 43 px back). `World::set_shade` took a boundary with the same ids and sides as unchanged (true while shades were static); it now compares each piece's geometry too | Done |
| 168.6 | The user's interactive sign-off | Open |

## Decisions

- **A light region like any other.** The light is a disk region (light, inside) appended to the shades' regions. The
  boundary, the darkening and the fill follow from the existing rules: its edge blocks where the outside is in the
  shadow, a surface in it is lit, and a surface it crosses is not darkened.
- **In effect only where it meets the shadow.** When the disk lies wholly in the light, nothing changes. The
  session keeps the static arrangement and its ids, so levels without nearby shadow (and every frame far from it)
  are unchanged.
- **Ids.** The arrangement with the light differs at every position. Its pieces take ids from one block reserved
  at the start: the static pieces, plus two more on each carrier the circle crosses, plus the circle's own pieces.
  The light's pieces are not plannable, and their ids carry no meaning across frames.
- **Consequence.** A player standing in the dark can shoot only within the light, or out where it reaches the
  light. An arrow travels in the shadow only through a portal or a wrap.

## Tests

`rcg_tests`:
- "shade: the player's light -- in effect only where it meets the shadow": distance, `light` (none 100 px away or
  far off), the boundary with the light over an annulus (four edges), shadowed just beyond it and not within.
- "shade: the player's light -- its edge stops the arrow leaving it into the shadow": the world's light circle follows
  the player when its pieces keep their ids (the lag); a player in the shadow, a wall
  in their light lit, one beyond it darkened, the shot to a lit button stopped and the edge drawn. From a spot where
  the light reaches past the shadow's edge, the shot gets out and presses the button. This replaces "an arrow in
  the shadow passes its surfaces and leaves it": its player stood in the shadow, so their arrow now starts in their
  light.

The session golden: `objectives__lantern__play` frame 11 (the player beside the shadow) gains the light's two edge
pieces in its world, with the visibility, preview and draw that follow; every other frame and session is identical.
