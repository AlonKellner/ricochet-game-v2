# TDD 98: The panel's small frictions (Stage 220)

Your request (2026-11-11): "any other editor improvements like completion and ease of use". The plan you approved
(2026-11-11): Stages 213-220; this one the last: the small frictions the survey found in the panel and the status bar.

The rules are docs/EDITOR.md (the status bar). The game does not change.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 220.1 | A command's hint lasts until the next key or until the pointer moves to another of the panel's rows (a placement's instructions until it ends) | Done |
| 220.2 | The status bar red only while it says the problems, not over a hint | Done |
| 220.3 | A value cut in the panel shown whole on hover; a chip's label cut where its chip ends, whole on hover | Done |
| 220.4 | The user's interactive sign-off | Done ("approved, proceed", 2026-11-11) |

## Decisions

**Hints that go.** A command's message (a refusal, "3 selected") stayed in the status bar until the next command, so
the hovered rows' hints could not be read. It now goes with the next key, or when the pointer moves from the row it
was over to another of the panel's rows; over the canvas it stays (a message about what was just placed). A step's
placement keeps its instructions until the placement ends.

**Red for problems.** The status line was red whenever the level had a problem, also while it showed a hovered row's
hint. It is red now while it says the problems with nothing after them; a hint reads in the plain colour (the
problems stay red in the panel).

**Whole on hover.** A value longer than its 28 characters, and a chip's label, were cut with no way to read them but
editing; hovered, the status bar shows them whole. Chips' labels are cut where the chip ends (a one-chip row is the
panel's width) instead of at fifteen characters.

## Tests

- `rcg_tests`, `test_editor.cpp` "the panel's small frictions": the palette's `one_sided_mirror` whole on hover; a long
  `flags` value whole on hover; a refusal's hint gone with the next key, and with a move to another row; the status red
  over the canvas, plain over a row.
