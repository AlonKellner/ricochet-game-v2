# TDD 86: One style, one painter -- the editor draws as the game does, the player over the surfaces (Stage 208)

Your request (2026-11-10): "I don't like the visuals on grappling surfaces or pullable objects. The icons thing is not
very good, please come up with a couple of alternative options and a recommendation. Also, scrutinize the entirety of
the visualization system and let me know if you have any suggestions for improvements." Then: "Plan these changes,
including your recommended option B." The plan (approved 2026-11-10) is three stages: this one (one style table, one
surface painter, the draw order), the marks that say what a surface does (option B, Stage 209), and the hues,
accessibility, target states and GAME_SPEC 22 (Stage 210).

The rules are GAME_SPEC 22 (the Stage 208 note).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 208.1 | The style table `rcg/style.hpp`: every colour, width and size named; the inline literals named (draw lists byte-identical) | Done |
| 208.2 | The surface painter public `rcg/surface_paint.hpp` (draw lists byte-identical) | Done |
| 208.3 | The editor paints elements, edges and objects with it (`render::transform`, the level's starting flags) | Done |
| 208.4 | The surfaces before the player, the cursor and the HUD (z0; the digest unchanged) | Done |
| 208.5 | The user's interactive sign-off | Done ("Works perfectly! All stages are approved", 2026-11-11) |

## Decisions

**A pure refactor first.** The style table and the shared painter change no call: the session golden replays byte for
byte after each (`make sessions-check`), the proof the game draws as before. Colours given an alpha at a call site
(`becoming`) have a named base colour and `render::with_alpha`, which yields the same floats.

**The editor draws what the game draws.** It kept a copy of the palette and its own painter, which had drifted: a
lifted blue, a pass side coloured by its hit kind (target gold, spawn cyan, reset pink) where the game draws every such
side yellow, objects orange (pullable) or gray, no hit marks, a whole surface dashed in its colour when any flag gates
it. Now each built surface goes through `paint::surface` under the level's starting flags (kept by `refresh()`), mapped
to the screen by `render::transform` (points through `to_screen`, radii by the zoom; widths, dashes and fonts in
constant pixels, the editor's convention) into the element's one node. Objects are built with `object_spec` and painted
the same way (by their face). The editor keeps its own overlays: halos, names, side ticks, errors, broken geometry,
motion poses, shades; and a faint dashed hint on a surface present only under a flag (what its old dash said). A
surface absent at the start is dashed gray, as in the game. Mirrors are the game's pure blue (darker on the canvas than
the editor's lifted one; Stage 210's hues may lift it for both).

**The player over the surfaces.** The player, the cursor, the HUD and the surfaces share z0, and a host draws equal z
in list order (Godot by child order, the Python host in order); the surfaces came last, so a platform or a shield the
player overlapped was drawn over it. They now come first. The session digest sorts by z then key, so no sha moves.

## Tests

- `rcg_tests`, `test_editor.cpp`: the editor draws a shield's band, a fence's steel band and an object in its face's
  colour (red, not the old orange); the edges' strokes counted as the game draws them (a 3 px stroke, or two 1.5 px
  side strokes).
- `test_bodies.cpp`: every surface node comes before the player node.
- The session golden byte-identical (`make sessions-check`, ctest `rcg_sessions_*`, `capi_sessions_*`).
