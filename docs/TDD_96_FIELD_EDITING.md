# TDD 96: Field editing that does what one expects (Stage 218)

Your request (2026-11-11): "any other editor improvements like completion and ease of use". The plan you approved
(2026-11-11): Stages 213-220; this one the frictions the survey of the panel's text fields found.

The rules are docs/EDITOR.md (the panel). The game does not change.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 218.1 | A default edited from its text (`Row::draft`: a parameter's default as it reads for the element, an item's, an opening's flag); unchanged, nothing written | Done |
| 218.2 | Keys through a field: Ctrl+S, Ctrl+Z, Ctrl+Y, F5 apply it first; Ctrl+Backspace clears before the caret | Done |
| 218.3 | Malformed text refused: JSON that does not read (it was taken as a string), a flag expression that does not compile (tinted as typed) | Done |
| 218.4 | The scroll settled: a new selection from the top, never past the rows, a newly edited field and its list in view | Done |
| 218.5 | The user's interactive sign-off | Pending |

## Decisions

**Defaults as text.** A click on `(door:gate)` started from an empty field, so changing a default meant retyping it.
Each row now carries the text its default reads as (`Row::draft`; a parameter's default evaluated for the element --
`door:${name}` is `door:gate`; a word about a value, `(none)`, `(auto)`, `(the face)`, has none). Applied unchanged it
writes nothing: the document still says only what differs.

**Keys through a field.** A focused field swallowed every key, so a save or an undo meant leaving it first. Ctrl+S,
Ctrl+Z, Ctrl+Y and F5 now apply the field, then act (a refused field keeps the focus and the key does nothing). The
prompts (a new level's name, the search) keep their own keys. Ctrl+Backspace clears what is before the caret -- the
field cleared from its end, where the caret starts. (Select-all is not offered: the field has no selection.)

**Malformed text refused.** Text that does not read as JSON became a string, so `[1, 2` was saved as the string
"[1, 2" and the level's problem appeared somewhere else. Text that starts like JSON (`[`, `{`, `"`) and does not read
is refused with the parser's reason. A flag expression is checked by `rc::FlagExpr` as it is typed (the field tinted
red) and refused on Enter.

**The scroll.** It was never reset or clamped, so a short selection after a long one could show an empty panel, and a
field low in the panel had its completion below the window. `rows()` now settles the scroll once per layout: a new
selection from the top, never past the rows, and a field newly given the focus moved up until its row and its
completion's rows show. A wheel scroll is not undone (the selection is recorded before it).

## Tests

- `rcg_tests`, `test_editor.cpp` "field editing": a door's `open` from `door:d` (Enter: nothing written; typed on:
  `door:dx`); Ctrl+S and Ctrl+Z through the field; `a &&` tinted and refused, `a && b` taken; `[1, 2` refused, the
  bounds unchanged; a new selection from the top; a field at the window's bottom moved up with its list in view.
- Earlier tests that typed into a field now clear it first (Ctrl+Backspace); the tags test relied on malformed JSON
  becoming a string.
