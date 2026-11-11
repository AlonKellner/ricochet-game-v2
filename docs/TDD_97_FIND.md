# TDD 97: Find anything -- Ctrl+F (Stage 219)

Your request (2026-11-11): "any other editor improvements like completion and ease of use". The plan you approved
(2026-11-11): Stages 213-220; this one: an element or item reached by name, and a problem that selects what it names.

The rules are docs/EDITOR.md (Ctrl+F, Problems). The game does not change.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 219.1 | Ctrl+F: a find field at the panel's top, what the level holds ranked by name (its letters lit), then by kind or a flag it touches | Done |
| 219.2 | Up/Down pick; Enter or a click selects it and centres the view on it; Esc clears, then closes; a click elsewhere leaves it | Done |
| 219.3 | A level problem's click selects the item its path names (`level.detectors[0]`, `level.objects[1]`, `level.objective`, ...) | Done |
| 219.4 | The user's interactive sign-off | Done ("approved, proceed", 2026-11-11) |

## Decisions

**What it finds.** Elements, detectors, objects, carriers, the goal and the player's start -- what the selection can
hold. The query is the level search's soft match (Stage 167) on the name; a name that does not match may still match
its kind (`detector`) or a flag it writes or reads (Stage 217's index: `lamp` finds the button writing it), ranked
below the names and saying which (`button (lamp)`). Empty, everything in the document's order.

**Shown, not just selected.** Enter or a click selects it and centres the view on its box (the selection's box, Stage
175), the zoom kept: what was found is on screen.

**Ctrl+F, not F.** F frames the level (Stage 155); Ctrl+F was free in both hosts (Godot and tk forward it as a key with
Ctrl, no text).

**Problems name items.** A problem the level has (not an element's) was a dead row; its message carries the path the
reader names (`level.objects[0] ...`), which now selects that item.

## Tests

- `rcg_tests`, `test_editor.cpp` "Ctrl+F finds": the found rows; `gat` -> the door, Enter selects and centres; `lamp`
  -> the button by its flag; `detec` -> the detector by its kind, a click selects it; Down, Enter; Esc clears then
  closes; a broken object's problem selects the object.
