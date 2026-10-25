# The level editor

The level editor edits `rc-level/2` levels (docs/LEVEL_FORMAT.md, docs/ELEMENTS.md) visually. It is native
(`rcg::Editor`, `native/game/include/rcg/editor.hpp`), behind the C API (`rc_editor_*`, `rc_api.h` 1.8), like
the game. A host only paints the editor's draw list and forwards the pointer, the keys and the text. Two hosts
run it:

- **The Python host**, with tkinter (Python 3 with Tk; on macOS `/usr/local/bin/python3` or the python.org build):

  ```bash
  make core                                     # the C API's shared library
  python3 tests/host_py/rc_editor_tk.py data/levels/test_levels/flag_switches.json
  python3 tests/host_py/rc_editor_tk.py         # a new level (the first save asks where)
  ```

- **Godot** (`RicochetEditor`, `native/hosts/godot/ricochet_editor.cpp`; the scene
  `engines/godot/scenes/editor/editor.tscn`):

  ```bash
  make native                                   # the extension and the data copy
  godot --path engines/godot res://scenes/editor/editor.tscn -- --level=res://data/levels/test_levels/flag_switches.json
  godot --path engines/godot res://scenes/editor/editor.tscn    # a new level
  ```

  In the Godot editor, set the node's `level_path` (and `save_path`, optional) and run the scene (F6). A level
  under `res://data` is `make native`'s copy of the repository's `data/`, so a save writes the original
  (`data/levels/...`), never the copy. A new level saves to `data/levels/untitled.json`. Play-testing runs a
  `RicochetGame` child on the document.

## The window

- **The canvas**, left: the level, with the grid (10 units, and 50), the bounds (dashed) and the room's walls.
  Every element is drawn in its side's colour as the game colours it: red walls, blue mirrors, purple
  inversions, cyan portals, orange projections, gray passes, yellow buttons and togglers. A short tick from the
  middle of a surface points at its left side, a second one at its right side when that side does something.
  A door, present only while its flag is unset, is dashed. An element with a problem has a red dashed overlay.
  The player's start is the green circle. Each element's name is written beside it.
- **The panel**, right: the buttons (undo, redo, save, play), the tool, what the tools place (with its
  description), then the selected element's fields or, with nothing selected, the level's fields. Last come the
  problems.
- **The status bar**, bottom: the tool, the kind placed, the grid, the problems, unsaved changes, and the hint
  of whatever is under the pointer (a field's meaning, a refused action).

## Tools and keys

| Key | What |
|---|---|
| V | select: click an element, the goal, a detector or an object to select it; drag it to move it, drag a handle to reshape it |
| L | line: two clicks, start and end (a line; its middle handle bends it into an arc) |
| A | arc: three clicks, start, end, and a point on the arc between them |
| C | circle: two clicks, the center and a point on the circle |
| P | the player's start: click where the player starts |
| O | the goal: two corners (a rectangle; a circle through the panel's `goal`) |
| D | a detector: two corners (a rectangle, mode `enter`, flag `det:<name>`) |
| B | an object: one click (a pullable circle of radius 16; edit it in the panel's `objects`) |
| K, Shift+K | a carrier: a line by two points, a circle by its center and a point on it (drawn faint and dashed across the canvas) |
| Tab / Shift+Tab, 1-9, or a kind in the panel's palette | what the line, arc and circle tools place (every kind, the level's own last; clicking a kind also picks the line tool, the circle tool for a circle-only kind) |
| Delete, Backspace | remove the selected element |
| Ctrl+D | duplicate it (offset by 20, 20) |
| arrows (Shift: x10) | move it by a grid step |
| Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y | undo, redo (a drag is one step) |
| Ctrl+S | save |
| Ctrl+O, the panel's level name | the level list: a click opens a level (unsaved changes: the first click warns, the second discards them); Esc closes it |
| Ctrl+Left / Ctrl+Right, the panel's `<` / `>` | the previous / next level in the list |
| F5 | play the level (refused while it has problems); Esc returns |
| G | the grid on or off (off: whole units) |
| F | frame the level's bounds (the view frames them on every resize until you pan or zoom) |
| wheel | zoom about the pointer (over the panel: scroll it) |
| middle drag, Space+drag | pan |
| right click, Esc | cancel a placement; else deselect |

On a Mac, Command works as Ctrl.

**Snapping.** A point you place or drag goes, in this order:
1. Onto another element's point (an endpoint, a via, a circle's center, the player's start) within 8 pixels.
   It takes that point's value exactly, so two walls meeting at a corner share their endpoint by provenance
   (GAME_SPEC 31.4), never by a near-equal number.
2. Onto the grid (10 units; with G off, whole units).

With Alt held, the point stays exactly under the pointer.

**Handles** (the selected element): its start and end, its via (or, on a line, its middle, which bends the
line when dragged), a circle's center and radius, and the player's start. When an endpoint moves, the via
stays where it is (GAME_SPEC 19.2).

**Motion.** An element's `motion` field takes its keys (`{"keys": [[phase, dx, dy, angle], ...]}`). The element is
drawn faint and dashed where each key puts it, and the level's `cycle` field sets the period. Play-test (F5) to
see it move; the wheel sets the cycle there.

**Carriers.** A point placed near a carrier snaps onto it, a grid step along it. When both clicks of the line
tool (or the three of the arc tool on a circle) land on one carrier, the element is placed on it (`carrier`,
`from`, `to`), and its handles move along it. A carrier is selected by a click on its line or circle. Its handles
are its defining points (a line's two, a circle's center and radius), and dragging them moves every element on
it. Renaming it carries the elements, and deleting it frees them, each keeping its place as its own geometry. Its `exact` row
(Stage 154) snaps its elements onto points exactly on it (GAME_SPEC 19.1).

**The goal, detectors and objects** are selected like elements: a click inside one (an element's line above it
wins the click). A rectangle has corner handles, a circle a center and a radius handle, a detector's segment its
two ends, and the whole thing drags. The panel shows its own fields:
- the goal: its shape (click: a rectangle becomes the circle in it, and back) and its `requires`;
- a detector: its name, `for` and `mode` (they cycle), flag, value and shape;
- an object: its name, `kind` and `face` (they cycle), solidity, center, radius and label.

Renaming a detector carries `det:<name>` to the doors and the goal's conditions that use it. Delete removes the
selected item, Ctrl+D duplicates a detector or an object, and the arrows move it.

## The panel

Click a field to edit it:
- **An enum** (a mirror's `back`, a door's `closed`) cycles to its next value; Shift+click goes back.
- **A switch** (`flip`, a bool parameter, the room's `built`) toggles.
- **The kind**: click it, then a kind in the palette, and the element becomes one (Shift+click: the next kind;
  Esc: no change). The element keeps its parameters, and the problems show what the new kind does not accept.
- **Any other field** takes typed text: type, then Enter to apply or Esc to cancel. The text is read as JSON
  (`[700, 300]`, `true`, `{"door:gate": true}`), and a bare word is a string (`gate`). Empty text removes the
  field: a parameter returns to its default, a label to the kind's.

A parameter left at its default shows the default in parentheses. Renaming an element carries the buttons that
open it (their `opens`). With nothing selected, the panel edits the level: its name, bounds, gravity, room,
player's start, initial flags, goal, the goal's conditions (`requires`), detectors and objects. The goal (green),
the detectors (yellow) and the objects (orange: pullable; gray: static) are drawn on the canvas.

**Problems.** Every element is checked as the game checks it after every change (the reader's errors, then the
builder's, `validate_spec`). Each problem names its element. Clicking a problem selects that element. A level
with problems cannot be played.

## Play-testing and saving

F5 (or the panel's play) switches the editor to play mode: the host plays the document as an ordinary game,
starting at the player's start. Esc returns to the editor with the document as it was. Nothing is edited while
playing.

Ctrl+S asks the host to save. The host writes the document (the canonical spelling, `rc_tool fmt`'s) to the
level's file, or asks where for a new level, then tells the editor it saved. A level the editor saves passes
`rc_tool fmt --check`.

## Commands

Every action is a command, a JSON object, which is also how scripts and tests drive the editor
(`rc_editor_command`, `rcg::Editor::command`). The result is `{"ok": true, ...}` or
`{"ok": false, "error": "..."}`; a refused command changes nothing.

| Command | What |
|---|---|
| `{"op": "new"}`, `{"op": "load", "document": {...}}` / `"text": "..."` | a new level; a level (`rc-level/1` is migrated) |
| `{"op": "add", "element": {...}}` | add an element (named `<kind>_<n>` if it has no name); `{"name": ...}` |
| `{"op": "remove", "name": n}` | remove it |
| `{"op": "set", "name": n, "key": k, "value": v}` | set an element's key (`null`: remove it; `"name"`: a rename) |
| `{"op": "set", "path": "/gravity", "value": v}` | set a value in the document by JSON pointer (`null`: remove it) |
| `{"op": "rename", "name": n, "to": m}` | rename (the buttons with `opens: n` follow) |
| `{"op": "move", "name": n, "handle": "start" \| "end" \| "via" \| "center" \| "radius", "to": v}` | move a handle |
| `{"op": "move", "name": n, "handle": "body", "by": [dx, dy]}` | move the element |
| `{"op": "duplicate", "name": n, "by": [dx, dy]?}` | duplicate it; `{"name": ...}` |
| `{"op": "undo"}`, `{"op": "redo"}` | undo, redo |
| `{"op": "select", "name": n \| null}` | select |
| `{"op": "select" \| "remove" \| "duplicate", "item": "goal" \| "detector:<name>" \| "object:<name>" \| "carrier:<name>"}` | an item: select it, remove it (a carrier's elements freed), duplicate it (`{"item": ...}`) |
| `{"op": "move", "item": i, "handle": "body", "by": [dx, dy]}`, `{"op": "move", "item": i, "handle": "corner0".."corner3" \| "center" \| "radius" \| "a" \| "b", "to": v}` | move an item, or a handle of it |
| `{"op": "rename", "item": "detector:<name>" \| "object:<name>" \| "carrier:<name>", "to": m}` | rename it (a detector's `det:<name>`, a carrier's elements follow) |
| `{"op": "tool", "tool": "select" \| "line" \| "arc" \| "circle" \| "player" \| "goal" \| "detector" \| "object" \| "carrier_line" \| "carrier_circle"}`, `{"op": "kind", "kind": k}` | the tool; what it places |
| `{"op": "snap", "grid": bool}`, `{"op": "view", "center": [x, y], "zoom": z}`, `{"op": "frame"}` | the grid; the view |
| `{"op": "validate"}` | `{"errors": [{"element", "name", "message"}]}` |
| `{"op": "save"}`, `{"op": "saved"}`, `{"op": "save_cancelled"}` | request a save; the host saved; the host could not |
| `{"op": "play"}`, `{"op": "stop"}` | play mode (`{"from": [x, y]}`), edit mode |
| `{"op": "document"}`, `{"op": "lower"}` | the document (`"document"`, `"text"`); its `rc-level/1` lowering (`"level"`) |
| `{"op": "state"}`, `{"op": "panel"}` | the tool, kind, selection, mode, undo depth, focus, hint...; the panel's rows |
| `{"op": "pointer", "kind": "down" \| "up" \| "move", "x", "y", "button": "left" \| "right" \| "middle", "mods": ["shift", "ctrl", "alt"]}` | input as a host forwards it |
| `{"op": "key", "key": "L" \| "escape" \| "f5" \| code, "mods": [...], "down": bool?}` | a key (a press and a release without `down`) |
| `{"op": "text", "text": s}`, `{"op": "scroll", "dy": d}`, `{"op": "resize", "w": w, "h": h}` | typed text; the wheel; the window |

For example, a door, a button that opens it, and a save:

```json
{"op": "add", "element": {"kind": "door", "name": "gate", "start": [900, 300], "end": [900, 500]}}
{"op": "add", "element": {"kind": "button", "opens": "gate", "start": [600, 200], "end": [700, 200]}}
{"op": "save"}
```

## Hosting the editor

docs/PORTING.md ("Hosting the editor") describes what a host does. The tkinter host
(`tests/host_py/rc_editor_tk.py`, about 300 lines) is the reference.
