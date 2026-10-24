# TDD 48: The native level editor (Stage 143)

The editor program's second stage (docs/TDD_47_LEVEL_FORMAT_V2.md is the first). The level editor is native,
like the game: `rcg::Editor` holds an `rc-level/2` document and edits it through commands. Its pointer, key and
text input arrive in window pixels, and its whole screen is one draw list (the canvas, the inspector panel, the
status bar). It sits behind the C API (`rc_editor_*`, 1.8), so a host is a painter and an event forwarder. This
stage's host is the Python one, with tkinter (`tests/host_py/rc_editor_tk.py`), which proves the editor needs
no engine. Godot's host is the next stage. The user guide is docs/EDITOR.md.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 143.1 | `rcg::Editor`: the document, undo/redo, the commands (`editor.cpp`) | Done |
| 143.2 | The view and input: pan/zoom, picking, handles, snapping by provenance, the tools, the keys (`editor_view.cpp`) | Done |
| 143.3 | The panel (immediate mode, in the draw list) and live validation per element | Done |
| 143.4 | The C API 1.8 (`rc_editor_*`, the draw pools shared with the game's), the C smoke | Done |
| 143.5 | The Python host: `rc_host.Editor`, the tkinter front end, its tests | Done |
| 143.6 | Docs, `make gates`, the user's interactive sign-off on the tkinter editor | Done (`make gates` ok; the user's sign-off 2026-10-24) |

## Decisions

- **One draw list in screen space.** The editor owns its view (`center`, `zoom`) and emits window pixels: the
  canvas, the panel and the status bar, all as the game's draw commands. Hosts paint at identity with
  no camera, through the same changes protocol as the game's list (a node per element, so moving one element
  repaints one node). Rejected: a per-node screen-space flag (a new field in `rc_draw_list`, and a second
  layer in every engine) and a second list (two painters per host).
- **Every action is a command.** Clicks, keys and panel edits build the same JSON commands a script sends, run
  through one dispatcher, so the tests drive the editor exactly as a user does. Commands are refused whole
  (`{"ok": false, "error": ...}`), never half-applied.
- **The document stays JSON** (not the typed document the program's plan sketched): every edit is a keyed set,
  and the format is open-ended (templates' parameters), so a typed mirror would add a translation and nothing
  else. Geometry is read through small typed helpers.
- **Undo is a copy of the document per edit** (small documents); a drag is one step, a click without a move
  none.
- **Validation per element.** After every change each element is lowered alone and checked as the game checks
  it (the reader, then `validate_spec`, then `build_spec` for the drawing: a portal's image is drawn). The whole
  level is checked last for what only it shows (names). A problem names its element, and the element is drawn
  with a red overlay. A level with problems is not played.
- **Snapping by provenance.** A point near another element's point takes that point's value exactly, so a
  corner is shared by identity (GAME_SPEC 31.4), never by near-equal numbers. Otherwise the grid (10, or whole
  units), or with Alt the pointer itself.
- **Play-testing is the host's ordinary game.** The editor only switches modes. The host parses the document's
  text and runs a game, as for any level, so nothing is special about a level played from the editor.
- **Names identify elements.** Loading names the unnamed ones (but `edges`, which is not edited), and a rename
  carries the buttons that open the renamed door.
- **Templates' `example`**: the parameters a new element starts with (a portal's `d`, a switch's flag), so an
  element placed from the palette is valid at once.

## Tests

- `rcg_tests` "editor: ..." (8 cases): every level loads and saves byte for byte (an `rc-level/1` one migrates);
  commands, rename, set by key and by path, move, duplicate, remove, nine undos and redos, refusals, the save
  handshake; the pointer placing lines, arcs, circles (an inversion refused on a line), the grid, a point shared
  exactly with an off-grid endpoint, right-click cancel, the player's start; dragging the middle handle (a line
  becomes an arc), an endpoint (the via stays), the body (whole grid steps), the player's start, and a click
  that is no edit; keys (nudges, duplicate, delete, undo/redo, the palette, the grid, save, F5 and Esc);
  problems named and blocking play; the panel (an enum cycles, a switch toggles, typed fields, a rename, Esc
  cancels, the level's fields, the place row, a problem's row selects); the draw list (keys, unchanged versions,
  zoom about the pointer).
- `capi_c_smoke`: the editor from C (a command, a line drawn with the pointer, the list and its changes, the
  status, the document parsed as a level, F5).
- `tests/host_py/test_capi_editor.py` (7): every level byte for byte through the C API, commands and undo, the
  pointer and the draw list, a play-test with a `Game` on the document, the save handshake, the tkinter painter
  on every draw command kind (a recording canvas: no display needed) and on a real level, the key names.
