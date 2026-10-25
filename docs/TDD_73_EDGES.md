# TDD 73: The level's edges (Stage 169)

Your request (2026-10-25): "I want levels to have a simple configuration for the level bounds, whether to set them
to blocking, reflective, pass-through, or whatever any other effect. Notice that level bounds are not the room
borders, but the actual level borders."

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 169.1 | The lowering: the level key `edges` -- a kind or `{"kind", ...params}` for all four edges, `"wrap"`, or `{top, right, bottom, left}` of those and `"pass"`. Each configured edge is an element of its kind on the reversed edge wall (its left side, a face, toward the level), through the same step as a placed element (tags, regions). A wrap is a portal at the right edge (d = (-w, 0)) and one at the bottom (d = (0, -h)). Unconfigured levels lower byte for byte as before | Done |
| 169.2 | Tags and shades carry their origin prefix (an edge's is `level.edges.<edge>`) | Done |
| 169.3 | The ∞ band: only along the edges that pass (`open_edges`: an edge with a surface that is not a non-plannable pass closes it) | Done |
| 169.4 | Migrate: configured edges read back as the `edges` key (per edge: its pass wall, a kind matched on the reversed wall, or a wrap's portal); the lowering check guards it | Done |
| 169.5 | The editor: the `edges` field; configured edges drawn on the bounds in their face's colour with a tick inward; their mistakes named `edges` | Done |
| 169.6 | The demo level `objectives/mirror_hall` (a mirror top edge, arrow-block sides and bottom: the button behind the screen is reached by a bounce off the top edge) and its session | Done |
| 169.7 | Tests; docs (GAME_SPEC 9.10 and 4.2, 2.76.0; LEVEL_FORMAT, EDITOR, CHANGELOG); `make gate` | Done |
| 169.8 | The user's interactive sign-off | Open |

## Decisions

- **The edges are elements.** Any kind, builtin or a level's own template, can be an edge, with its params, tags
  and hits. No effect is special-cased: "blocking" is `wall` or `arrow_block`, "reflective" `mirror` or
  `two_sided_mirror`, "pass-through" the default.
- **Faces inward.** An element's left side is its face (a mirror's reflective side). The edge walls run clockwise,
  which puts their left sides outward, so a configured edge is placed reversed.
- **Wrap as portals.** Joining opposite edges is a translation portal pair, so the solver needs nothing new. The
  arrow keeps its direction and its height, unlike the wrap through infinity, which brings it back along its own
  line from the other side.
- **Infinity.** An edge that is not a pass is no way to infinity, so the ∞ band is left out along it. Elsewhere,
  and for the circled cross of a transformed frame, nothing changes.

## Tests

`rcg_tests` "edges: ...": the default (an arrow wrapping in from the left stops at a wall); mirrors turning the
arrow onto a button behind the player, walls and arrow blocks stopping it, one edge configured; faces inward,
labelled, reflective and plannable; wrap carrying an upward shot to the left edge at the same height (through
infinity it would miss); the ∞ band along passing edges only; the errors; migrate reading configurations back.
"editor: the level's edges ...": the field, the drawn edges, the named mistake. The level counts are 47 (the demo).
No existing record, dump or golden changed.
