# TDD 49: The level editor in Godot (Stage 144)

The editor program's third stage: the native level editor (docs/TDD_48_NATIVE_EDITOR.md) hosted by Godot.
Like the game, it is one node on the C API alone. `RicochetEditor` forwards Godot's input, paints the editor's
draw list, writes the document when a save is requested, and plays the document with a `RicochetGame` child.
The user guide is docs/EDITOR.md; the host's contract is docs/PORTING.md ("Hosting the editor").

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 144.1 | `apply_draw_list`: the game's layer painter shared (`RicochetGame::sync` calls it) | Done |
| 144.2 | `RicochetEditor`: input (canvas pixels, Ctrl or Command, typed text), painting, resizing, saving, play-testing | Done |
| 144.3 | `RicochetGame.level_json` (a level's text instead of a file); the scene; `--level=` | Done |
| 144.4 | The GUT test (`test_editor_host.gd`, in the final suite), docs, `make gate` | Done |
| 144.5 | The user's interactive sign-off (author a level, save it, `rc_tool fmt --check` it, play it) | Done (2026-10-24) |

## Decisions

- **The editor paints through the game's layers.** The draw list is the same structure, so the code that
  turns it into one `RicochetLayer` per node is shared (`apply_draw_list`). The editor's list is in canvas
  pixels, and the node has no camera, so the layers draw at identity.
- **Pointer coordinates are the canvas's** (`get_global_mouse_position`, the project's stretch applied), and the
  editor's size is the viewport's visible rect, so they match the layers whatever the window size.
- **Ctrl or Command** is the editor's Ctrl. Godot's `is_command_or_control_pressed` is Command alone on a Mac,
  where a Ctrl shortcut would otherwise be lost.
- **Saves go to the source.** A level under `res://data` is `make native`'s copy of the repository's `data/`
  (gitignored, overwritten by the next copy), so the default save target is the original under `data/levels`
  (`save_target()`). `save_path` overrides it; the GUT test saves to `user://`.
- **A play-test is an ordinary game.** `RicochetGame` gains `level_json`, a level's text that replaces
  `level_path`. The editor's layers hide while the child plays, and Esc frees the child (and its camera).
- **No GDScript**: the GDScript budget is unchanged.

## Tests

`engines/godot/tests/test_editor_host.gd` (6, in the final Godot suite, `tools/gut_ledger_check.py`): the editor
painted as layers and the document as on disk; commands edit it and add a layer; Godot's keys reach the
editor (the tool, the palette); Ctrl+S writes the document and clears the dirty flag; the default save target is
the repository's file, not the copy; F5 plays the level with a `RicochetGame` child that draws, and Esc returns.
