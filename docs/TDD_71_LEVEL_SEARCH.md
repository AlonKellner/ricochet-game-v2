# TDD 71: A searchable level list (Stage 167)

Your request (2026-10-25): "The editor has too many levels in the drop down list, let's make the drop down list a
searchable list, where I can type anything and soft text matches pop to the top of the list, with the matching
characters highlighted."

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 167.1 | The soft match: the query's characters in order, case-insensitive; a run scores most, then a word's start (after `/`, `_`, `-`), then an earlier start; the best start wins; matches first by score, the rest after in their order | Done |
| 167.2 | The list: a search field focused when it opens; the matched characters lit (the name laid out a character per cell, the matches tinted); Up/Down pick, Enter opens (copy mode: names the copy), Esc clears then closes; a click elsewhere leaves the search, opening nothing | Done |
| 167.3 | Tests; docs; `make gate` | Done |
| 167.4 | The user's interactive sign-off | Open |

## Decisions

- **The editor lays the characters out.** Hosts draw text in their own fonts, so the editor cannot know where a
  character of a proportional string falls. A matching name is drawn a character per fixed cell, so its matched
  characters can be tinted exactly; a name without a match is drawn as before.
- **A typed word wins.** A run of the query's characters scores more than two word starts, so `gr` ranks
  `grapple_gap` above `ghost_room`.

## Tests

`rcg_tests` "editor: the level list is searchable ...": the ranking and the marks (a run, case-insensitive, several
matches above the rest), typing into the open list, Down and Enter opening the second match, Esc clearing then
closing.
