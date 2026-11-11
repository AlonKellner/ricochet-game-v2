# TDD 91: A door's look from its `open` (Stage 213)

Your request (2026-11-11): plan and do the queue -- first, a door's look from the `open` feature: "a wall given
`+ door` opens and closes but draws no posts". The plan you approved (2026-11-11): Stages 213-220.

The rule is GAME_SPEC 22 (the door row).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 213.1 | `is_door`: the label (Door, ChargedDoor), an opening's swing, or an `open` condition -- a presence entry `[DEFAULT, false]` not keyed by an element's own state or a channel | Done |
| 213.2 | Goldens: none move (no level has an `open` on a surface other than a door's) | Done |
| 213.3 | The user's interactive sign-off | Pending |

## Decisions

**By what it does, not its name.** `is_door` (build.hpp) read the label the kind gives (`Door`, `ChargedDoor`) or an
opening's swing. Since Stage 207 any surface takes `open` and Stage 212 adds it in one click, so a wall, a mirror or a
button that is gone while a flag holds was drawn as if it never moved. `compose` writes `open` as one presence entry,
values `[DEFAULT, false]` (surface_element.cpp); that entry now makes it a door. No rc-level/1 change: the built
surface's `presence` says it.

**Not an element's own state.** A burnable is gone while `el:<name>:fire` holds, and the retired toggle surface while
`el:<name>:off` did -- the same entry shape. Those vanish by their own state, not by a door's flag, so keys under
`el:` stay out (a charged door stays a door by its label). Spawn channels' host conditions (`chan:`, values
`[DEFAULT, armed]`) never matched.

## Tests

- `rcg_tests`, `test_bodies.cpp` "any surface with an open is a door": a wall's posts closed (full) and open (faint);
  a mirror's with an expression `det:plate && !locked`; a wall whose `open` is `el:d:fire`: none.
- The session golden unchanged (`make sessions-check`).
