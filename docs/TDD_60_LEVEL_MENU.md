# TDD 60: The editor's level menu (Stage 155)

Your request: "I want the editor to have some level selection menu/option, to help navigate levels without
opening and closing the editor every single time." The guide is docs/EDITOR.md (keys) and docs/PORTING.md
("Hosting the editor").

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 155.1 | The editor: the levels the host lists, the open level's name, the list in the panel (a click opens), previous and next (wrapping), the unsaved-changes rule (the first ask warns, the second discards), Ctrl+O, Ctrl+Left/Right, Esc | Done |
| 155.2 | C API 1.11: `rc_editor_status.open_requested`; the commands `levels`, `open`, `next_level`, `level_menu`, `open_request`, `load` with a `name` | Done |
| 155.3 | The hosts: tkinter and Godot list `data/levels` (Godot: the repository's folder when it is there, else `res://data`), take a request, load the file, and save back to it | Done |
| 155.4 | Tests, docs, `make gate` | Done |
| 155.5 | The user's interactive sign-off | Done (the user's sign-off 2026-10-25) |

## Decisions

- **The editor never reads files.** As with saving, the host owns file I/O: it lists the levels once and loads the
  one the editor asks for. The editor keeps only the names, so any host (and any engine) can supply them.
- **Unsaved changes are asked twice, not lost.** Opening another level over unsaved changes is refused once with a
  warning in the status line; asking for the same level again discards them.
- **The opened level is the save target.** Each host points its save path at the file it loaded.

## Tests

`rcg_tests` "editor: the level menu -- ..." (the list from the host, the nav row opening the list, a level row's
request, the load naming the level, next wrapping, Ctrl+Left, the unsaved-changes rule); pytest
`test_the_level_menu_handshake` (the C API's status flag and commands over the real levels); GUT
`test_the_level_menu_lists_and_opens_levels` (the Godot host lists the levels and loads the requested one).
