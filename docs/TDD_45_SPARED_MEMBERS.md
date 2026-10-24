# TDD 45: Spared members (Stage 140)

The cast-cost program's Stage D (docs/TDD_42_CAST_COST.md, the plan's §3). After Stages 137-139 the time of a
region is its casts' CPU, four per member. Stage 137's finding F28: 93 of the 256 measured plans build an **empty
region** at full cost -- a side the player cannot reach first still casts every member. And since Stage 134 a walk
computes only its candidate surfaces, so the points of a surface that is never a candidate cannot change any walk's
shape: their members split cells into two cells of one shape.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 140.0 | The measurement: never-candidate members, off-span tangencies, empty regions and the rule's soundness over 1536 builds | Done (below) |
| 140.1 | The rules in GAME_SPEC 15.3 (spared points; a plan the player cannot start) | Done |
| 140.2 | The native builder: `Surf::never_candidate`, `Builder::spared_surface`, `Builder::cannot_start`, `vis::spare_members()`; the arc-cancellation fix in `merge_cells_of` | Done |
| 140.3 | The tests (`test_spared_members.cpp`): the loops equal the full build's on every test-level region; the rules' cases | Done |
| 140.4 | The judge (region digests: cell counts; sessions: the fill), the user's approval, the re-bless | Done (approved 2026-10-24; re-blessed: 118 region digests, 46 sessions) |
| 140.5 | The measurement after, docs, `make gates`, sign-off | Done (`make gates` ok; the user's interactive sign-off 2026-10-24) |

## 140.0 The measurement

Over every level and plannable side (`rc_tool regions`' set: 256 plans at six players, 1536 builds; the members
listed by a scratch dumper):

| What | Count | Share |
|---|---|---|
| members in all | 208678 | |
| members at points of never-candidate surfaces (endpoints 30080, tangencies 11792; proper crossings: none, a level's bounds cross nothing on-segment) | 41872 | 20.1% of the members (hit_effects 12%, crossings 10%, portals 13%, tangent_infinity and inversion_grid 30%) |
| tangencies to an arc off its span (the plan's D.2) | 0 | dropped: nothing to spare |
| builds whose region is empty | 961 of 1536 | 68.0% of all casts (461214 of 678059) |
| ... caught by the rule (a line with the player strictly on the other side; a circle with the player inside and the entry its outer side; a full circle with the player outside, the entry its inner side, the outer side applying an effect) | 395 | 33.5% of all casts (227302) |
| ... the rule wrong (fired on a non-empty region) | 0 | (an earlier draft that read arcs as lines fired wrongly 14 times: arcs are decided only with the player inside) |
| ... missed: the infinity-only plan `[inf]` (210), an arc first with the player outside (158), the player on the entry's side yet nothing reaches it (158), a pre-transformed scene (38), the player on the carrier (2) | 566 | left to the casts |

The plan's D.3 (sides from mids) is not measured here: F14 found the side casts differ from the mids on 9% of the
members, and a rule that drops them would change regions where they differ; deferred.

## 140.1 The rules

See GAME_SPEC 15.3, "Spared points" and "A plan the player cannot start". Both are proofs about the walks, not
heuristics: a never-candidate surface is computed by no walk (Stage 134), so no walk's shape changes at its points;
and with no pre-transform the physical walk from the player is a straight beam whose every event before the first
entry is a divergence, so the entry must be its first crossing of the carrier from the player's side (the far side
after an odd number of leading infinity entries). The side is decided exactly on the data (BigFloat; the solver's
`geom::side_left` convention through the segment's winding), never when the player lies on the carrier.

## 140.2-140.3 The implementation

- `Surf::never_candidate()` (scene.hpp): both sides pass, no state change, no landing, in the base row and every table
  row; not a target. `Builder::spared_surface(s)` adds "named by no plan entry". The three member-collection loops
  (the aim pencil, the last epoch's points, the earlier epochs' pushed points) skip a spared surface's endpoints,
  tangencies, tangency at Q and crossings (either surface spared).
- `Builder::cannot_start()`: after `prepare()`, before any member is collected; `build()` returns the supported, empty
  result with a note. `vis::spare_members()` turns both rules off for the tests.
- Tests: `spared members: the Stage 140 rules leave every region's loops as the full build's` (every test level, every
  single-entry plan and the empty plan, two players: the loops' and whiskers' edges equal without the member indices,
  the early-empty regions' full builds have no cells); the pure pass-through's points (unplanned: none; planned: back);
  the unreachable side (empty at once, no casts; the reachable side built; an infinity entry first swaps them).

## 140.4 The judge

`make judge` against main@4a2ddef (`native/build-ref/judge/report.md`): no trace changes (0 sweep combos; the walks
are untouched), 118 region digests of 306 changed -- cell counts and the splitting of the loops' edges at the spared
members' crossings. Dumped on both trees and compared with the split pieces merged: 304 regions identical, the two
others differ only in zero-length clip-edge pieces and the splitting of one arc. `test_spared_members.cpp` compares
every test-level region with the full build (`spare_members()` off) by its loops' areas, a grid's membership and the
whiskers' length: 196 regions, all the same shape; 68 empty at once (the full builds found nothing); members
29113 -> 14021.

- **Sweeps:** the invariant and visibility sweeps hold (0 violations). Before the arc-cancellation fix the
  visibility sweeps of `circle_no_walls` failed at the player exactly on the circle mirror (172 violations: VIS-STAR,
  VIS-CONTAINS-MATCHES-POLYGON, VISIBILITY-EQUALS-NON-DIVERGENCE): with the bounds' corners no longer members, the
  disc inside the circle became two half-disc cells whose arcs share both ends run the other way, and
  `merge_cells_of` cancelled them as "one carrier piece two layers share" (it compared the ends alone). The fix:
  on a circle the piece must be the same arc (`on_arc_side`). A pre-existing fragility the extra members hid.
- **Sessions:** 46 of 47 differ, 986 records, every one in the `visibility` layer alone: the fill's tessellation
  (`visibility.fill`, `visibility.n`: the cell counts) -- the fill polygons' total area is identical in every record
  (the worst relative difference 0.0). No other field moves.
- **GUT rows, fixtures, corpus:** unchanged.

The re-bless after approval: `make rebless APPROVED=1` (the 118 region digests), `make sessions`.

## 140.5 The measurement after

`rc_tool regions --all` (six players, six threads, three repetitions), main@4a2ddef against the branch, paired:

| | main | Stage 140 |
|---|---|---|
| the 256 plans' medians summed | 416 ms | 210 ms (0.51x) |
| plans over 4 ms | 21 | 14 |
| the worst side | 6.14 ms (projective [7L], crossings [17R]) | 5.22 ms (crossings [17L]) |
| casts | 115944 | 59377 |
| members | 34780 | 17528 |
| climbs | 298 | 171 |
| the 93 empty plans' medians summed | 172 ms | 59 ms |

By level, the worst side: projective 6.14 -> 4.93, crossings 6.14 -> 5.22, tangent_infinity 6.10 -> 4.19, portals
5.98 -> 4.77, hit_effects 5.45 -> 4.68, two_sided 3.12 -> 2.25, inversion_grid 3.06 -> 2.17, main 2.90 -> 0.98.
The remaining empty plans (59 ms) are the infinity-only plan, arcs with the player outside, pre-transformed scenes and
sides the player is on yet nothing reaches (TDD 140.0): left to the casts.

## Tests

| Test | What it checks |
|---|---|
| `spared members: the Stage 140 rules leave every region's loops as the full build's, on the test levels` | the rules change no region's shape |
| `spared members: a pure pass-through's points are no members, a planned one's are` | the spared-points rule and its plan exception |
| `spared members: a plan the player cannot start is empty at once, and only then` | the early empty on the far side, the build on the near side, the infinity entry's swap |
| the region digests (re-blessed on approval), the structure checks, the non-divergence tests, the K13 scans | the regions' cells merge, the shapes hold |
