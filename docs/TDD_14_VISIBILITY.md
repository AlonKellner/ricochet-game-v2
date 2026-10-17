# TDD Document 14: The Visibility Region

**Stage 110** | The white region under the preview: every cursor position at which the planned and the physical paths agree up to the cursor (GAME_SPEC §15). It is built natively on the beam solver, and two families of invariants pin it: the region equals the non-divergence set, cursor by cursor, and its polygons are valid and correctly built.

Specification: GAME_SPEC §15 (rewritten in this stage), §29.3. Background: TDD_06 (the original plan, Stages 34a–38, written for the deleted frame solver and never implemented), TDD_10–13.

### Stage Status

| Phase | Topic | Status |
|-------|-------|--------|
| 110.0 | B1: an entry that changes no frame is consumed by both walks (oracle first) | Done |
| 110.1 | B2: a path cut by the event limit shows the stop marker | Done |
| 110.2 | Native region: members, casts, cells, clipping, regions, `contains` | Done |
| 110.3 | Invariants: the pin and the structure checks; native tests, oracle labels | Done |
| 110.4 | Bindings, `VisibilityRenderer`, the level hook, the V toggle, the GUT sweep | Done; user sign-off pending |
| 110.5 | Projective plan entries | Not started: the region is not drawn for such plans |
| 110.6 | Follow-up: B3, B4, B5; the region drawn as one mesh; K8; VIS-REGULAR, VIS-DRAWN-MATCHES, the cursor-reading NO-POST check, carrier cursors in the sweeps, a detection case per region check | Done; user sign-off pending |

**Workflow:** branch `stage110-visibility`, small green commits. Before every commit: the full GUT suite (`./run_tests_parallel.sh`), ctest in RelWithDebInfo and under ASan (`make native-asan`, run alone), `make oracle-test`, and `git diff --stat bench/goldens` empty.

## Design decisions

**The definition is the merge, and the cursor's own point.** A cursor is visible iff (V1) the preview's planned path reaches it, (V2) every merged step before it is ALIGNED (GAME_SPEC §14.2, frame ids: epoch, stack, flags), and (V3) both walks leave the cursor's own point in the same frame (B4). V3 reads the events at the cursor's drawn point (`cursor_point_agrees`, `TracePath.event_frames`); when the physical walk has no event there it passes the point in the frame it had. Where `trace_pair` returns no pair (the cursor on the player) the cursor is not classified. A cursor the trace never reaches (cut by the event limit) is outside (user, 2026-10-16).

**Casts are traces, members are beams.** Every cursor whose aim at the player has direction `d` lies on one beam of the plan's final frame: the circle through the player's image `P_K`, the cursor and the frame's infinity `Q_K` (a line when `Q_K` is infinity), the *member* of `d`. On a member the visible cursors form intervals: from the last entry's departure point (the player, with no plan) to the first event at which the physical walk changes the frame (the stop) or to the budget's last event, inside the member's arc owned by `d` (`P_K` to `Q_K`). A cast is an ordinary pair trace (`Tracer::solve`) whose cursor lies on a member. It is read through the steps the preview draws (`Tracer::cast_paths`: the same conversion and frame ids as `trace_pair`, with `TracePath.event_steps` mapping events to steps), so the cast compares the two walks exactly as the merge does: by step index, by frame id. Events at one drawn point end one step, and a step of zero length (a cursor at a teleport's exit) is not drawn. No solver parameter was added: the approved plan's `cursor_entry` turned out unnecessary (simplicity).

**The far end.** The first physical event past the departure point (a step beyond it) that changes the frame or ends the walk is the stop. A walk whose beam began before the last entry (an infinity entry that kept the beam, in a pre-transform level) ends its lap where the beam began, not at `P_K`: that point is the stop.

**Critical members.** The walk's shape can change only where a member passes a segment endpoint, a tangency, a proper crossing (GAME_SPEC §9.7), a planned piece's end, the tangency at a finite `Q`, or (for a finite `Q_K`) the straight member through `P_K` and `Q_K`. These points, in every frame of the plan, are mapped to the final frame. Each one bounds two members, one each way along its line, because a walk passes the point after its wrap too. The four axes keep every gap below a quarter turn. Members are sorted by a diamond pseudo-angle (a real key: a strict order without trigonometry, §31.2).

**Cells.** Three casts per member (the member, and either side of it at ±ε) and one in each gap. Where the three casts of a gap disagree on the walk's shape, the gap is bisected and a member is inserted at the switch (refinement). A cell is a curvilinear quadrilateral: two member arcs, the carrier arc its stops lie on, and the carrier arc its departures lie on (a point, for the empty plan). Its corners are the crossings of each side member with those carriers: the same computation for every cell that shares a corner, so shared corners are equal. On a member through a point of interest, a crossing there snaps to that point. On a tangency member, the touched carrier's crossing is the tangent point.

**Clipping.** A cell through infinity (members to `Q_K` at infinity, or beside the straight member through a finite `Q_K`) runs outside the clip rectangle along a connector. Every cell is cut by the rectangle (Weiler–Atherton against a convex window), walking its boundary the way the cells wind: reversed by every reflection or inversion in the final frame, and by the realization through infinity. The rectangle holds the level bounds grown by 50 (the classified cursors) and every finite corner.

**Regions.** Adjacent cells are merged: member pieces of opposite directions cancel, and so do carrier pieces two layers share (a departure arc that is also a stop arc). What remains is chained into loops. A loop through one vertex twice (a region pinched there) is cut into simple loops, and sliver edges shorter than 1e-9 of the coordinates' scale are dropped. `contains` is even-odd over the loops, with the half-open rule at the ray's height (arcs cut into y-monotone pieces). On a boundary edge it returns the edge's flag, and at a vertex the vertex's flag. A member's piece is inclusive where the member's own cast sees it, and stops and departure arcs are inclusive (the cursor wins ties, and a departure point is reached at once). At a member's own point of interest, a vertex takes the cast's answer for a cursor exactly there.

**Whiskers.** The region's only 1D parts (VIS-REGULAR): an arc of a member tangent at a frame's finite infinity point (`Q_j`, in any frame of the plan) to a carrier through it (every other member crosses that carrier there and diverges), and a piece of the last planned entry's own segment when that side is a terminal (the arrow ends exactly at the cursor). A departure whisker is kept only when a cast exactly on the carrier (a dyadic point of the segment, `exact_on_carrier`) sees its cursor; under V3 that rules out every departure off the planned segment -- the thin lines of the user's report (`three_mirrors.tscn`, plan [4 L, 6 R]): the planned walk applies the entry there and the physical one passes. Whiskers are inclusive and drawn as thin lines.

**Infinity on the carriers.** A straight member parallel to a line carrier meets it at infinity (a refinement member at the switch where the crossing passes through infinity). That crossing is a vertex at infinity at the end the cast reached, as `Q_K` is: the carrier piece leaves the clip rectangle along the carrier, and a connector joins the member's far point.

**Drawing: one generalized polygon.** The renderer draws the whole region as one triangle array (`vis::fill_triangles`, on the worker thread). Drawn cell by cell, two cells tessellated their shared arc independently, and the differing chord points (T-junctions: 106 on `pre_inverted`, 66 on `tangent_infinity`, empty plan) showed as hairline cracks and double-tinted slivers between shapes, mostly with circle inversions (user, 2026-10-17). The fill tessellates every edge on one circle or line -- the same member or carrier, exactly the same canonical geometry -- at the same points: a common angular grid of a whole number of steps per turn, plus every cell corner lying on it. Cells sharing a piece therefore share its vertices, and the cells (simple rings) are triangulated with earcut (`rc/earcut.hpp`) into one watertight mesh. The merged loops are not triangulated directly: their holes pinch at `P_K` and `Q_K`, where every member meets, and ear clipping is unreliable with touching holes. Where a member is nearly tangent to a carrier, two arcs' chords can cross within the tessellation's tolerance though the exact cell is simple: a ring is first cut at such crossings and the lobes wound against it are dropped (`vis::simple_rings`; without it the fill overlapped itself in a thin triangle, user's report on `pre_inverted.tscn`). VIS-DRAWN-MATCHES checks the result; a test checks there are no seams wider than 0.1 px (thin cells where members converge at a pole leave gaps of hundredths of a pixel).

**What stays out.** Projective plan entries (110.5): `BeamTracer.visibility` reports such plans as unsupported and nothing is drawn. The arrow's flight cue at the event limit (§12.6).

## What Is Introduced

- `native/core/include/rc/visibility.hpp`: `vis::compute`, `vis::contains`, `vis::margin`, `vis::fill_triangles`, the builder, the edge geometry and tessellation.
- `native/core/include/rc/earcut.hpp`: polygon triangulation (the earcut algorithm, ISC; holes supported, unused by the fill).
- `VisibilityRegion.fill_triangles()`: `{points, indices}` for the renderer.
- `native/core/include/rc/visibility_checks.hpp`: the pin and the structure checks (included by `invariants.hpp`).
- `Tracer::solve`: one cast (the ladder's events for a float64 cursor, no display conversion, no cache).
- `inv::Input.visibility`, `sweep::check_combo(..., visibility, pin_only)`, `sweep::run(..., visibility)`.
- `native/gd/gd_visibility.{hpp,cpp}`: `VisibilityRegion` (`cell_polygons`, `outlines`, `whisker_polylines`, `contains`, `margin`, `check`, `notes`). Also `BeamTracer.visibility(defs, player, plan_entries, flags, bounds)`, `InvariantKernel.check_visibility_pin`, and `SweepKernel.run(..., visibility)`.
- `scripts/visual/visibility_renderer.gd`: the fill at z 10 (one triangle array), recomputed only when the player, the plan (with clicks and pieces), the flags, the surfaces or a switch change. Debug builds log `LIVE VISIBILITY-EQUALS-NON-DIVERGENCE` where the region and the preview disagree at the cursor.
- `LevelSettings` adds the renderer next to the edge overlay; `FeatureFlags.visibility()`; the V key toggles it.
- Oracle: `docs/research/oracle/visibility.py` (the definition at 50 digits) and `gen_fixtures_stage110_visibility.py` (`bench/fixtures/stage110_visibility.json`: about 1,000 cursors labelled on eight cases).

## Invariants

Per cursor (in `inv::check_all` when a region is given; the visibility sweep runs it alone):
- **VISIBILITY-EQUALS-NON-DIVERGENCE** (UX1 and UX2): `contains(cursor)` is Inside iff the preview reaches the cursor with the merged steps before it ALIGNED. Strict: every disagreement is a violation, `false visible` or `false shadow`, with the cursor's margin to the nearest edge.

Per region (`inv::check_visibility`):
- **Validity:** VIS-REGULAR (the region has area wherever it is: no whisker but the two kinds above, no loop that doubles back on itself), VIS-CLOSED (loops close, no zero-length edge), VIS-SIMPLE (no two edges of a loop cross or overlap, S13), VIS-ORIENTED (cells wind one way, loops have area, an outer loop exists), VIS-DISJOINT (loops neither cross nor share pieces; cells partition the region, S15), VIS-FINITE (S16).
- **Provenance:** VIS-EDGE-ON-GEOMETRY (every edge lies on a member through `P_K` and `Q_K`, on a surface's carrier, on its segment at a stop and at a departure whose entry transforms (a pass or state entry is consumed on its carrier, B1), or on the clip rectangle, S14), VIS-VERTEX-PROVENANCE (every vertex is a pole, on the rectangle, or on a carrier), VIS-SHADOW-ANCHORED (a member boundary piece ends at such anchors), VIS-NO-BLOCKER-INSIDE (no region inside a two-sided blocker that is not planned and not through a pole), VIS-SOURCE-ON-BOUNDARY (departure points belong to the region).
- **Consistency:** VIS-STAR (each cell's middle member is in the region over its intervals), VIS-CELL-CONSTANT (a cast inside a cell has the cell's shape), VIS-ITERATION-CHAIN (departure points are in the region of the plan without its last entry), VIS-BOUNDARY-DECIDED (at every exact point of interest the region decides as the solver), VIS-CONTAINS-MATCHES-POLYGON (the loops and the drawn cells agree away from the boundary), VIS-DRAWN-MATCHES (the drawn triangles cover a point once where the region contains it and never elsewhere; their area is the cells' within the tessellation bound), VIS-DETERMINISTIC (S3).

## Unit Tests Added

| File | Covers |
|---|---|
| `native/tests/test_quiet_entries.cpp`, `tests/test_stage110_quiet_entries.gd` | B1 and B5 at every tier and through the preview; B5 on `hit_effects.tscn` |
| `tests/test_stage110_truncation.gd` | B2: `TracePath.truncated`, the stop marker |
| `native/tests/test_visibility.cpp` | The oracle's labels; the pin on every test level and on the levels' plans; every structure check, and a corruption per check that trips it (with only its listed consequences); VIS-REGULAR's allowed and flagged parts; the drawn fill (one cover, no seams); B3's display cursor index; K8's exact member cursor. `RC_VIS_FULL=1` runs every player and plan (minutes). Skipped debug cases draw one region to a PNG (`RC_VIS_SCENE`, `RC_VIS_PLAN`, `RC_VIS_PLAYER`, `RC_VIS_PNG`) and pin it on a grid (`RC_VIS_PIN=<step>`). Under the sanitizers a third of the levels is sampled |
| `tests/test_stage110_visibility.gd` | The binding on hand-checked rooms, the pin there, the renderer's recompute-on-change and its one triangle array, B3 through the binding |
| `tests/test_visibility_sweep.gd` | The sharded sweep: every test level and playground (no plan), the plan scenes' mirror and inversion plans, the playgrounds' plans, infinity plans |
| `docs/research/oracle/test_fixture_schema.py` | The labels' schema |

## B-list (behaviour changes, decided)

| # | Change | Evidence | Status |
|---|---|---|---|
| B1 | A planned entry the planned walk consumes without changing the frame (a pass side, a terminal crossed off its segment, a write of a flag's current value) is consumed by the physical walk at the same event; divergence is judged by the frame each walk leaves. | Every oracle fixture regenerates identically; the 284,096 pinned sweep traces and every golden replay byte-identical; the new fixtures fail without the fix at every tier. | Done (user: "resolve B1") |
| B2 | A path cut by the event limit draws the spec's stop marker at its end (§12.6) instead of a plain green path that stops short. | Goldens unchanged (the flag is not in the canon). | Done (user: "resolve B2") |
| B4 | The cursor's own point counts (V3): a cursor where the planned walk applies an entry and the physical one passes (a planned surface crossed off its segment) is outside. Before, the region drew such departure points as thin lines along the planned carrier that ignored the cursor and flickered with the player. | The oracle first (`visibility.py`: runs of events at one drawn point, frames after each run); 5 labels flip, all at a planned mirror's end; new label cases `hit_effects_state_plan`, `three_mirrors_departures` (the report's player). The census over the shipped levels (`RC_VIS_FULL=1`): regions with whiskers 94 → 3, carrier whiskers 1,087 → 4 (all on a planned terminal's segment), member whiskers 1 → 1; casts and departure edges unchanged. Every pin and structure check clean; K7 cursors 6 → 5; goldens byte-identical (`event_frames` is not in the canon). | Done (user: "B4 stricter definition") |
| B5 | Surfaces on one carrier are crossed at one point (K6). When the physical walk fires an unplanned surface there that writes what the planned entry writes (`test_levels/hit_effects.tscn`: 15 fires, 16 is planned), it leaves the point in the planned frame: the entry counts as consumed (match by frame, through a shadow of the planned walk over the point). Before, the physical walk unmatched its plan, emitted no cursor event, and the steps after the cursor paired off by one and showed red. | Oracle first (`trace2.py` `_Point`); `gen_fixtures_stage110_frames.py`: 8 rescues and 3 controls, the 8 failing at every tier before the native change; every other fixture, corpus and golden byte-identical (no rescue fires on any of them). | Done (user: "match by frame") |
| B3 | Fixed (F5): the display dropped the zero-length incoming half of a wrap that lies on the bounds' edge without lowering `cursor_index` (`arc::prepare_for_display`), so the index could exceed the drawn steps by one and a step after the cursor was drawn as pre-cursor; the definition, the renderer and NO-POST-CURSOR-DIVERGENCE capped the index at the steps drawn. Now every dropped piece before the cursor lowers the index, and the caps are gone. | `playground/directional_arcs.tscn`, plan ∞, player (500, 300), cursor (1360, 300): index 4 over 3 drawn steps before, 3 after (`test_visibility.cpp` "the display's cursor index counts drawn steps (B3)", `test_stage110_visibility.gd`). | Done (user: "fix the display") |

## Known findings

Both are measure-zero sets: a sweep cursor hits them only when it lies exactly on a carrier or a member. The pin reports K7 with a tag (`inv::pin_tag`), and the sweeps list it instead of failing; K8 is fixed, so a cursor on a member is no longer exempt.

| # | Finding | Evidence | Status |
|---|---|---|---|
| K7 | A cursor exactly on a planned surface's carrier, at a point where a walk departs or ends its lap. The solver reaches it at once in some places and only after the full loop in others, with no change in the casts along the carrier (`test_levels/portals.tscn`, plan 6L 5L: at (960, 600) at once, at (960, 440) after the loop through infinity). In a pre-transform level, a cursor at an earlier entry's reflection point, re-reached at the end of the lap, is emitted at (500, 300) and not at (500, 320) (`playground/pre_inverted.tscn`, plan 2L ∞, player (570, 830)): the cursor-wins tie is not applied consistently there. The region reads a whisker or a departure edge's flag from one cast at a point exactly on the carrier. | Tag "on a planned carrier". The native plans pin (`RC_VIS_FULL=1`): 6 of 64,489 cursors. The GUT visibility sweep: 45 combos. | Open: a solver tie rule to bring to the user |
| K9 | Found by the sweep cursors on the planned carriers beyond their segments (F4): `playground/pre_inverted.tscn`, plan [2 L, 3 L, 4 R], cursor (1900, 880) exactly on surface 4's carrier far off its segment. The invariant sweep reports DIRECTION-ONLY (`hit s=8 vs s=4 without plan`) and PHYSICS-COMPLIANCE (a step of the planned path crossing a wall) from several players. The planned walk applies the first entry off its segment, so the paths diverge at once; a pre-existing solver or invariant edge case, not the region's. | `test_invariant_sweep.gd` `KNOWN_FINDINGS`: 12 violations, one plan and one cursor | Open: to investigate (solver) |
| K8 | A cursor exactly on the member tangent at `Q` to a carrier through `Q`, on its other arc (`playground/tangent_infinity.tscn`, plan ∞, player (760, 440), cursor (800, 420): the member is the circle centred at (835, 540), radius 125). The opposite aim's cast was built from a rounded direction, so it crossed the carrier at `Q` where the exact cursor touches it, and the region excluded a cursor the preview shows. | Fixed (F6): the other way's cursor is the player mirrored in the diameter across the axis `Q` - centre (`tangent_at_pole_other`), kept when it lies on the other arc and an exact test puts it on the circle tangent to the carrier at `Q` (else the rounded aim as before): (910, 440) here, its cast alive, the cursor inside (`test_visibility.cpp` "the member tangent at Q is cast exactly both ways (K8)"). The tag "on a critical member" and the sweep's K8 entry are removed; the GUT sweeps report no such mismatch. | Done (user: "try an exact cursor") |

## Performance

The region's cost on `main.tscn` (12 player positions each, `visibility bench`):

| Plan | Median | Max | Casts per region |
|---|---|---|---|
| none | 6.3 ms | 13.9 ms | 95 |
| one mirror | 15–27 ms | 41 ms | 318–400 |

The region is recomputed only when its inputs change, on a worker thread. The cursor never moves it. `make frame-bench` on `main.tscn` stays within the 8 ms frame budget in every phase:

| Phase | Mean | p95 | Regions computed |
|---|---|---|---|
| static | 0.011 ms | 0.012 ms | 1 |
| cursor_orbit | 0.162 ms | 0.214 ms | 0 |
| plan_orbit | 0.325 ms | 0.464 ms | 1 |
| player_orbit | 0.166 ms | 0.262 ms | 22 |

## Interactive User Tests

1. **Empty room:** on `scenes/test_levels/empty_room.tscn` the room is white. A block (`room_with_mirror.tscn`'s mirror, unplanned) casts an unfilled shadow, and the shadow's edges pass the block's endpoints.
2. **Live update:** moving the player (WASD) moves the region and its shadows, and the cursor alone does not.
3. **Layering:** the fill is under the preview lines and over the surfaces. V hides and shows it.
4. **The promise:** anywhere in the white the preview is all green to the cursor; just outside, it shows red or yellow.
5. **A planned mirror** (`room_with_mirror.tscn`, from the mirror's reflective side) narrows the region to the reflection through the mirror's lit piece. A second planned mirror (`two_mirrors.tscn`) narrows it again. Clearing the plan (C) restores the full region.
6. **Inversion** (`room_with_inversion.tscn`, `playgrounds/plannable_inversion.tscn`): curved regions.
7. **Infinity** (`playground/pre_inverted.tscn`, and an edge click on `empty_room.tscn`): the region through infinity, curved through `Q0` with the pre-transform.
8. **Truncation:** between two facing mirrors (`parallel_mirrors.tscn`) a trapped shot shows the stop marker at the cut.
9. **No thin lines** (B4): with a planned mirror (`three_mirrors.tscn`, plan [4 L, 6 R]) walking the player shows no lines along the mirror's carrier beyond its ends, and nothing flickers.
10. **No seams** (F8): on `pre_inverted.tscn`, `tangent_infinity.tscn` and `plannable_inversion.tscn` the fill shows no hairlines between shapes.
11. **No overlap** (F8): on `pre_inverted.tscn`, empty plan, the player near (1277, 387): no thin triangle drawn twice (darker) in the fill.
12. **Tangent member** (K8): on `tangent_infinity.tscn` with the ∞ plan and the player at (760, 440), the cursor at (800, 420) shows an aligned preview, and the region's thin arc through it from `Q` to the player is drawn.

## Regression Checklist

The full GUT suite, ctest (RelWithDebInfo and ASan), the oracle tests, the level parity (unchanged), the goldens (byte-identical).

## Files Modified

See the stage's commits on `stage110-visibility`.
