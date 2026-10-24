# The level format (`rc-level/2`, lowering to `rc-level/1`)

A level is a JSON file under `data/levels/<dir>/<name>.json`. It is the source of truth of the level
(Stage 115, docs/TDD_20_ENGINE_AGNOSTIC_DATA.md); every host reads it through the native reader
(`rcg::parse_level`, `native/game/include/rcg/level.hpp`), never through an engine's own parser. The reader is
strict: an unknown key, a missing required key or a value of the wrong type is an error that names its path
(`level.elements[3] (mirror_2).back: one of pass, block expected`).

Since Stage 142 (docs/TDD_47_LEVEL_FORMAT_V2.md) levels are written in **`rc-level/2`**: a list of *elements*
(a wall, a mirror, a door, a button, ...) with names and parameters, and the level's initial flags. The reader
*lowers* an `rc-level/2` level to the `rc-level/1` document described further down (its surfaces with their
ids), which the builder reads; `rc-level/1` is still read as it is. The element kinds are data templates
(`data/elements/<kind>.json`, and a level's own `templates`): see docs/ELEMENTS.md for the kinds and the
template language.

Coordinates: Y down, X right, 1 unit = 1 pixel. Every point is rounded to float32 when read (the game's
vectors are float32); other numbers (a radius, a portal's angle) stay float64. Write numbers as their
shortest round-trip decimals (`rc_tool fmt` writes the canonical spelling; `docs/research/levels/level_fmt.py`
too).

## The level (`rc-level/2`)

| Key | Value | Default |
|---|---|---|
| `schema` | `"rc-level/2"` | required |
| `name`, `bounds`, `gravity`, `room`, `player`, `camera`, `background`, `colliders`, `features` | as in `rc-level/1` (below) | |
| `flags` | `{key: bool \| number \| string}`: the flags the level starts with (a number reads as a float64) | `{}` |
| `elements` | the elements, in build order (below) | required |
| `templates` | `{kind: template}`: the level's own element kinds, added to the builtin ones or replacing one (docs/ELEMENTS.md) | `{}` |
| `objective` | `{"goal": region, "requires": [{"key", "values"}]}`: reach the goal while every condition holds to win (GAME_SPEC 24.6); none: every target hit wins | none |
| `detectors` | `[{"name", "region": region \| "segment": {"a", "b"}, "mode": "while" \| "enter" \| "toggle", "flag", "value", "for"}]`: flags written as the player moves (GAME_SPEC 24.5; flag default `det:<name>`, mode `while`, value `true`, for `player`) | `[]` |
| `objects` | `[{"name", "circle": {"center", "radius"}, "kind": "pullable" \| "static", "face": "wall" \| "mirror" \| "pass", "solid", "label", "gravity", "pops", "carries"}]`: circle bodies (GAME_SPEC 25.5; radius 16, pullable, wall, solid, label `Object` by default); their surfaces take the ids after the level's. Stage 150: `gravity` a factor of the level's (default 1; negative floats up), `pops` (the arrow pops it: flag `obj:<name>:popped`), `carries` another object until it pops | `[]` |
| `carriers` | `[{"name", "line": {"a": [x, y], "b": [x, y]}} \| {"name", "circle": {"center": [x, y], "radius": r}}]`: lines and circles elements are placed on (Stage 149, below) | `[]` |

A region is `{"rect": [x, y, w, h]}` or `{"circle": {"center": [x, y], "radius": r}}`. The objective and the
detectors are not surfaces: the lowering leaves them out.

An element:

| Key | Value |
|---|---|
| `kind` | a builtin kind (`wall`, `mirror`, `door`, ..., docs/ELEMENTS.md), a level template's, `edges` or `raw` |
| `name` | unique in the level; default `<kind>_<n>` (the n-th unnamed one of its kind). Flags derive from it (`door:<name>`) |
| `start`, `end`, `via` | the geometry: a line when the three are collinear, else the arc from start through via to end; `via` omitted: the middle of start and end (a line) |
| `circle` | `{"center": [x, y], "radius": r}` instead: a full circle |
| `straight` | `true`: the exact line through start and end (via for display only) |
| `label` | the node prefix, overriding the kind's (`null`: not drawn) |
| `flip` | `true`: left and right swapped (a mirror facing right) |
| `carrier`, `from`, `to` | instead of the geometry (Stage 149): on the named carrier, from one point to another (both projected onto it); on a circle `via` picks the arc (default: the shorter one), and neither `from` nor `to` is the whole circle |
| *parameters* | the kind's own (`back`, `flag`, `d`, ...), each with a default unless required |

**Carriers** are an authoring construct: the lowering projects an element's `from`, `to` (and `via`) onto its
carrier and writes them as its start, end and via (a line `straight`). Two elements sharing a point share the
same projected value, so a door between two walls on one line meets them by provenance. Move a carrier's points
and every element on it follows. The solver still sees each element's own three points: two arcs on one
declared circle are, to it, two circles through their own points (docs/TDD_53_DECLARED_CARRIERS.md).

Two kinds are the format's own: `{"kind": "edges"}` places the four edge walls where it stands, and
`{"kind": "raw", "surface": {...}}` is an `rc-level/1` surface (without ids) taken as it is -- the escape hatch
for what no template expresses.

**The lowering** lists the surfaces in this order, and gives them consecutive ids in it (a portal two):
1. the room's four walls (top, right, bottom, left; terminal, player-solid, label `Surface`) when `room.built`;
2. every element's surfaces, in order (an element may have several, sharing its geometry);
3. the four edge walls of the bounds (pass, not plannable, not drawn) -- where an `edges` element stands, else here;
4. the pre-transform circles (`pre_inversion` elements), last.

`rc_tool lower <levels> <out>` writes the lowerings (`bench/lowered/<dir>__<name>.json`, `make lowered`: the
oracle's independent builder reads them; the lowered gate fails while they are stale). `rc_tool migrate
[--check] <files>` rewrites `rc-level/1` levels as `rc-level/2`: the room and edge walls implicit where they are
the generated ones, every run of surfaces the first template (by priority) that expands to exactly it, else a
`raw` element; a migration is written only when its lowering is the original byte for byte.

## The lowered level (`rc-level/1`)

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

### A surface

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
`{"kind": "target"}`, `{"kind": "grapple"}`, `{"kind": "pull"}` (Stage 146: journeys at landing, GAME_SPEC 25.6), `{"kind": "reset", "channels": [...]}` or `{"kind": "spawn", "channel": name,
"template": {"surface": surface (no ids, no label), "orientation": "CARRIER_TANGENT" | ..., "mirror_with_side":
bool, "offset": [x, y], "trim": bool}}`; any of them `"deferred": true` (applied when the shot lands).

## Checks

- `rcg_tests` "rc-level/2: every level lowers to its committed lowering, and migrates back from it": each level's
  lowering equals `bench/lowered`, and migrating that lowering gives the level back byte for byte.
- `rcg_levels_canonical`: `rc_tool fmt --check data/levels data/elements` (the canonical spelling; every level
  parses and builds).
- `rcg_tests` "level data: every level built natively equals the game's level dump": the native builder
  against `bench/scenes` (bit for bit; a dump's zero stands for either sign, TDD_20 F3).
- `docs/research/oracle/test_oracle_v2.py::test_level_data_matches_game`: the Python builder
  (`docs/research/oracle/level_json.py`, reading the lowerings) against the same dumps.
- `tests/test_stage115_level_data.gd`: the Godot scenes' nodes agree with their data (until the host makes
  them from it).
- The session replay golden (`bench/sessions`): the game played on the levels built from their data.
