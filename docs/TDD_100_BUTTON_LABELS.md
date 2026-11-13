# TDD 100: The editor's buttons use their width (Stage 222)

Your request (2026-11-13): "the editor menu buttons all have the start of the text aligned to the center of the button,
but that means that the left half of all buttons is unused space. Please align the text start to the button left (with
a small margin), such that the full button area is used efficiently."

The rules are docs/EDITOR.md (the status bar). The game does not change.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 222.1 | A button's label starts 8 px into its cell (6 inside its fill), left-aligned: the palette's and the tools' chips, the actions, undo/redo/save/play, the level's name | Done |
| 222.2 | The level menu's arrows in the middle of their buttons; the level's name cut to its button | Done |
| 222.3 | A tool's label cut where its chip ends, whole on hover (as the palette's chips since Stage 220) | Done |
| 222.4 | The user's interactive sign-off | Done ("Looks perfect, approved, proceed", 2026-11-13) |

## Decisions

**Why the middle.** The panel put each label at its button's middle with the alignment "centre", but the hosts draw a
label from its point (docs/PORTING.md, `TEXT`: Godot's `draw_string` with no width ignores the alignment), so every
label started at the middle. The labels now start at the left with the alignment "left", which every host draws the
same; nothing in the hosts changes.

**Cut by width.** With the start at the left the label has the whole button: the chips' cut (Stage 220, by their width)
now fits inside them. The tools' labels with their keys ("circle carrier  shift K") were never cut and ran into the
next chip; they are cut the same way, whole in the status bar on hover. The level's name is cut from the front to its
button's width (its end, the file's name, is what tells levels apart).

**The arrows.** The level menu's `<` and `>` are a character each in narrow buttons: put in the middle of their
buttons (half a character back from the middle point), not at the left.

## Tests

`rcg_tests` "editor: a button's label starts at its left ... (Stage 222)": undo, play, two palette chips, a tool and
the level's name each left-aligned 6 px inside the button under it; the arrows in the middle of theirs; the circle
carrier's tool cut to "circle carrier." and whole in the status bar on hover. Before the change 15 of its 16 checks
failed.
