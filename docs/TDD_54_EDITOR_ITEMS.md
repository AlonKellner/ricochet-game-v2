# TDD 54: The goal, detectors and objects edited on the canvas (Stage 148)

A follow-up to Stages 145-146, from the user's review of the editor ("why is the goal not editable within the
editor?"). The goal, detectors and objects are level-wide keys, not elements. Stages 145-146 gave each a
placement tool and a JSON field in the level's panel, but none of the select, drag and handle machinery the
elements have. This stage gives them all of it, and fixes what a visual review of the editor found.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 148.1 | Items: selected by a click inside (elements above them win), highlighted; handles (rectangle corners, a circle's center and radius, a segment's ends) and body drags; the panel's typed rows; Delete, Ctrl+D, arrows; the commands with `item` | Done |
| 148.2 | The visual review (the draw list rendered to images): the view framed on every resize until the user moves it (the tkinter host showed half a level); spawners no longer dashed like doors; a radius dragged once rounded (it was rounded twice); empty level fields read `(none)` | Done |
| 148.3 | The palette: every kind as a chip in the panel (one click picks it, and the line or circle tool); the panel scrolls with the wheel | Done |
| 148.4 | Tests, docs, `make gate` | Done |
| 148.4b | After Stage 149: an element's kind picked from the palette (it cycled through all 27); the Godot host resets the canvas transform after a play-test (a level with a camera left the editor's view moved) | Done |
| 148.5 | The user's interactive sign-off | Done (2026-10-24) |

## Decisions

- **Items beside elements.** The selection stays an element's name, and an item (`"goal"`, `"detector:<name>"`,
  `"object:<name>"`) is a second selection. Choosing one clears the other. An item's handles carry the JSON
  pointer of what they move, so one drag routine reshapes a goal, a detector's region and an object alike.
- **An element above an item wins the click,** since elements are thin lines inside large regions. A click
  inside a region with no element under the pointer selects the smallest item: an object, then a detector, then
  the goal.
- **Renaming a detector carries its flag:** the detector's own explicit `det:<old>`, the elements' `flag`
  parameters and the goal's `requires` keys become `det:<new>`. An object has nothing that refers to it.
- **The view frames the level on every resize until the user pans or zooms.** It used to frame only for its
  default size, so a host that resized without asking for a frame showed the level cut off.

## Tests

`rcg_tests` "editor: the goal, detectors and objects are selected, dragged and edited like elements (Stage 148)":
a click selects the goal; its corner and its body drag; the shape becomes a circle whose radius drags; a
detector's mode cycles and its rename reaches the door and the goal; an object's kind cycles, Ctrl+D duplicates
it, Delete removes the copy, an arrow moves it; an element above an item wins the click; the goal removed and
restored by undo; refusals.
