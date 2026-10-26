# TDD 74: A level renamed in the editor (Stage 170)

Your request (2026-10-26): "Add a rename option from within the editor." The guide is docs/EDITOR.md; the host side
docs/PORTING.md.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 170.1 | The editor: the command `rename_level` (names checked as a new level's, a name in use refused, the undo stack keeps the new name), F2, the level menu's `~ rename this level`, the level's `name` field; a prompt with the level's name (Enter renames, Esc cancels); `state`'s `renamed_from`, `saved` with `name` | Done |
| 170.2 | The hosts: after the save under the new name, Godot and the Python host remove the old file (only when the save went to the levels' folder under the level's name); an unnamed level saved there is named by its file | Done |
| 170.3 | Tests; docs; `make gate` | Done |
| 170.4 | The user's interactive sign-off | Done (the user's sign-off 2026-10-26) |

## Decisions

- **A rename is written at once.** Enter saves the level under its new name, its unsaved changes with it, and the
  host removes the old file -- as a file manager renames. The level list then names the files on disk. (Stage 165's
  new level waits for a save because nothing exists yet; a renamed level already has a file.)
- **The editor never touches files.** It asks for the save and names the old file (`renamed_from`); the host writes
  the new one and removes the old. A failed save keeps the rename waiting: the next save removes the file. Renamed
  twice before a save, the file removed is the one on disk (the first name); renamed back to it, nothing is removed.
- **Only the level's own file is removed.** A host that saves elsewhere (Godot's `save_path`, a file dialog) removes
  nothing.
- **Other files' names are not followed.** A scene's `level_path`, a session script and the lowerings
  (`bench/lowered`) keep the old name; renaming a repository level means renaming those too (the gates say which).

## Tests

`rcg_tests` "editor: a level renamed ...": the menu row and its prompt, the rename (level, document, the list, the
save asked, `renamed_from`), undo keeping the name, `saved` clearing it, a name in use and a bad name refused, the
same name a no-op, renamed twice and back, F2 and Esc, the `name` field, refused while playing, an unnamed level
named, `saved` naming an unnamed level. pytest `test_a_level_renamed` (F2 through the C API); GUT
`test_a_rename_saves_at_once_and_removes_only_the_levels_own_file` (saved at once, the repository's file kept when
the save goes elsewhere).
