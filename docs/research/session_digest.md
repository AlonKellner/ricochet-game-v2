# The session replay golden

Stage 115 (docs/TDD_20_ENGINE_AGNOSTIC_DATA.md). The game played through scripted sessions, one record of
its observable state per step: `bench/sessions/<session>.jsonl`. It was recorded from the GDScript game and
was the oracle of the engine-agnostic port: the native session (`rcg::Session`), `rc_tool replay` and the C
API hosts reproduce every record byte for byte. Since Stage 119 (the GDScript game deleted) the native game
records it (`rc_tool replay`, `make sessions`); "The run" below describes the GDScript recorder, whose
semantics the native replay keeps. A changed record is a behaviour change (the judge's report, the user's
approval, then `make sessions`).

| Piece | Path |
|---|---|
| Session scripts (input data, committed) | `data/sessions/<level>__<name>.json` |
| Their generator (run once per new level) | `docs/research/sessions/gen_sessions.py` |
| Recorder | `rc_tool replay` (the C API; Stage 119; until then the GDScript game's `record_sessions.gd`) |
| Record encoder | `rcg/digest.hpp` (`rcg::digest`; the GDScript `SessionDigest` until Stage 119) |
| Hand-written scripts (the GUT ports, Stage 119) | `docs/research/sessions/ported_sessions.py` (`*__ported_*`) |
| Golden | `bench/sessions/<session>.jsonl` |
| Judge | `docs/research/session_judge.py <ref dir> <candidate dir>` |
| Targets | `make sessions` (re-record), `make sessions-check` (also ctest `capi_sessions_*`, `rcg_sessions_*`) |

## Session scripts (`rc-session/1`)

```json
{"schema": "rc-session/1", "level": "test_levels/hit_effects", "kind": "teleport",
 "switches": {"infinity": true, "visibility": true},
 "steps": [{"op": "teleport", "at": [x, y]}, {"op": "cursor", "at": [x, y]},
           {"op": "click", "button": "left|right"}, {"op": "key", "action": "<action>"},
           {"op": "fire", "speedup_at": 20}, {"op": "hold", "actions": ["move_right"], "ticks": 45}]}
```

- `level`: the level's path under `scenes/` without the extension (later: under `data/levels/`).
- `kind`: `teleport` (the player only moves by `teleport`) or `kinematic` (`hold` steps move it by the
  player controller). Both are gated, natively too since Stage 117.
- `switches`: optional overrides of the feature switches (default: the game's defaults).
- Coordinates are multiples of 1/4 px (exact in every decimal parser; the recorder checks).
- Actions: `undo` (Z), `reset` (R), `clear_plan` (C), `cycle_display` (Tab), `toggle_gravity` (G),
  `toggle_visibility` (V), `any` (a key that is no movement: speeds a flying arrow up).
- `fire`: the fire action, then the whole flight; `speedup_at: k` presses `any` after the k-th flight frame.
- `hold`: the movement actions held for `ticks` physics ticks (the player's physics runs only inside a hold;
  the native controller since Stage 117). The up action registers as just pressed on the second tick (the
  engine's input timing): a jump with gravity starts there.

## The run

- A fresh level per session: the surface id counter reset, the switches applied, the level instantiated.
- The cursor and the player do not move by themselves (the cursor's processing and the player's physics are
  off outside `hold`); `teleport` / `cursor` set their positions. The cursor starts at (0, 0).
- The visibility region is computed in the frame (not on a worker thread).
- Input goes through the engine's input path (press and release), one event pair per step.
- After every step the game settles: 3 frames, then frames until two consecutive records are equal (at most
  12; the recorder fails otherwise). The record is the settled state. Frames are 1/60 s (`--fixed-fps 60`).
- A flight samples the arrow (position, direction) after every frame, from the first frame after the fire.
- The initial record (`"op": "start"`, `"i": 0`) is the settled level before any step.

## The record

One JSON object per line, encoded canonically (`SessionDigest.encode`):

- object keys sorted (by their string), no whitespace;
- `null`, `true`, `false`; integers in decimal;
- every float as the string `"f"` + the 16 hex digits of its IEEE-754 float64 bytes in little-endian order
  (a Vector2 component is its float32 value widened; ints and floats are never mixed up);
- Vector2 as `[x, y]`, Color as `[r, g, b, a]` (float32 components widened);
- strings with `"`, `\`, `\n`, `\t`, `\r` escaped by name, other control characters as `\u00XX`, everything
  else as UTF-8.

`sha(v)` is the lowercase hex sha256 of `encode(v)` (UTF-8). Fields (`full` recordings keep the sha'd parts
whole: `--full`):

| Field | Value |
|---|---|
| `i`, `op` | the step's index (0: start) and op; `action` for a key step |
| `player`, `cursor` | positions (Vector2) |
| `facing` | the player's facing angle (float32 widened): the preview's first step's start direction, else toward the cursor |
| `camera` | the camera's position when the level has one and no arrow flies, else `null` |
| `display_mode` | 0 merged, 1 physical, 2 planned |
| `gravity` | the level's gravity (Vector2) |
| `infinity`, `visibility_on` | the feature switches |
| `flying` | an arrow in flight |
| `plan` | per entry `[surface id, side (0 left, 1 right), click (Vector2; ±inf: unknown), piece index, span (6 floats) or null]` |
| `hud` | the plan HUD's text, `null` without a HUD |
| `flags` | the game state's flags (bool, float, string or int values) |
| `targets` | the targets hit, sorted ids |
| `checkpoints` | the undo stack's depth |
| `pole` | the frame's image of infinity after the plan (Vector2) or `null` |
| `hover` | what the cursor is over: `{}`; `{"infinity": true}`; `{"surface", "side", "piece"}`; `null` in flight |
| `world` | `{"ids": [...], "generations": {...}, "defs": sha(scene defs), "pieces": sha({"pieces": [[id, index, start, end, via, span]...], "crossings": [...]})}` |
| `preview_n` | `[physical steps, planned steps, merged steps, planned cursor index]` |
| `preview` | sha of `{"physical", "planned"}` (`{"steps", "cursor_index", "truncated"}`), `"merged"` steps, `"hits"` (the physical hits by surface), `"final_flags"`, `"cursor_point_agrees"`, `"events"` (`[physical, planned]`, each `[kind, surface id, applied]`) and `"stops"` (the truncation markers) |
| `overlays` | sha of the surface nodes sorted by (surface id, piece): `[id, piece, hover overlays, plan overlays, plan entry numbers]`, an overlay `[side, has_incoming, has_outgoing, outgoing_side, incoming colour, outgoing colour, gradient colour, piece]` |
| `edge` | the edge overlay: `{"hovered", "point", "marks": [{"index", "point", "on_point", "valid"}]}` |
| `visibility` | `{"region", "supported"}`; supported: `"n": [fill points, fill indices, whiskers]`, `"fill": sha({"points", "indices"})`, `"whiskers": sha(polylines)` |
| `spawn_n`, `spawn` | the spawn preview's `[placed, vanishing]` counts, sha of `{"placed": [{"channel", "generation", "spec"}], "vanishing": [ids]}` (a spec as its fields) |
| `draw` | (Stage 118) sha of the draw list: what the level's nodes draw, each `[key, z_index, position, modulate, calls]` sorted by (z_index, encoded key), a key `["surface", id, piece]`, `["edge"]`, `["visibility"]`, `["spawn"]`, `["path"]`, `["arrow"]` (in flight only), `["cursor"]`, `["player"]`, `["background"]` or `["hud"]`; the calls as the CanvasItem takes them (`scripts/visual/draw_rec.gd`, `rcg/render.hpp`): `["line", from, to, color, width]`, `["arc", center, radius, start, end, point_count, color, width]`, `["polygon", points, colors]`, `["colored_polygon", points, color]`, `["circle", center, radius, color]`, `["rect", position, size, color]`, `["polyline" \| "multiline", points, color, width]`, `["dashed_line", from, to, color, width, dash]`, `["text", position, text, alignment, font_size, color]`, `["triangles", points, indices, color]`, the player's `["polygon2d", polygon, color, rotation]`, the HUD's `["label", text]`; `--full`: the list itself |
| `flight` | fire steps: `{"frames", "stream": sha([[position, direction] per frame]), "end"}` |
| `trajectory` | hold steps: the player's position after every tick, whole |

A path step (TraceStep) is `[start, end, via, is_arc, type, surface id, hit side, hit on segment,
after portal, ghost]`. Solver internals that are not game state (frame ids, error estimates, precision bits,
timings) are not recorded.

## Coverage (48 sessions, 18 levels with a GameManager)

`tour` (hover both sides of every interactive surface, plan clicks on pieces, the display / visibility /
gravity toggles, fire with a speed-up, undo, remove and clear clicks, reset, the infinity entry and a shot
through it), `shots` (the player at a free point, a shot at every interactive surface in turn so the hit
effects accumulate, undo, reset, shots along one-entry plans) and `walk` (kinematic: main,
open_room_playable, plannable_inversion, inversion_grid). Stage 119 adds 8 `ported` sessions (the GUT
tests' interactions: undo after moving, the cursor on the player, Q0 on screen, the edge band and its right
clicks, the infinity switch off, ghost refusal, hit effects armed / registered / re-armed, a spawn trimmed by
the floor, a landing that changes the flags). The B7 snap band is tested natively ("ported:
test_stage113_projective_visibility: test_b7_..." in rcg_tests).
