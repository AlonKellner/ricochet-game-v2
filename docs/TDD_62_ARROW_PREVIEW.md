# TDD 62: The arrow's states in the preview (Stage 157)

Your rules (2026-10-25): "The preview should visualize the consequences (like a surface turning on fire) only if the
consequence was in effect before any divergence in the preview. If the arrow turned on fire in a non divergent
preview section, all following still on fire sections should somehow signify the arrow is on fire (red crosses
moving with the dashes along the path or something?)". The same for charge (white Vs). The rule is GAME_SPEC 14.11.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 157.1 | The preview's arrow states: the physical walk's hits as flag changes, each placed at the merged step it ends (walk order, by point); per step the states to mark (begun before the divergence); the flags at the divergence and the things they change | Done |
| 157.2 | The drawing: the marks along a step (lines and arcs) moving with the dashes, red crosses, white chevrons, pale blue stars (frost: not asked, added alike); the glow node for things on fire, charged or frosted, the preview's consequences dashed | Done |
| 157.3 | Pass sides carrying a tag's writes plannable (and objects with tags); the torch and frost barrier plannable again; tests; docs; `make gate` | Done |
| 157.4 | The user's interactive sign-off | Done (the user's sign-off 2026-10-25) |

## Decisions

- **"Before any divergence" is the existing divergence (GAME_SPEC 14.2).** Since Stage 134 a state change no plan entry
  names is itself a divergence: the planned walk computes only its entries. So a consequence shows when every surface
  that changes the arrow's state on the way is planned. That keeps the rule strict and the same as the path's
  colours, but it needs those surfaces to be plannable: a pass side that carries a tag's writes now is, and the
  torch and frost barrier are again (Stage 156 had made them plain passes).
- **Changes are placed by point, in walk order.** The display path is cut and merged, so its steps have no index into
  the solver's steps. Every change happens at a hit, and every hit ends a step, so the k-th change belongs to the
  first step after the previous one that ends at its point.
- **A state that began before the divergence stays marked after it.** Your rule: "all following still on fire
  sections". The marks follow the physical walk (its own steps after the divergence) while the state holds.
- **Frost gets marks too.** Not asked; drawn the same way (pale blue stars) so the three states read alike.
- **The glow node also shows what things are.** A preview of a thing catching fire needs the thing's current state
  drawn too: on fire (orange), charged (white), frosted (pale blue), solid under the dashed consequences.

## Tests

`rcg_tests` "arrow states: the preview marks ...": an unplanned torch diverges the walks (no consequence); planned,
the arrow's steps after it carry fire marks and the flammable wall is the consequence, and the glow node is drawn;
an unplanned mirror that turns the arrow through a torch onto a flammable floor shows no marks and no consequence
(the shot still burns it); a planned charger and frost barrier mark both states.
