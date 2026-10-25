# TDD 58: Light and shadow (Stage 153)

The last of the next primitives sketched in TDD_52. Your description: "Shadows stop arrows and hide and deactivate
any surfaces in the shadows. The way it works is by having barriers which define dark regions and regions which
define light regions, they may be the sides of a line, or inside/outside of a circular carrier. Anything that is
within only dark regions and no light regions is in shadows. The effect of crossing a light/dark barrier is
blocking the arrow if that area is in shadows. Light sources have light regions around them." The rule is in
GAME_SPEC 9.9; the authoring guide in docs/ELEMENTS.md ("Light and shadow").

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 153.1 | The format: a template's `shade` (`type`, `side`, `when`, expressions); the lowering records it; the level's `shades` (a line has no inside; a shade does not move) | Done |
| 153.2 | `rcg/shade.hpp`: regions (half-planes, disks and their outsides), the arrangement of their boundaries split at crossings (lines clipped to the bounds, collinear lines and equal circles sharing a carrier), the boundary under the active regions, the fill (Clipper2, earcut), darkening | Done |
| 153.3 | The world: ids reserved for the arrangement's pieces, the boundary after every other surface, darkened surfaces in the rebuild; the session's `shadow()` at the start, a landing, a tick, a journey's end and an undo; the `["shadow"]` node | Done |
| 153.4 | Templates `dark`, `light`; the editor draws shades in their colours over the shadow under the starting flags; the demo level `objectives/lantern`; tests; docs; `make gate` | Done |
| 153.5 | The user's interactive sign-off | Done (the user's sign-off 2026-10-25) |

## Decisions

- **The game builds the shadow's edge; the solver sees surfaces.** The edge is made of ordinary one-sided surfaces
  (terminal from the light, a pass from the dark), so the walker, the preview and visibility need no change. The
  visibility region stops at the shadow's edge for free.
- **The arrangement is fixed, its pieces' presence is not.** Every shade's boundary is split by every other one,
  active or not, once, when the level starts. So each piece keeps one id while lamps switch on and off, undo
  restores the same surfaces, and records stay deterministic.
- **A shot sees the shadow of its moment.** A lamp switched on mid-flight lights the next shot, like moving surfaces
  (TDD_56). Making the edge depend on flags inside the walk would need presence conditions per combination of
  lamps.
- **"Deactivate" means for the arrow.** A darkened surface is a pass with no hits. It stays solid for the player
  (a wall in the dark still holds the player) and keeps its presence (a closed door in the dark still blocks the
  way). It is drawn as it is, under the fill. Only surfaces lying wholly in the shadow are darkened (its point in
  the shadow and no edge crossing it): one half lit stays whole, and its dark half is out of the arrow's reach
  anyway unless the arrow starts in the shadow.
- **Regions are geometric, membership is floating point, sides are by provenance.** A piece's own carrier decides
  its two sides symbolically (the regions on that carrier). The other regions are evaluated at a point of the
  piece, which no other carrier passes through (the pieces end at every crossing). Crossing points are rounded to
  float32 once and shared by both carriers' pieces, so pieces meet at identical points.
- **Shades do not move (v1).** A moving shade would change the arrangement's piece count; its ids would churn.
- **The shade's own surface is never present** (presence `el:<name>:barrier`), so it costs nothing in the walk, and
  the editor still selects and drags it. The game draws shades in the shadow's node.

## Tests

`rcg_tests` "shade: ..." (7): a region's sides are the surface's sides (lines and circles); the arrow stops where it
would enter a dark disk and a button in it is darkened, the shadow node is drawn; light under a flag undoes it, a
switch turns the lamp on and the next shot presses the button, undo turns it off and darkens the button again; an
arrow fired in the shadow passes a darkened wall and leaves to press a lit button; the level's errors; the fill's
area (an annulus) and the boundary's pieces per active set; the demo level. "editor: shades are drawn in their
colours over a faint shadow". The session golden gains `objectives__lantern__play`.
