# Porting Ricochet to another engine

Ricochet's game is native and engine-free. The rules, physics, rendering decisions, level loading and
tooling all live in C++ behind a C API (`native/capi/include/rc_api.h`). An engine *hosts* the game; it does
not implement it.

A host does four things:

- It loads a level's JSON text.
- It forwards input as a handful of semantic events.
- It calls one function per frame.
- It draws the draw list it gets back with its own 2D primitives.

The Godot host is one GDExtension node: `native/hosts/godot`, about 500 lines of C++ that include only
`rc_api.h` (a lint checks it), with its project in `engines/godot`. The Python host
(`tests/host_py/rc_host.py`, ctypes, about 300 lines) plays every recorded session bit for bit; use it as the
reference.

## What you build and link

| Library | Target | Use |
|---|---|---|
| `libricochet_capi` (`.dylib` / `.so` / `.dll`) | `rc_capi_shared` | engines that load C libraries at runtime: Unity (P/Invoke), C# / Python / Lua FFI |
| `librc_capi.a` | `rc_capi` | engines that link C/C++ statically: Unreal modules, custom C++ engines (also link `libricochet_game.a` and `libricochet_core.a`) |

```bash
cmake -S native -B native/build-core -DRC_BUILD_EXTENSION=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build native/build-core --target rc_capi_shared rc_capi
```

**Build requirements:**

- A C++20 compiler.
- No third-party runtime dependencies: nlohmann/json and doctest are vendored.
- The flags `-ffp-contract=off -fno-fast-math` (set by `native/cmake/fp.cmake`). The game's numbers are
  bit-exact float32/float64 computations, and the session golden pins them.

Include only `rc_api.h`. The ABI is plain C:

- every struct starts with its `size`;
- every enum is an `int32_t` value that is never renumbered;
- `rc_api_version()` returns `(major << 16) | minor`. Refuse another major version.

## The loop

```c
rc_level *level;  rc_level_parse(json_text, json_length, &level);   // the host reads the file
rc_game *game;    rc_game_create(level, NULL, &game);
rc_level_release(level);                                            // the game keeps its own reference

every frame:
    rc_game_set_cursor(game, mouse_world_x, mouse_world_y);
    for each input event this frame: rc_game_event(game, RC_EVENT_...);
    rc_game_advance(game, dt_seconds, held_keys, pressed_keys);
    rc_game_frame(game, &frame);        // player, facing, arrow, camera, bounds
    rc_game_draw_list(game, &list);     // draw it (below)
    rc_game_hud(game, &text, &len);     // the HUD text (also a LABEL command in the list)

rc_game_destroy(game);
```

### Input

Map your engine's input onto events. These are the Godot host's bindings (`project.godot`'s input map):

| Event | Default binding |
|---|---|
| `RC_EVENT_PLAN_ADD` | left mouse button (plans the surface side under the cursor) |
| `RC_EVENT_PLAN_REMOVE` | right mouse button |
| `RC_EVENT_FIRE` | Space |
| `RC_EVENT_UNDO` / `RESET` / `CLEAR_PLAN` | Z / R / C |
| `RC_EVENT_CYCLE_DISPLAY` | Tab |
| `RC_EVENT_TOGGLE_GRAVITY` / `TOGGLE_VISIBILITY` | G / V |
| `RC_EVENT_ANY_KEY` | any other key (speeds up a flight) |

**The debug dump** (C API 1.5, not an event). The Godot host binds it to F12: `rc_game_debug_dump(game, 0, &text,
&len)` gives the game's state as JSON, which the host saves to `user://debug_dumps/`; with `RC_DEBUG_SUMMARY` it
gives a few lines of text, which the host prints. The JSON holds the solver's inputs exactly (the player and cursor
in hex floats too), the scene's defs, the plan, the preview's walks and the region's summary; the summary also says
whether the preview and the region agree on the cursor. It is meant for bug reports, and its contents may change.

**The violation viewer** (C API 1.6, a tool). `rc_game_set_plan(game, json, len)` sets a plan as it is, from a JSON
array of `{surface_id, side, click?, span?}` (a piece by its span, as the sweeps write it). The Godot host's viewer
mode (`RicochetGame.violations_path`, `scenes/violation_viewer.tscn`) uses it with `rc_game_teleport` and a fixed
cursor to show each of the sweeps' violations (`make violations`).

Movement is not an event. Pass the held movement keys (`RC_KEY_LEFT | RIGHT | UP | DOWN`, default WASD) to
every `rc_game_advance`, and the keys first pressed this frame as `pressed` (Up is the jump with gravity).
A press is kept until the next physics tick runs.

**While the arrow flies:**

- every key event speeds it up;
- clicks are ignored;
- do not send movement keys as events.

The cursor is in world coordinates. If your camera moves, unproject the mouse first.

### Time

`rc_game_advance(dt)`:

- **While a shot flies** (`frame.flying`), the game is paused and the arrow moves `1600 * dt` units
  (ten times faster after a speed-up).
- **Otherwise**, the player's physics runs on a fixed 1/60 s tick, accumulated from `dt` (a `dt` of 0 runs
  no tick). Then the preview, the hover, the overlays and the visibility region are brought up to date.

Call `rc_game_advance(game, 0, 0, 0)` after events whose effect you want drawn before the next frame. The
Godot host does this after every click and key.

### Background work (C API 1.2)

Create a live game with `rc_game_config.async = 1`. The frame then never waits for the solver: the
visibility region and the preview's exact precision climbs are computed on the game's own threads.

- **The region.** Until a newer region is ready, the frame draws the last one, and `frame.region_pending`
  is 1. While the player keeps moving, a running region finishes and shows, a little behind the player,
  while the newest one computes.
- **The preview.** When float64 is not enough to decide the path exactly, the frame draws the float64 path
  and `frame.preview_exact` is 0 until the exact path replaces it. The rules never read a provisional path:
  the overlays, the spawn preview, the record and a fire use the exact one, and a fire waits for it.
- **The click.** While the cursor rests on a plannable side and nothing else computes, the game computes the
  region that side's click would make, so the click shows it at once.

The flags are informational: a host may show a busy hint, or ignore them. Nothing else changes for the host,
which still calls `rc_game_advance` every frame (that is when finished work is collected).

**Animation (C API 1.3).** With `rc_game_config.animate = 1` the draw list is what should be on screen
now: the player drawn between physics ticks and, when a region is late (it missed its frame and its
computation took `rc_game_config.late_ms` or more -- by default 1000/30: slower than 30 Hz; C API 1.4), the
shown region following the player, then the late one morphing in (about 100 ms); a preview whose exact climb was
that slow morphs in from the path shown (about 80 ms). With `rc_game_config.budget_ms` (async), a frame waits
up to that long for the work it started. What finishes in time is drawn that frame as it is, with no
smoothing. The Godot host uses 4 ms; pick what your frame can spare. `frame.camera_x/y` then follows the drawn player. Nothing else changes for the host, but
it must draw every frame: the presenter animates from what the last list showed, and its clock is the `dt`
of `rc_game_advance`. The game's state and record are the same either way.

`async = 0`, the default and an older host's config, computes everything inside the frame, in order. Use
it to replay sessions: the session golden is recorded that way. An asynchronous game reaches the same record
once its work is done, but not on a given frame.

### Coordinates

- 1 unit = 1 pixel at zoom 1.
- X points right, Y points down.
- Angles are radians, measured from +X toward +Y (clockwise on screen).
- Positions are float32.
- Engines with Y up (Unity, Unreal's 2D views) flip Y when drawing: draw at `(x, -y)`, and negate angles and
  rotations.

### The camera

When `frame.camera` is set, the level has a camera:

- It follows `(frame.camera_x, frame.camera_y)`: the player, or the arrow in flight.
- It is clamped to the bounds `(bounds_x, bounds_y, bounds_w, bounds_h)`.
- It is smoothed when `camera_smoothing` is set: an exponential approach at `camera_smoothing_speed` per
  second, which is Godot's `position_smoothing_speed`.

Without a camera, the view shows world `(0, 0)` to `(1920, 1080)`.

## Drawing the draw list

`rc_game_draw_list` returns nodes over commands over flat pools: points as `(x, y)` float pairs, colours as
`(r, g, b, a)` float quadruples, triangle indices, and UTF-8 text. The view is borrowed: copy what you keep
before the next call that mutates the game.

**Nodes.** Draw the nodes in order:

- The list is sorted by `z`. Within one `z`, list order is draw order.
- Each node's commands are in its own frame, translated by `(node.x, node.y)`.
- Multiply every colour by `node.modulate` (the path, the visibility region and the spawn preview dim to
  alpha 0.25 during a flight).
- The node's `key` (JSON, e.g. `["surface",3,-1]`) is stable while the node exists. Use it to keep one
  retained draw object per node, and redraw a node only when its commands change. The Godot host compares
  the commands' bytes.

**Changes only (C API 1.1).** `rc_game_draw_changes` returns the same list, with each node's stable id
(`node_ids`) and content version (`node_versions`). A node whose commands are the same as at the previous call is
marked `node_unchanged` and carries no commands: keep what you drew for it, and update only its position, z and
modulate. A node missing from the list is gone. The Godot host draws this way.

**Commands.** Each kind uses the fields listed:

| Kind | Fields | Meaning |
|---|---|---|
| `LINE` | points[0..1], color, width | a segment |
| `ARC` | points[0] center, radius, start/end angle, arc_point_count, color, width | the polyline `rc_tessellate_arc` returns (Godot's `draw_arc`) |
| `POLYGON` | points, colors (one per vertex) | a filled convex polygon (chevrons, the arrow head) |
| `COLORED_POLYGON` | points, color | a filled polygon |
| `CIRCLE` | points[0] center, radius, color | a filled disc |
| `RECT` | points[0] position, points[1] size, color | a filled axis-aligned rectangle |
| `POLYLINE` | points, color, width | an open polyline |
| `MULTILINE` | points (pairs), color, width | independent segments |
| `DASHED_LINE` | points[0..1], color, width, dash | a dashed segment, dash length = gap length = `dash` |
| `TEXT` | points[0] baseline start, text, alignment, font_size, color | a label starting at the point (the alignment is what the game passes Godot's `draw_string` with no width, where Godot ignores it: draw every label from its point) |
| `TRIANGLES` | points, indices, color | a filled triangle mesh (the visibility region) |
| `POLYGON2D` | points, color, rotation | the player: a filled polygon rotated by `rotation` about the node's origin |
| `LABEL` | text | the HUD: the default UI font at world (10, 10), white |

**Widths.** A `width` below 0 means a thin line: one pixel, not scaled by zoom (Godot's default). Otherwise
it is in world units.

**Pixel-identical Godot.** The Godot host maps each command onto the CanvasItem call with the same name and
parameters, and so draws the original game's pixels. Before the GDScript game was retired, this was
checked against its scenes with screenshots: 36 pairs, 0 differing pixels (docs/TDD_23_ENGINE_BOUNDARY.md).

**Other engines.** Stroke and fill with your own primitives. Pixel identity with Godot is not required; the
geometry is exact.

## Errors and logging

**Return statuses.** Every call returns `RC_OK` or an error status:

- `RC_E_ARGUMENT`: null, malformed or unknown input.
- `RC_E_LEVEL`: the level does not follow `rc-level/1` (`docs/LEVEL_FORMAT.md`) or does not build.
- `RC_E_INTERNAL`: a bug; it is also logged.

`rc_last_error()` holds the message, per thread. No C++ exception ever crosses the boundary.

**The log.** Install a sink with `rc_set_log(fn, user)` to route the game's errors and warnings to your
engine's log. The default writes to stderr.

## Threading

- Use one game from one thread at a time.
- A parsed level is immutable and may be shared between games on any threads.
- The library keeps no global state besides the log sink, the per-thread last error and a pool of compute
  threads (the hardware threads but one) that the visibility region's casts share.
- An asynchronous game (`async = 1`) owns up to three worker threads of its own. It calls back into
  nothing: its results are taken on your thread in `rc_game_advance`. `rc_game_destroy` waits for its
  running work, cancelling what it can.

## Verifying a port

`SKIP_GODOT=1 make gate` runs every engine-free check: the native tests, the oracle, the session golden
through the C API in C++ and in Python, and the lints that keep the game engine-free.

**The session golden.** `bench/sessions/*.jsonl` records the game's full observable state after every step
of 48 scripted sessions (`data/sessions/*.json`, documented in `docs/research/session_digest.md`). A host
that replays them through the C API and gets the same records drives the game correctly:

- `rc_tool replay data/sessions data/levels --check bench/sessions` does this in C++;
- `tests/host_py/rc_host.py replay()` does it in Python.

**Replaying a script.** For each step:

- **teleport:** `rc_game_teleport`.
- **cursor:** `rc_game_set_cursor`.
- **click:** the plan event.
- **key:** its event.
- **fire:** `RC_EVENT_FIRE`, then frames of 1/60 s until the arrow lands. Before each frame, sample the
  arrow from the frame. Send `RC_EVENT_ANY_KEY` after frame `speedup_at`.
- **hold:** frames of 1/60 s with the held keys; the Up key is pressed on the second.

Then call `rc_game_advance(game, 0, 0, 0)` and `rc_game_record(game, extra)`, where `extra` is the step's own
fields. The record is the golden's line.

**Draw-list mapping.** `tests/host_py/test_capi_drawlist.py` checks that the flat list matches the record's
draw list call for call. Port it to check your mapping.

## Engine notes

### Unity (C#)

Load `libricochet_capi` with `[DllImport("ricochet_capi")]`. Mirror the structs with
`[StructLayout(LayoutKind.Sequential)]` and the field order of `rc_api.h`: `rc_host.py`'s ctypes structs are
the same declarations, one to one.

Every frame, from a MonoBehaviour's `Update`:

- set the cursor from `Camera.main.ScreenToWorldPoint` (flip Y);
- send the events from `Input`;
- call `rc_game_advance(Time.deltaTime, ...)`;
- draw the list with a retained `Mesh` per node: tessellate arcs with `rc_tessellate_arc`, triangulate
  strokes as quads, and use `TextMeshPro` for text.

Copy the pools out of the borrowed view with `Marshal.Copy`.

### Unreal (C++)

Link `rc_capi` statically from a module (`PublicAdditionalLibraries`) and include `rc_api.h`. A `UObject` or
`AActor` owns the `rc_game`.

In `Tick`, map Enhanced Input actions onto events, and draw either with `UCanvas` in a HUD (lines, polygons,
text) or with a procedural mesh per node. The draw list is in pixels, with Y down like Unreal's canvas.

### A custom engine

Drive the loop above from your main loop. Draw the list with your renderer: a line and triangle batcher, an
arc tessellator (or `rc_tessellate_arc`), and a font.

For a host that only needs to run the game, with no rendering, `rc_tool replay`
(`native/hosts/headless/replay_capi.cpp`) is a complete example in
under 200 lines.

## Files and data

- **Levels:** `data/levels/**/*.json` (`rc-level/1`). The Godot host's build (`make native`) copies them into
  `engines/godot/data/`, because an exported Godot game reads only `res://`. Package them however your engine
  ships assets.
- **Saves:** GAME_SPEC §27 keeps progression on the host side.
