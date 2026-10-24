# TDD 55: Bubbles (Stage 150)

The first of the next primitives sketched in TDD_52. In your words: "circular surfaces which can float and cause
objects to float, pop on impact". A bubble is an object (Stage 146) with three new properties. The rule is in
GAME_SPEC 25.5.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 150.1 | Objects gain `gravity` (a factor of the level's), `pops` (a hit sets `obj:<name>:popped`; presence while unset) and `carries` (one level) | Done |
| 150.2 | The bodies' step: a body's own gravity; a popped body is gone (no step, no collision); carried bodies follow their carrier at their offset | Done |
| 150.3 | Editor rows; the demo level `objectives/bubble_drop`; tests; docs; `make gate` | Done |
| 150.4 | The user's interactive sign-off | Open |

## Decisions

- **Floating is gravity, reversed.** The controller already steps a body under any gravity vector. With an
  upward one, a bubble rests against a ceiling because each tick's move into it is removed. "Floor" there means
  nothing (it is never on one), and the floor snap is skipped (it only applies when not rising).
- **A pop is a flag and presence,** so the arrow's own shot sees the bubble vanish at the hit and passes on, and
  undo brings it back. A bubble with `face: pass` lets the arrow through; a wall-faced one stops it as it pops.
- **"Cause objects to float" is carrying:** the carried object keeps its offset from its carrier and skips its
  own physics until the carrier pops. Bodies still do not meet each other (Stage 146, v1). Carrying is the one
  link between them, one level deep.

## Tests

`rcg_tests` "bubbles: ..." (2): a bubble floats against the ceiling holding a box 40 px below it, the arrow pops
it (its flag set, its surface absent), the box falls to the floor, and undo restores the bubble; the demo level
played to its goal (the box falls on a plate that opens the goal); the schema (`carries` names another object,
one level only). The session golden gains `objectives__bubble_drop__play`.
