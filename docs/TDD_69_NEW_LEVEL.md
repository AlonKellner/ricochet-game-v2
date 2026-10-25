# TDD 69: New levels in the editor (Stage 165)

Your request (2026-10-25): "Please add an in-app editor option to create a new level, with two ways to do so: Empty
level; Copy of an existing level." The guide is docs/EDITOR.md; the host side docs/PORTING.md.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 165.1 | The editor: the level menu's `+ new empty level` and `+ copy of a level` (then pick the level), Ctrl+N, a name prompt (a free default; Enter creates, Esc cancels), the command `new_level` (names checked, unsaved changes asked twice) | Done |
| 165.2 | A copy through the host: `open_request` returns `as`, the host loads the copied level with that name and `unsaved`; both hosts save under the editor's level name (its folder made) | Done |
| 165.3 | Tests; docs; `make gate` | Done |
| 165.4 | The user's interactive sign-off | Open |

## Decisions

- **Nothing is written until you save.** The new level (or copy) opens unsaved under its new name; the first save
  writes `data/levels/<name>.json`. Leaving it without saving loses it (unsaved changes are asked about twice).
- **The save target follows the level's name.** Each host saves to the levels' folder under the editor's level
  name, so a new level never overwrites the level it was made from.
- **The editor never reads files.** A copy is read by the host, as opening a level is (Stage 155).

## Tests

`rcg_tests` "editor: a new level -- empty or a copy ...": the menu row, the prompt, the created empty level (named,
unsaved), a name in use refused, unsaved changes asked twice, the copy's pick and prompt, the request with `as`, the
copy loaded under its name, Ctrl+N and Esc. pytest `test_a_new_level_and_a_copy`; GUT
`test_a_new_level_and_a_copy_save_under_their_names` (the save targets, nothing written).
