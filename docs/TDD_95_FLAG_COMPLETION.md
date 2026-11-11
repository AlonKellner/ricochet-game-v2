# TDD 95: Flag names complete -- the list under the field (Stage 217)

Your request (2026-11-11): "completion for flags names where they can be used, including when writing a boolean
expression. The completion should first show all flags, but just like the partial match level search bring the best
fit flags to the top, and pressing them or tab or some other key should put the top option inline." Your choice: option
A -- a ranked list under the field, Up/Down pick, Tab accepts, the pick drawn dim at the caret. The plan you approved
(2026-11-11): Stages 213-220, this one after the queue.

The rules are docs/EDITOR.md (Completing a flag). The game does not change.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 217.1 | Parameter types `flag`, `expr`, `door` (strings the editor completes): surface_element's `flag`, `opens`, `open`, `when`, `armed`; the shades' `when` | Done |
| 217.2 | `rcg::flag_index(doc)`: every flag written or read, its writers and readers (elements, detectors, the goal, the start, the arrow), read off the document; `door_names` | Done |
| 217.3 | The completion: the word at the caret, ranked by the soft match, rows under the field, Up/Down, Tab, a click, Esc; the dim rest at the caret; the canvas outline | Done |
| 217.4 | The opening flag's chips (Stage 204) retired: the list completes the field | Done |
| 217.5 | Hosts: Tab reaches it from Godot (GUT) and the C API (pytest); `{"op": "complete"}` | Done |
| 217.6 | The user's interactive sign-off | Done ("Works perfectly! Proceed", 2026-11-11) |

## Decisions

**Which fields.** Nothing said which strings are flags, so the parameter specs say it now: `flag` a flag written
(a button's), `expr` a flag expression read (a door's `open`, a switch's `when`, `armed`, a shade's `when`), `door` a
door's name (a button's `opens`); they validate as strings. The editor's own fields join them by their paths (an
opening's flag, a detector's), and the JSON fields (the goal's `requires`, the starting `flags`) complete the string the
caret is in.

**The level's flags, from the document.** `flag_markers` (Stage 179) walks the lowering and only when the level has
no problem, and keeps only flags two things share. The completion reads the document instead: each element's
parameters by their types (defaults filled in where they read, as written where they do not), a raw surface's keys,
the tags' flags, an opening's flag, the detectors, the goal's requires, the starting flags -- each with who writes it
and who reads it. So a level with a problem still completes, and a flag one button writes is offered.

**The ranking.** The level search's soft match (Stage 167), `:` a word start too (`det:pl` matches `det:plate`'s
parts). Empty, every flag, the written ones first; typed, the matches first, best first, then the rest; a name nothing
has yet is offered as itself.

**Tab puts it in, Enter applies.** Enter keeps meaning "the field as typed", so a new flag name is never replaced by a
near one. Tab, or a click on a row, replaces the word at the caret and keeps the field focused -- the click is not the
commit every other panel click is. After a pick the list stays closed until the text changes.

**The rest at the caret.** The text is drawn in proportion; to put the pick's rest right after the caret the field is
laid out a character per cell while completing (as the level search's matched names are).

**The opening flag's chips** (Stage 204: up to nine flags, cut at fifteen characters) gave way to the list.

## Tests

- `rcg_tests`, `test_editor.cpp` "flag names complete": the index of a level (a door and its button, a detector, a
  starting flag, a tag's charge, the arrow's); an expression completed at the caret (`det:pl` -> `det:plate`, then
  ` && !la` -> `lamp`); the list's rows and the pick; a click keeps the field; Esc closes, a second cancels; Up/Down;
  the canvas outline; `opens` lists doors; a level with a problem completes. Stage 204's opening test completes its flag.
- pytest `test_capi_editor.py`: Tab through `rc_editor_key`; GUT `test_editor_host.gd`: Godot's Tab.
