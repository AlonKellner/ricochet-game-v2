# TDD 27: The presenter — the player between ticks, the region morphing as a diff of shapes (Stage 122)

The third stage of the smooth-frames program (plan approved by the user, 2026-10-20). Stage 121 took the
solver off the frame, but what it computes still lands on screen at once: a region arrives a few frames
behind the player and replaces the one shown, and a plan change swaps the whole region in one frame. This
stage animates those changes. It is presentation only: the game's state, its record, every result and the
session golden are unchanged, and replays run unanimated.

The decisions this stage follows (user):

- **Subtle and quick** (2026-10-20): about 80–120 ms, ease-out.
- **Crisp lines with a morph, no crossfades** (2026-10-20): no opacity blending of changing shapes.
- **Match by surface** (2026-10-21): "both visibility morphs and path preview morphs should try to match
  hitpoints of the before and after shapes by the surface ids, such that the morphs usually just move the
  points along the same surface".

The plan's presentation stage is split in two. This stage builds the presenter, the player's motion and
the region morph. Stage 123 brings the path morph (the same surface matching, over the path's hits), the
overlays, the spawn preview and the flight's dimming.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 122.1 | `rcg::Presenter` (`rcg/present.hpp`): the session's frame in, the frame on screen out; its clock | Done |
| 122.2 | The player drawn between its last two physics ticks (`Session::tick_from`, `tick_alpha`) | Done |
| 122.3 | The region morph: first per ray (rejected interactively), then a diff of shapes (`rcg::shape_morph`) with the shadows following the player; C API 1.3 (`rc_game_config.animate`); the Godot host animates | Done |
| 122.4 | Smoothing only for late work: the frame's budget (`SessionConfig::budget`, C API `budget_ms`); the presenter morphs and follows only a late region | Done |
| 122.5 | Docs, `make gates`, sign-off | In progress |

## 122.1 The presenter

`Presenter::frame(nodes, session)` takes the session's frame (`Session::draw_frame`) and returns what is on
screen now. An unchanged node keeps its content version, so a host's change tracking (C API 1.1) still skips
it. `Presenter::advance(dt)` moves its clock. `rc_game_advance` calls it, so the clock is the host's frame
time.

The presenter only reads the session. A host without `animate` never creates one, and the replays, the
record and every golden are the session's alone.

## 122.2 The player between ticks

Physics runs on a fixed 1/60 s tick. On a display that is not 60 Hz the player would move on some frames and
not on others. The presenter draws the player at `tick_from + (player − tick_from) · alpha`, where
`tick_from` is its position before the last tick and `alpha` is the unticked time over the tick. This is the
usual render interpolation: the drawn player trails the game's by less than one tick. A jump of over 64 px
between ticks (a teleport, a reset, an undo) is drawn at once. While the arrow flies, the game is paused and
the player is drawn where it is.

`rc_frame.camera_x/y` follows the drawn player when animating, so the camera moves as smoothly as the
player. Godot's own camera smoothing still applies on top of it.

## 122.3 The region morph: a diff of shapes

**The first design, and why it went.** The first version sampled each region along 720 rays from its apex
and interpolated each ray's visible spans, sliding ends along their surfaces. When nothing matched (the apex
jumped, a pencil of circles), it collapsed the old region into the player and grew the new one. The user's
interactive test (2026-10-21) rejected both:

- "When moving the visibility edges are crisp, but they are no longer lines, they are strange curves." On a
  ray whose end changes surface, the per-ray interpolation bends straight edges.
- "When an inversion is planned in inversion_grid, the flickering is unbearable." A planned inversion's
  region is a pencil of circles, so every region update collapsed and regrew it.

The user's direction: "calculating a git like diff ... between the shapes of two frames ... keeping the
points that match paired during the animation. Then, diff points should be paired with arbitrary points on
the counter part shape, preferably the closest points. Also, the morph should prefer to keep shapes as
simple polygons or multi-lines with straight lines."

**The shape** (`shape_morph::shape_of`). A region is its cells' rings, exactly as the fill makes them:
conforming (a corner of one cell on another's edge is a vertex of both), clipped to the domain and split into
simple rings, each counterclockwise. Cells are not merged: a pencil's two sheets can overlap (a planned
inversion's cells cover 3.4M px² of a 2.4M px² domain), and the fill draws each cell. Straight runs are
merged and arcs reduced to chords within 0.5 px. Every vertex is tagged with the surfaces it lies on: a wall
corner has two, a point on a wall one, the apex none.

**The diff** (`shape_morph::plan`):

- **Pieces pair by place.** A cheap signature (centroid distance for the size, the areas' ratio) picks each
  piece's three likeliest partners both ways. Then the mean distance of each one's vertices to the other's
  outline ranks them, and pairs are taken greedily.
- **Each pair is aligned like a diff.** This is a cyclic Needleman–Wunsch alignment:
  - a pair of vertices costs their distance, half when they lie on the same surfaces, plus a penalty when
    their surfaces differ;
  - an unpaired vertex costs its distance to the other ring, plus a gap cost;
  - the cyclic start (the anchor) is the cheapest of several tried: every rotation of a small ring, the two
    closest same-surface pairs and the closest point for a large one. One fixed anchor reversed thin wedge
    cells that moved more than their width.
- **Unpaired vertices go to the other shape's closest points,** as the user specified. A deleted vertex moves
  to the closest point of the new ring's stretch between its paired neighbours' partners, and an inserted
  one comes from the old ring's stretch. Within each gap the projections are kept in order along both
  chains, so both ends trace their rings in order (no zigzag on a curve, no notch at the end).
- **A piece with no partner grows out of the other shape's closest edge** (or shrinks into it), as a
  degenerate polygon on that edge. With nothing there (the region switched on or off), it grows out of the
  drawn player.

**The frames.** Every vertex moves in a straight line (ease-out, 100 ms), so every frame is polygons of
straight edges, triangulated by earcut. When a newer region arrives mid-morph, the diff starts again from the
shape on screen. At the end, the node is the session's exact region.

**Following the player before the next region** (the user's idea: "the rays which reach the player update to
the new position even before the new visibility region ... is fully calculated"). When a region's apex is
the player (no plan, or entries that do not transform), its shape follows the drawn player at once:

- its apex vertices sit on the drawn player;
- a shadow's far end recasts: on a ray from the apex through a surface's end (the occluding corner) to a
  straight surface, the far end moves to where the ray from the drawn player through that corner meets the
  surface, while it stays on the surface's segment.

The morph's target follows the same way, so the shape reacts to every frame of motion, and the next region
corrects the rest. Behind a planned mirror the apex is the player's image, so those regions only morph.

**Measured** (one new region's shape and diff, a morph frame; per level):

| Level | Pieces | Vertices | Shape | Diff | Frame |
|---|---|---|---|---|---|
| main | 22 | 66 | 0.06 ms | 0.14 ms | 0.01 ms |
| crossings | 50 | 155 | 0.10 ms | 0.21 ms | 0.01 ms |
| tangent_infinity | 46 | 1204 | 0.25 ms | 1.88 ms | 0.10 ms |
| inversion_grid, inversion planned | 66 | 2773 | 0.68 ms | 4.24 ms | 0.34 ms |

Walking four steps on each of these levels, every diff frame (e = 0.25, 0.5, 0.75) has no self-crossing on
crossings, main and hit_effects. On the planned inversion it has a few more than the regions' own sub-pixel
slivers. Each frame's area stays between the two regions'; the inversion dips about 2% mid-morph.

## 122.4 Smoothing only for late work

The user's direction after the diff (2026-10-21): "I highly prefer to avoid using this mechanism in normal
circumstance. Ideally, the preview and visibility calculations will be optimized to be fast enough for the
required frame rate, and this smoothing algorithm will not even trigger. Please make this mechanism trigger
only if the calculations don't match the required frame rate."

**The frame's budget** (`SessionConfig::budget`, C API `rc_game_config.budget_ms`; the Godot host's
`solve_budget_ms`, 5 ms by default). An asynchronous frame that starts work (a region, a preview's exact
climb) waits for it up to the budget, then settles again. What finishes in time shows that frame, exact, as a
synchronous frame would show it. What does not finish arrives in a later frame. A budget of 0 (the default)
never waits.

A frame waits only for the work it started itself; work already late is not waited for again. It also skips
the wait when the last region (or exact climb) took longer than the budget. Waiting for work that cannot
finish in time would only slow the frame.

**On time, nothing is smoothed.** The presenter calls a region late when the session still waits for one at
frame time (`region_pending`), or when the last frame did. The rules:

- **No region late:** the session's region is drawn as it is, with no morph and no following.
- **A region late:** the shown one follows the drawn player (its shadows recast), and when the late region
  arrives it morphs in from what was shown.
- **A morph running:** a newer region retargets it, never cutting it short.

A synchronous game (the replays) never has a region late, so it never morphs.

**Measured** (an asynchronous session with a 5 ms budget, walking left and right for 240 frames at 60 Hz;
a frame is late when the region or the exact preview is still pending after it):

| Level | Late frames | Frame mean | Frame max |
|---|---|---|---|
| main | 0 / 240 | 0.94 ms | 1.31 ms |
| inversion_grid | 165 / 240 | 1.50 ms | 6.60 ms |
| tangent_infinity | 239 / 240 | 0.38 ms | 6.37 ms |
| hit_effects | 240 / 240 | 0.57 ms | 6.45 ms |
| crossings | 240 / 240 | 0.05 ms | 0.16 ms |

On main every region is on time: nothing is smoothed. On crossings, hit_effects and tangent_infinity a
walking region takes longer than a frame (crossings about 15–17 ms, hit_effects over a second: F7), so the
smoothing covers them on every frame. Making those regions fit the frame is the next stage's work (the plan's
N+3: the solver's ties, the region's refinement), after which the smoothing will rarely trigger there either. The player drawn between
physics ticks is separate (motion at display rates that are not 60 Hz) and stays.

### What still made main smooth (the user, 2026-10-21: "I can still sometimes see morphing and hairline artifacts on main")

Measured on main (walking 600 frames at 60 Hz, the cursor resting on a mirror, a 5 ms budget):

| Cause | Late frames | Fix |
|---|---|---|
| The prefetch (Stage 121) ran between frames while the player walked; its casts held the shared pool, so the next region missed its budget | 86–115 / 600 | No prefetch until the player has stood still for 100 ms: 6–11 / 600 |
| One slow region (OS scheduling: 2–7 ms instead of 1 ms) made the next frame skip its wait | (of the rest) | The wait is skipped only when the last 5 regions' median exceeds the budget: 0–2 / 600 |
| On-time regions renewed a running morph | (all, after one late region) | A morph is retargeted but never extended on time (122.4b) |

**The hairlines** came from morphing a region cell by cell: two neighbouring cells' shared edge could part or
overlap mid-morph (F11). A region whose cells do not overlap now morphs as the outline of their union: the
edges two cells share cancel (points identified on a 1e-4 px grid), the rest chain into rings (outer
counterclockwise, holes clockwise). There is no edge between cells left to part. A region whose cells overlap
(the outline would cross itself, as a planned inversion's does) still morphs cell by cell. A shape carries
both forms; a diff uses outlines when both shapes have one, else pieces for both. On main and crossings the
outline is one ring of 11 and 36 vertices, and the diff costs about 0.1 ms.

## The C API (1.3)

| Field | Meaning |
|---|---|
| `rc_game_config.animate` | 1: the draw list is animated (`rcg::Presenter`): the player between ticks, a late region morphed in; 0 (the default, and an older host's config): the game's state as it is |
| `rc_game_config.budget_ms` | (async) the milliseconds a frame waits for the work it started; on time it shows that frame, unsmoothed. 0 (the default): never wait |

With `animate`, `rc_frame.camera_x/y` follows the drawn player. The Godot host animates (its `animate`
property, on by default); its session-replay test turns it off.

## Tests

| Test | What it shows |
|---|---|
| `present: a region's outline is its fill` | the pieces' area is the fill's (4 levels and a planned inversion); every ring is simple; most vertices lie on surfaces |
| `present: the diff of two regions starts and ends on them, pairs their corners, and stays simple polygons` | walking on crossings, main, hit_effects and inversion_grid with an inversion planned: e = 0 and 1 are the two regions; between, the area stays between theirs and no ring crosses itself (beyond the regions' own slivers); 90% of shared corners pair with themselves |
| `present: a ring with no partner grows out of the anchor` | an empty region to a full one grows monotonically and ends exact |
| `present: without animation the presenter's frame is the session's` | no durations, no following: every node's key, version and position are the session's |
| `present: a region's change morphs for its duration, then shows the region` | a walk morphs and lands on the exact node; a planned mirror never drops below half the area; switched off, no frame has an empty mesh (Godot refuses one) |
| `present: before the next region, its shadows follow the drawn player` | half a tick behind: the shown mesh is the region with its shadows recast from the drawn player (main.json's mirrors occlude) |
| `present: the player is drawn between its last two physics ticks` | half a tick: halfway |
| `present: regions on time are drawn as they are: no morph, no following` | an asynchronous session with a budget, walking between ticks: every region node is the session's, nothing animates |
| `present: a region that misses its frame follows, then morphs in` | no budget: while pending the region follows; on arrival it morphs; then it is the session's |
| `async: a frame with a budget shows the work that finishes in it, as a synchronous frame` | with a budget, nothing pending and the record equals the synchronous session's without a drain |
| `present: one late region does not keep on-time regions morphing` | one late region, then 30 on-time walking frames: the morph ends within its 100 ms |
| `present: cells that do not overlap morph as one outline (no edge between cells to part)` | main: one outline ring, its diff one ring mid-morph; a planned inversion: pieces |
| `async: no prefetch while the player moves` | 20 walking frames start no prefetch; standing still 100 ms starts one |
| `test_an_animated_game_smooths_only_a_late_region` (Python host) | through the C API: a synchronous animated game draws the game's region; an asynchronous one with no budget morphs its late region in, then draws the game's |

## Findings

| # | Finding | Status |
|---|---|---|
| F8 | A region and the preview arrive later than their input and replace what was shown at once | The region: resolved here (the morph). The path: Stage 123 |
| F9 | The first morph (per ray) bent straight edges and collapsed a planned inversion's region on every update | Resolved: the diff of shapes (any pencil, straight edges) |
| F10 | A planned inversion's diff costs about 4 ms per new region on the frame thread (2773 vertices) | Open (only when late, since 122.4): the diff could run beside the region's job |
| F11 | Mid-morph, two cells' shared edges can part or overlap by a hairline (each pair is aligned on its own) | Resolved where the cells do not overlap (one outline); open for overlapping cells (a planned inversion), only when late |
