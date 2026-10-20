# TDD 23: The engine boundary -- the draw list, the C API, its hosts (Stage 118)

The fourth stage of the engine-agnostic port (TDD_20's introduction). Everything the game shows is now
decided natively, and the whole game sits behind a plain C ABI that any engine can host:

- **The draw list.** `rcg/render.hpp`, `Session::draw`: every 2D draw call of every node, in CanvasItem terms.
- **The C API.** `native/capi/include/rc_api.h`.
- **Three hosts on it:**
  - the headless `rc_tool`;
  - a Python ctypes host;
  - a Godot adapter node.

Godot's own GDScript game still ships. The next stage switches the scenes to the adapter and deletes the
GDScript game.

Background: TDD_20 (the session golden), TDD_21 (`rcg::Session`), TDD_22 (the native controller).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 118.0 | Branch `agnostic-host`, this doc | Done |
| 118.1 | The draw list natively, checked call for call against the GDScript nodes; the golden records it (approved) | Done |
| 118.2 | The C API (`rc_api.h`) over a live-play `rcg::Session`; a C11 smoke host | Done |
| 118.3 | `rc_tool replay` on the C API alone; the Python ctypes host (`tests/host_py`, `make pyhost`, gate `pyhost`) | Done |
| 118.4 | The Godot adapter (`RicochetGame`, `native/hosts/godot`, `scenes/rc_level.tscn`); the screenshot comparison | Done |
| 118.5 | Docs (GAME_SPEC 2.30.0 §20.1, §20.4, §21.1; CHANGELOG; WORKFLOW), `make gates` (964 s, green) | Done |
| 118.6 | Interactive sign-off: `scenes/rc_level.tscn` (the adapter) side by side with `scenes/main.tscn` | |

### 118.1 The draw list

- **The GDScript side.** `scripts/visual/draw_rec.gd` (`DrawRec`) wraps every draw call of the game's nodes
  (`SurfaceNode`, `EdgeOverlayNode`, `VisibilityRenderer`, `SpawnPreviewNode`, `PathRenderer`,
  `ArrowAnimator`, `Cursor`). It draws, and while it is enabled (the session recorder) it also logs each call
  with the arguments Godot takes.
  - `SessionDigest.draw` gathers the logs.
  - It adds the nodes drawn by their properties: the player's Polygon2D, the background ColorRect and the
    HUD's Label.
  - Each record of a node is `[key, z_index, position, modulate, calls]`.
  - Two `draw_string` calls of the edge overlay were not logged and now go through `DrawRec`.
- **The native side.** `rcg/render.hpp` and `native/game/src/session_draw.cpp` (`Session::draw`) port every
  `_draw`, keeping GDScript's precision:
  - Vector2 arithmetic in float32;
  - a float64 scalar rounded where it meets a vector;
  - `wrapf` and the edge's float64 comparisons;
  - the stop octagon's `from_angle` in float32.
- **Evidence.** The Godot recording (`RECORD_ARGS="--draw --full" tools/record_sessions.sh`) and the native
  replay (`rc_tool replay --full`) are identical on all 1316 records of the 40 sessions: about 133k calls of
  every kind and every node, in all three display modes.
  - Two things are not covered. No level has a state-diamond hit marker, and records are taken between
    flights, so the arrow in flight is not recorded either.
  - The arrow is checked through the C API instead: `tests/host_py`, `test_a_shot_flies_and_the_arrow_is_drawn`.
- **The golden.** With the user's approval (2026-10-20), every record of `bench/sessions` holds `draw`: the
  sha of the list.
  - All other fields are byte-identical.
  - The recorder always records it; `--full` writes the list itself.

The draw order. The digest sorts a list by (z, key), which is independent of the tree. The draw list itself
(`Session::draw`, the C API) is in draw order: by z, and within a z in the level's tree order. Every scene has
the same tree order:

1. the background;
2. the player;
3. the cursor;
4. the path renderer (z 20);
5. the arrow (z 30);
6. the HUD;
7. then the nodes that `_ready` adds: the surface pieces, the edge (z 5), the visibility region (z 10) and the
   spawn preview (z 19).

So the player and the cursor draw under the surfaces. GAME_SPEC §20.4's player=40 was never implemented; the
spec now states the real order.

### 118.2 The C API

`native/capi/include/rc_api.h` is `extern "C"`. It is built as `rc_capi` (static) and `ricochet_capi`
(shared).

**Its rules:**

- Every struct is size-prefixed, so an older host's smaller struct is filled as far as it reaches.
- Enums are int32.
- The library owns all memory. Views are borrowed until the next non-const call.
- No exception crosses the boundary. A failure is a status, its message is in `rc_last_error()` (per
  thread), and an internal failure is also logged. `rc_set_log` bridges the core's error sink.
- Games are single-threaded; a level is immutable and shared.

**The calls:**

| Area | Calls |
|---|---|
| Levels | `rc_level_parse` (refuses a level that does not build), `rc_level_release` |
| Games | `rc_game_create` (switches: infinity, visibility), `rc_game_destroy` |
| Input | `rc_game_event` (PLAN_ADD, PLAN_REMOVE, FIRE, UNDO, RESET, CLEAR_PLAN, CYCLE_DISPLAY, TOGGLE_GRAVITY, TOGGLE_VISIBILITY, ANY_KEY), `rc_game_set_cursor`, `rc_game_teleport` |
| Frames | `rc_game_advance(dt, held, pressed)` |
| Output | `rc_game_frame` (player, facing, flight, camera, bounds, display mode, gravity), `rc_game_draw_list` (nodes over commands over pools of points, colours, indices and text), `rc_game_hud` |
| Records | `rc_game_record` (the golden's record; `RC_RECORD_DRAW_LIST`), `rc_digest_sha` |
| Helpers | `rc_tessellate_arc` (`draw_arc`'s polyline, for engines without arcs) |

**Live play.** `rcg::Session` now plays frame by frame:

- `start_fire`, `speed_up`, and `advance(dt, input)`.
- In flight, the arrow moves one frame.
- Otherwise, the physics ticks that are due run (a fixed 1/60 s, accumulated), then the session settles.
- A jump press waits for the next tick.
- `fire()` and `hold()` are these primitives at a fixed frame time, so the golden still checks them.
- During a flight, every key event speeds the arrow up and clicks are ignored, as in `game_manager.gd`.

**The C11 host.** ctest `capi_c_smoke` (`-std=c11 -pedantic -Werror`, against the shared library) checks:

- the error paths;
- a plan, a shot flown frame by frame, and a speed-up;
- the draw list's bounds;
- an older host's smaller struct;
- the HUD, the record, the sha and the tessellation.

ctest `capi_no_godot` checks the C API names no Godot header.

### 118.3 The hosts

- **`rc_tool replay`** plays the session scripts through `rc_api.h` alone (`native/hosts/headless/replay_capi.cpp`).
  - It sends events, the cursor and frames of 1/60 s.
  - In a hold, it sends the held keys with the up key pressed on the second tick.
  - It reads the arrow and the player back from the frame, and hashes the flight stream with `rc_digest_sha`.
  - ctest `capi_sessions_{0..5}` runs `--check bench/sessions`: all 40 sessions equal the golden.
  - `rcg_sessions_*` keeps checking the session directly.
- **`tests/host_py`** (`rc_host.py`) is a ctypes host, the way Unity's P/Invoke binds a C library.
  - `test_capi_sessions.py`: all 40 sessions from Python equal the golden (33 s).
  - `test_capi_errors.py`: statuses, messages, older structs, level lifetime.
  - `test_capi_drawlist.py`: the flat list equals the record's list call for call; the draw order; a shot in
    flight draws the arrow and dims the path; `rc_tessellate_arc`.
  - It runs as `make pyhost` and as the `pyhost` gate.

### 118.4 The Godot adapter

`native/hosts/godot/ricochet_game.{hpp,cpp}` is linked into the extension and registered by its
initializer. ctest `godot_adapter_includes` checks that it includes only `rc_api.h`, godot-cpp, the standard
library and its own headers; the check was shown to catch a planted `rcg/` include.

**What `RicochetGame` (a Node2D) does:**

- **Level.** Reads `level_path` with FileAccess.
- **Input.** Maps Godot's input as `game_manager.gd` did:
  - Tab, G, V, Z, R, C;
  - the left and right mouse buttons;
  - the `fire` action;
  - in flight, any key except the movement keys.
- **Frames.** Advances the game every frame with the held and just-pressed movement actions, and follows the
  camera (smoothing and limits from the level).
- **Drawing.** Paints the draw list as one `RicochetLayer` per draw node, kept in draw order with its z,
  position and modulate.
  - A layer's `_draw` makes the node's calls through the same CanvasItem API the GDScript nodes used: `draw_arc`, `draw_dashed_line`, `draw_string` with the fallback font, and the triangle array.
  - A layer is redrawn only when its commands' bytes change.
  - The HUD is a Label at (10, 10).

**Hooks for tools and tests:**

- `aim` sets a fixed cursor.
- `teleport` moves the player.
- `physics` turns the physics off.
- `record` and `hud` return the session record and the HUD text.

`scenes/rc_level.tscn` (the adapter on `main.json`) runs side by side with the old scenes.
`tests/test_stage118_adapter_smoke.gd` checks:

- the layers and their order;
- the record;
- Tab;
- a shot through `Input.parse_input_event`.

**The screenshots.** `make shots` runs `scripts/testing/shots.gd`. It needs a window, because the headless
renderer draws nothing, and the window must stay visible: on macOS an occluded window draws no frames and the
run waits for them.

- It shoots every level that has a tour session, the GDScript scene against the adapter, in the same state.
- The state: the player where the tour first puts it, the physics off, the cursor where the tour first aims.
  A second shot puts the cursor where the tour first plans, then clicks.
- It waits for the camera to settle before each shot.
- It counts the differing pixels. Result: 36 pairs over 18 levels, 0 differing pixels in each (1920x1080).
  - The aimed state covers the preview, the visibility region and the player's facing.
  - The planned state adds the plan's chevrons and labels, the hover, the HUD, and the hit markers.

## Findings

| # | Finding | Status |
|---|---|---|
| F5 | GAME_SPEC §20.4 gave the player z 40 and surfaces under everything; the game draws the player and the cursor at z 0 before the surfaces (tree order) | Documented (§20.4), unchanged: the port keeps the game's order |
