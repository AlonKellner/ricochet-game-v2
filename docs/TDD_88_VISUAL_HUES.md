# TDD 88: Hues, accessibility and states -- a button's key, a target's halo and checkmark (Stage 210)

Your request (2026-11-10): the visualization review's last stage -- hues by family, a second channel for divergence,
the target states GAME_SPEC 22 promised, GAME_SPEC 22 rewritten; and "can you also improve the visual of buttons?
Suggest a couple of options". Your choice: option A, a key outline (2026-11-11).

The rules are GAME_SPEC 22 (22.1, 22.2, 22.3, 22.4, 22.6).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 210.1 | A button's key: a rounded amber outline, dim while its flag is off, bright and filled once on | Done |
| 210.2 | Pass sides with hit effects gray (their marks and keys say what they do); the ghost toggler keeps its colour | Done |
| 210.3 | Targets: a halo until registered, then at half with a checkmark | Done |
| 210.4 | Hues: spawn teal, the reflection blue lifted; projections unchanged | Done |
| 210.5 | Accessibility: dim and crossing dots 3 units (diverged steps stay dashed: the user) | Done |
| 210.6 | GAME_SPEC 22 rewritten from the style table | Done |
| 210.7 | Goldens (every session's draw shas) and the user's interactive sign-off | Done ("Works perfectly! All stages are approved", 2026-11-11) |

## Decisions

**A button is a surface that writes a level flag.** `button_flag` (surface_paint.cpp): a state write on any side or
resolver row whose key is not the arrow's (`shot:*`), an object's (`obj:*`), an element's own (`el:*`) or the ghost.
Its key is a capsule about the piece (round ends; an arc's follows it; a full circle two rings), `KEY_HALF_WIDTH` 8 out,
in the doors' amber -- a button and the door it opens share it. Its state is its flag's truth under the flags the frame
is drawn with: a toggle shows on and off, a one-shot stays lit. The key replaces the diamond on a stop button.

**Yellow freed.** A pass side with hit effects was drawn in the flag toggler's yellow, whatever its hits were (a button, a
spawner, a torch, a charger, water): now gray, the colour of what the arrow does, its marks saying the rest. Forward
projections stay yellow-orange: with the pass sides gray, yellow is no longer shared, and an orange forward would crowd
the back projection's orange.

**Targets without animation.** GAME_SPEC promised a pulse; a pulse changes every frame's draw list (the hosts' caches,
the session golden). A static gold halo marks a target not yet registered; registered (`target:<id>` true), its look is
drawn at `TARGET_DONE_ALPHA` and its ring is a checkmark.

**Divergence stays dashed.** The plan gave diverged steps a second channel besides their hue: dotted (3 on, 5 off)
beside the aligned dashes (10 on, 5 off). The user, on the renders (2026-11-11): "I prefer ... for diverged to remain
dashed instead of dotted, this way the transition from a divergent cursor position to an aligned one is less jarring,
just a change of color and not a change of line pattern." Reverted; every step dashes 10/5.

**The reflection blue lifted** from 0000FF to (0.25, 0.45, 1.0), the editor's old lift, for both: pure blue on the dark
background was the least legible colour.

## Tests

- `rcg_tests`, `test_bodies.cpp` "states": a button's key dim with its flag off, bright and filled on; a target's
  halo, and registered: no halo, a checkmark per side, its face at half; "marks": a stop button has no diamond, its key
  dim.
- `test_present.cpp`: a crossing dot of 3.
- `test_bodies.cpp`: the counterpart's dim dot of 3.
