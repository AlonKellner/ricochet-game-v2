# TDD 64: Every change plannable, every change previewed (Stage 159)

Your rules (2026-10-25): "By definition, frost changes game state, and so it must be plannable. All game state
changing features must be plannable, that's the point of the game. For example, even grappling should show on
preview, the position of the player after the grapple is complete must be previewed. Same should be true for
pullable objects. Remember that the player or object may be stopped mid grapple/pull because of a player or object
blocking barrier, which is pass-through for arrows." And: "The complementary rule is that all game state changes
must always be somehow reflected in the preview." GAME_SPEC 14.12.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 159.1 | Plannable: a side with hit effects is interactive (the builder, and the oracle's level builder); Stage 157's narrower lowering rule removed | Done |
| 159.2 | Journeys: `journey_of` (a path's first fired grapple or pull) and `journey_move` (one tick of its motion) shared by the live game and the preview; colliders under given flags | Done |
| 159.3 | The preview: the landed flags at the divergence, the journey simulated from there (a hit before the divergence), the consequences drawn (surfaces, targets, goal, lights, released objects, the trail and the end ring) | Done |
| 159.4 | Tests, docs, the session golden re-recorded (draw lists only), `make gate` | Done |
| 159.5 | The user's interactive sign-off | Done (the user's sign-off 2026-10-25) |

## Decisions

- **One rule in the builder.** Every side with a hit effect becomes interactive when built, so no template or level can
  make a game-changing side unplannable. Only the circuit demo had such sides (its battery and charged door).
- **The preview's journey is the journey.** The live journey moves per physics tick; the preview runs the same
  function with the same tick from the same start, swept against the colliders under the flags the shot would land
  with. The tests compare the previewed end with the end after the shot: equal, free or stopped by a fence.
- **Changes are shown up to the divergence (§14.11).** The flags there, the shot's per-shot flags dropped, are what the
  shot leaves if the walks agree that far. Every difference from now is drawn.
- **The session golden changed only in its draw lists.** Sixteen records of seven levels, where a previewed shot
  changes something lasting, gained the consequence marks; their flags, positions, previews and visibility are
  unchanged (compared field by field). The empty node is never added, so other records are untouched.
- **Not yet previewed:** touching rules and shadows recomputed after the landing, a released object's fall, detector
  writes during a journey. Each needs the post-landing simulation of its system.

## Tests

`rcg_tests` "pull and grapple: the preview shows where the traveller ends ...": a grapple free and behind a fence, a
pull behind a fence -- the previewed end equals the end after the shot. "preview: the shot's lasting changes ...": a
button's door in the predicted flags, the consequences node drawn.
