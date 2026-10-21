# TDD 28: Regions on time -- ties by provenance, a cast's horizon, the region drawn as one shape (Stage 123)

The fourth stage of the smooth-frames program (plan approved by the user, 2026-10-20). Stage 122 smooths
what arrives late; the user's rule since then is that the smoothing must not be needed: "I highly prefer to
avoid using this mechanism in normal circumstance. Ideally, the preview and visibility calculations will be
optimized to be fast enough for the required frame rate, and this smoothing algorithm will not even
trigger" (2026-10-21). This stage makes the solver and the region fast enough for that, and fixes what the
user saw on screen meanwhile (hairline overlaps and cracks).

The decisions this stage follows (user, 2026-10-21):

- **At least 120 Hz.** A frame is 8.3 ms. The Godot host waits up to 4 ms for the work a frame starts
  (`solve_budget_ms`).
- **Points equal by definition are decided by provenance.** "If two points are known to be equal by
  definition (i.e wall corners, or any shared endpoints between surfaces), that case should not cause a
  precision ladder escalation, but be handled deterministically by provenance."
- **Keep the drawing exact.** No closing of sub-pixel slivers: a shadow's thin tip is true geometry.

The path morph that TDD_27 planned for this stage moves to a later one: the user put speed first.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 123.1 | Ties by provenance: one tie group for every carrier through a point; a cursor at a known point is that point; the instrument of why a walk climbs (re-blessed, approved) | Done |
| 123.2 | A BigFloat tier measures a near-zero gap at its own precision; the unconverged climbs counted | Done |
| 123.3 | A region that fits the budget cancels its stale predecessor | Done |
| 123.4 | The region drawn as the union of its cells (Clipper2); the morph diffs its outline (re-recorded, approved) | Done |
| 123.5 | The Godot host's solve budget: 4 ms | Done |
| 123.6 | A morph frame keeps lit what both its shapes cover | Done |
| 123.7 | Incidence memoized across walks; the region build's phases and batches timed | Done |
| 123.8 | A region's cast tells the ladder what it reads: no climb for a decision past it (re-blessed, approved) | Done |
| 123.9 | A region cast's climb walks only a little past what it reads | Done |
| 123.10 | An endpoint member's casts know the endpoint; one carrier's shared root is one decision (re-blessed, approved) | Done |
| 123.11 | Docs, `make gates`, sign-off | In progress |

## Where a region's time went

The region is built from casts: ordinary solves aimed along the region's members, their sides and the gaps
between them (GAME_SPEC §15.3). They run in parallel batches on the thread pool, and each batch waits for its
slowest cast. Stage 123.7 timed the phases and the batches. On every level the slowest cast was nearly the
whole phase, and it was always a precision climb: a decision whose float64 margin was too small for the
ladder, walked again at 4 to 64 BigFloat limbs.

The instrument of 123.1 (`Solver::gaps` records what set each margin, the ladder counts the declined
decisions by it, `LadderStats::why*`) sorted the climbs into a few classes. Every class turned out to be a
decision the walk could make without precision: either the two decisions are one by construction, or the
cast never reads the decision at all.

## 123.1 Ties by provenance

On an exact line, adjacent crossings of line carriers that meet the beam at one point share one parameter
and are ordered by id (`concurrent_ties`). Three carriers at one point (hit_effects' corners) made two pairs.
A pair whose first was already grouped started a new group, so the middle carrier sat a margin of exactly 0
from its neighbour group and the walk climbed to 64 limbs. Now a pair whose first is grouped joins the
group: no margin inside it.

A cursor that is a known point (a segment's endpoint, a shared corner) takes that point's exact parameter and
position, the cursor event included. The carriers through it tie with it by rule. Before, it landed a
rounding off the corner and drew a 1e-13 px step between two red endpoint marks.

hit_effects, 40 walking steps: 64-limb walks 4849 → 20, BigFloat time 16 s → 0.12 s, previews climbing
34/40 → 1/40. Judged and approved: only cursor events moved (to exactly the cursor, at most 1.9e-12 px), four
hit_effects regions structurally identical, the corner steps gone from 16 sessions.

## 123.2 Near-zero gaps at the tier's precision

The margins were measured between float64 views of the beam at every tier. Two hits a few 1e-13 px apart
stayed a rounding apart at 64 limbs, so the climb never converged. At a BigFloat tier a gap within float64's
reach of 0 is now measured at the tier's own precision. crossings: 64-limb walks 83 → 0, BigFloat
723 → 48 ms. Every result is unchanged (the same decisions, reached at 4 limbs).

## 123.3 and 123.5 The budget

When regions typically fit the frame's budget, a new region request cancels the running older one: the
frame waits for the new one anyway. Slower regions keep the running one, so a walking player still sees
regions arrive. The Godot host's budget is 4 ms of the 8.3 ms frame.

## 123.4 and 123.6 The region drawn as one shape

The user saw hairline overlaps and cracks. Measured at pixel centres (the GPU's top-left rule), the fill
drawn cell by cell covered pixels twice along the cells' seams (main 16, crossings 60, tangent_infinity 32
per 6 frames). The per-cell morph was far worse (tangent_infinity: 36,635 pixels twice and 11,079 cracks per
18 frames).

- `rc::vis::union_outline` (`rc/region_union.hpp`) unites the cells' conforming rings with Clipper2
  (vendored in `native/extern/clipper2`, Boost Software License) on a 1e-4 px grid: outer rings and holes.
  `fill_of` triangulates them. It runs in the region's job (`Session::region_outline`).
- The presenter morphs the outline's rings.
- A morph frame is united with the intersection of its two shapes (123.6): what both cover stays lit, so a
  morph can open no slit there.

After: no pixel is covered twice on any level, still or morphing. The cracks left are the region's own thin
features (shadow tips, thin slivers), at the same rate as in still frames; the user chose to keep them
exact. Judged and approved: the visibility draw node in 48 sessions (area equal to 1.2e-5), everything else
unchanged.

## 123.8 and 123.9 A cast's horizon

A cast reads its walks only up to a point (`cast_core`):

- the physical walk through its stop after the plan's K entries: the first hit past them that is applied
  or changes the flags (the far end of the member's visible stretch);
- the planned walk through its cursor and the events at the cursor's point.

The ladder required every event of both walks to be reliable. A side cast beside a tangency (1e-6 rad off a
member tangent to a circle) cuts the circle in a sub-pixel chord and bounces inside it up to the hit limit,
30 events, each a margin of 2.5e-4 px with an error estimate amplified along the chain. That cast climbed to
64 limbs, about 14 ms, for events nothing reads.

`Ladder::solve(..., stops)` now takes what the caller reads (`stops = K`, from the region only). The
reliability and the tolerance are required only for that prefix (`Ladder::horizon`). A walk with no such stop
is read whole. A climb's walks are cut a couple of crossings past the horizon (123.9), and walked whole again
when the cut walk holds no stop.

Previews and firing pass no horizon: unchanged. Two region digests changed (approved): one or two arcs' via
points by about 1e-11 px, the topology and every endpoint bit-identical.

## 123.10 Two more ties by provenance

- **An endpoint member's casts.** An aim member found at a segment's endpoint in the player's frame passes it
  by construction, but its cast the other way aims at a rounded point, so its beam missed the endpoint by
  about 1e-13 px and the ladder climbed to decide which side the rounding fell on. Both of the member's casts
  now carry the endpoint (`Solver::aim_known`): it is a known point of the walk's first beam when the beam
  passes it to rounding, and the hit there is decided by identity. hit_effects with a plan: climbs per 120
  regions 8420 → about 860.
- **One carrier's shared root.** Surfaces on one carrier cross it at one shared root (K6). Off an exact beam
  (after a reflection) `gaps` counted them as neighbours at distance 0, and each such cast climbed to 64 limbs
  (15–25 ms). They are one decision now, ordered by id. hit_effects' previews never climb any more (TDD_25
  F7); `async: a preview that must climb...` moved to inversion_grid, whose previews still climb at a few
  cursors.

Four region digests changed (approved): only the inclusive flags of one boundary ray each (a ray aimed
exactly through an endpoint: its membership now follows the endpoint rule, not the rounding).

## Results

`make perf` (120 frames a phase, ms of game time per frame, means), Stage 120's report against this stage's:

| Level | Phase | Stage 120 | Stage 123 |
|---|---|---|---|
| hit_effects | walk | 71.8 | 1.6 |
| hit_effects | player orbit | 73.3 | 1.6 |
| hit_effects | cursor orbit | 4.44 | 0.06 |
| hit_effects | plan click | 1270 | 13.8 |
| tangent_infinity | walk | 19.8 | 4.2 |
| crossings | walk | 19.6 | 2.1 |
| inversion_grid | walk | 3.4 | 2.1 |
| inversion_grid | plan click | 54.3 | 33.3 |
| crossings | plan click | 56.8 | 32.4 |

A region with no plan, built while walking (the measurement tool of 123.7, 120 regions, 8 threads): under the
4 ms budget on every level. The worst are tangent_infinity 3.6 ms and inversion_grid 3.1 ms. Before this stage
they were tangent_infinity 20.4, inversion_grid 5.0 and hit_effects 114 ms.

## Findings

| # | Finding | Status |
|---|---|---|
| F7 (TDD_25) | hit_effects: every cursor frame's preview declined float64 on a tie; its region climbed about 460 times per plan change | Resolved (123.1, 123.10) |
| F11 (TDD_27) | Mid-morph, overlapping cells' shared edges could part or overlap by a hairline | Resolved (123.4: one outline) |
| F12 | A region with a plan is still over the budget on 14 levels while walking: inversion_grid 35 ms, crossings 26, projective_concentric 20, full_circle_mirror 15, hit_effects 13, semi_circle_mirror 13, and 4–11 elsewhere. It is cast throughput, not climbs: 580 casts a region on inversion_grid (1400 on crossings), about 134 ms of single-thread work, 3.8× on 8 threads. malloc/free are about 25% of it, and each cast's exact circle after the plan's inversion (K7's test of the cursor on the beam) about 28% | Open: these regions are late, so the presenter smooths them. Next: the walks' allocations and the exact circles shared between a cast's walks |
| F13 | Provenance for a cast aimed at the crossing of two circles gains nothing: after bouncing, the beam comes back to the same irrational point, and no provenance follows it there | Recorded (tried in 123.10, removed) |

## Tests

| Test | What it checks |
|---|---|
| `visibility: the regions' digest matches bench/goldens/regions.json` | every region of the digest's set, bit for bit (re-blessed twice, approved) |
| `async: a preview that must climb shows provisionally, then the exact one` | now on inversion_grid (hit_effects no longer climbs) |
| `present: the region is drawn as the union of its cells: no pixel twice, no crack, still or morphing` (123.4) | a rasterised fill and morph frames on 5 levels |
| `present: a region's outline is its fill` (123.4) | the outline's triangulation covers what the cells cover |
| the sweeps, the session golden, the goldens | unchanged by 123.2, 123.3, 123.7, 123.9; judged and approved for 123.1, 123.4, 123.8, 123.10 |
