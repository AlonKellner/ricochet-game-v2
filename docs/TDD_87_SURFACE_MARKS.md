# TDD 87: Marks that say what a surface does -- barbs, fills, a mark per feature (Stage 209)

Your request (2026-11-10): "I don't like the visuals on grappling surfaces or pullable objects. The icons thing is not
very good." Your choice: option B -- the effect drawn along the surface in the journey's mint, barbs along a grapple, a
translucent fill and handle notches on a pullable object, a faint gray fill on a static one. Your approval of the
renders (2026-11-10): "Looks very good! Approved".

The rules are GAME_SPEC 22.6.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 209.1 | A surface's `object` (display only, from `object_spec`) | Done |
| 209.2 | The marks: grapple barbs, object fills and handles, a mark per feature, rows at the marker's point following arcs | Done |
| 209.3 | The arrow's state glyphs shared (`paint::state_glyph`: the path's marks and the tags' marks) | Done |
| 209.4 | Goldens: the session golden's draw shas (13 sessions) | Done (approved) |
| 209.5 | The user's interactive sign-off | Done ("Looks very good! Approved", 2026-11-10) |

## Decisions

**Why not icons.** Every hit kind but target, spawn and reset drew the same 5 px yellow diamond beside the surface's
middle: a grapple, a pull, a button, a break, a pop, a tag. A grapple read as a wall with two dots. The marks now say
what the surface does, on the surface: the barbs run the whole grapple, and an object's fill is the whole object.

**The journey's mint.** The preview already draws a grapple's and a pull's trip in it (`JOURNEY`); the surface that
starts a trip now carries the same colour.

**One mark per feature.** `mark_of` (surface_paint.cpp) names a hit's mark by its kind and, for a state write, its key
(`shot:<name>:broken` a crack, `obj:<name>:popped` a burst, a key ending in fire, charge or frost that glyph); a plain
flag write on a pass side has none (its side's colour says it, as before). Marks are deduplicated per side.

**Where.** At `marker_point` (the designer marker's point) rather than a piece's via (a full circle's lower left), 13
units off (clear of the designer's marker); on an arc the row lies on the offset arc by arc length. A full circle whose
sides mark alike shows them once, outside (an object's inner marks crowded its centre).

**The path's glyphs unchanged.** `arrow_marks` calls `paint::state_glyph` with the same arithmetic; no preview session's
path marks moved (the 13 sessions that changed hold grapples, objects, breakables or tags).

## Tests

- `rcg_tests`, `test_bodies.cpp` "marks": a grapple's barbs on both sides every 18 units, no diamond; on an arc each
  barb roots 3 units off its carrier; a pullable object's mint fill and four handles, a static one's gray fill; a break's
  crack per side, a charger's chevrons, a button's two diamonds; a full circle's target ring once, at its top, 13 out.
- The session golden re-recorded for the 13 sessions (draw shas only).
