# TDD 57: Spreading (Stage 152)

The third of the next primitives sketched in TDD_52: fire and electric charge that flow between elements without
an arrow. The rule is in GAME_SPEC 10.6 ("Spreading"); the authoring guide in docs/ELEMENTS.md ("Tags and
spreading").

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 152.1 | The format: `tags` on templates, elements and objects; the level's `spread` rules (default: `burnt` through `flammable`, kept; `charge` through `conductive`, sourced by `powered`); the lowering keeps each element's tags by its first surface id | Done |
| 152.2 | The session's spread: the touch graph of tagged things (segments, arcs, object circles within 0.5 px), cached per world version; persistent rules flood through every member, recomputed ones through present members (an absent one lit, passing nothing on); run at the start, after a landing, per tick, at a journey's end | Done |
| 152.3 | Templates: `conductor` (pass or wall), `battery` (powered), `charged_door` (gone while charged); `burnable` tagged `flammable` | Done |
| 152.4 | The editor (an element's and an object's `tags` row, the level's `spread` row); the demo level `objectives/circuit`; tests; docs; `make gate` | Done |
| 152.5 | The user's interactive sign-off | Done (the user's sign-off 2026-10-25) |

## Decisions

- **A game-layer rule, not a path effect.** Spreading writes flags; the solver, the preview and visibility see only
  the flags, as with detectors. No solver or oracle change.
- **Touching is geometric, within half a pixel.** Elements meet at shared endpoints (identical floats) or by
  crossing; a box resting on a wire touches it. The graph is rebuilt only when the world's version changes (a
  door opening does not change geometry, but presence is read per run).
- **Persistent flows ignore presence; recomputed flows respect it.** A burnt barrier is gone, yet the fire went
  through it to what it touched. A charged door is gone too: if charge flowed through it, the door would close
  the circuit by its own opening. So an absent conductor is lit (the door stays open while the circuit holds)
  but passes nothing on.
- **Recomputed flags are written only when they change**, so a steady circuit costs no flag version bump and no
  preview or visibility rework.
- **The level's rules replace the defaults.** A level that names its own rules gets exactly those.

## Tests

`rcg_tests` "spread: ..." (4): a fire arrow burns a barrier and the fire spreads to the one touching it, not to one
apart; a battery charges through a closed breaker door but not across a gap, a conductive object in the gap
closes the circuit and opens a charged door, the breaker opened breaks it again and undo restores it; the demo
level (a popped bubble drops a box onto the wires' ends, the door opens, the player walks to the goal); the
level's rules (strict keys, a custom rule replacing the defaults). The session golden gains
`objectives__circuit__play`.
