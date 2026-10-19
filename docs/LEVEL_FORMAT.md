# The level format (`rc-level/1`)

A level is a JSON file under `data/levels/<dir>/<name>.json`. It is the source of truth of the level
(Stage 115, docs/TDD_20_ENGINE_AGNOSTIC_DATA.md); every host reads it through the native reader
(`rcg::parse_level`, `native/game/include/rcg/level.hpp`; in Godot `RcgLevel.parse` / `LevelData`), never
through an engine's own parser. The reader is strict: an unknown key, a missing required key or a value of
the wrong type is an error that names its path (`level.surfaces[3].left.effect.type`).

Coordinates: Y down, X right, 1 unit = 1 pixel. Every point is rounded to float32 when read (the game's
vectors are float32); other numbers (a radius, a portal's angle) stay float64. Write numbers as their
shortest round-trip decimals (`docs/research/levels/level_fmt.py` writes the canonical spelling).

## The level

| Key | Value | Default |
|---|---|---|
| `schema` | `"rc-level/1"` | required |
| `name` | the level's path under `data/levels` without `.json` | `""` |
| `bounds` | `[x, y, w, h]`: the level's edge (the arrow wraps there, the ∞ entry is clicked there) | `[0, 0, 1920, 1080]` |
| `gravity` | `[gx, gy]` (px/s²; the player's controller) | `[0, 0]` |
| `room` | `{"rect": [x, y, w, h], "built": bool}`: the room (its walls are listed in `surfaces` when built; the sweeps bound their positions by it) | `[560, 240, 800, 600]`, built |
| `player` | `{"start": [x, y]}` or `null` | `null` |
| `camera` | `{"start": [x, y], "smoothing": bool, "smoothing_speed": number}` or `null` (no camera: the view is the bounds) | `null` |
| `background` | `{"rect": [x, y, w, h], "color": [r, g, b, a]}` or `null` | `null` |
| `colliders` | `[{"a": [x, y], "b": [x, y]}]`: segments only the player collides with (floors) | `[]` |
| `features` | `{"game", "preview", "arrow", "hud", "cursor"}`: which parts of the game the level runs (test levels leave some out: without `arrow` a shot lands at once) | all `true` |
| `surfaces` | the surfaces in build order (below) | required |

## A surface

Every surface the level builds is listed, in the order it is built, with the ids it takes: the room's walls,
the level's surfaces, the four edge walls (pass, not plannable, not drawn) and the pre-transform circles.

| Key | Value |
|---|---|
| `ids` | `[id]`; a portal `[source, image]` (the image is built: the source mapped by the portal's motion) |
| `label` | the node prefix (`"Mirror"`, `"Surface"`, ...); `null`: not drawn (the edge walls) |
| `start`, `end`, `via` | three points: a line when collinear, else the arc from start through via to end |
| `circle` | `{"center": [x, y], "radius": r}` instead of the three points: a full circle |
| `straight` | `true`: the exact line through start and end (via only for display) |
| `pre_transform` | `true`: a pre-transform inversion circle (GAME_SPEC 9.5; `circle` only, no sides) |
| `left`, `right` | a side (below) or `null` (the pass) |
| `resolver` | `{"key": flag, "table": [[value, {"left": side, "right": side}], ...]}`: categorical sides (GAME_SPEC 9.4; `"DEFAULT"` matches an unset flag); the order is the table's |
| `presence` | `[{"key": flag, "values": [...]}]`: the surface exists while every condition holds (GAME_SPEC 9.6); a surface that spawns gets its channels' host conditions when built |
| `player_solid` | `true`: the player collides with it |

A side: `{"effect": effect, "interactive": bool, "hits": [hit, ...]}` (`interactive`: plannable).

An effect (GAME_SPEC 10): `{"type": "pass" | "terminal" | "reflection" | "inversion" | "portal" |
"line_normal_projection" | "circle_normal_projection" | "line_directional_projection" |
"circle_directional_projection", "back": bool, "normal": [x, y], "theta": number, "d": [x, y]}` --
`back` for the projections, `normal` for the directional ones (normalized when built), `theta` (radians)
and `d` for a portal (the motion `z -> e^{i theta} z + d`; both sides the same).

A hit effect (GAME_SPEC 10.6): `{"kind": "state", "key": flag, "op": "set" | "toggle", "value": v}`,
`{"kind": "target"}`, `{"kind": "reset", "channels": [...]}` or `{"kind": "spawn", "channel": name,
"template": {"surface": surface (no ids, no label), "orientation": "CARRIER_TANGENT" | ..., "mirror_with_side":
bool, "offset": [x, y], "trim": bool}}`; any of them `"deferred": true` (applied when the shot lands).

## Checks

- `rcg_tests` "level data: every level built natively equals the game's level dump": the native builder
  against `bench/scenes` (bit for bit; a dump's zero stands for either sign, TDD_20 F3).
- `docs/research/oracle/test_oracle_v2.py::test_level_data_matches_game`: the Python builder
  (`docs/research/oracle/level_json.py`) against the same dumps.
- `tests/test_stage115_level_data.gd`: the Godot scenes' nodes agree with their data (until the host makes
  them from it).
- The session replay golden (`bench/sessions`): the game played on the levels built from their data.
