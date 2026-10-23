# TDD 38: The leftovers (Stage 133)

After Stage 132 the user asked to proceed to the open findings: F23, F24, F21 and F22 (TDD_33, TDD_34), and F26 and
F27 (TDD_36). Along the way, a request from the user's testing of Stage 132.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 133.1 | F23: the region's open boundary chains on the overlapping circles' pieces | Done (337 → 5 violations) |
| 133.2 | A circular portal's marker follows its circle (the user) | Done |
| 133.3 | F23's remaining cells and whisker: a piece's end that rounds a crossing is the crossing (the user's option 1) | Done (5 → 0; F23 closed) |
| 133.4 | F24: DIRECTION-ONLY at a beam exactly through two mirrors' crossing (a checker false positive) | Done (F24 closed) |
| 133.5 | F21: a circle normal's hit point that is floats exactly is exact data (no unconverged climbs) | Done (4 GUT rows re-recorded, approved) |
| 133.6 | F26: the long looping paths' side-by-side lanes (pre_inverted's preview cost) | Done (F26 closed) |
| 133.7 | F27: region casts skip precision on the order of pass-through crossings in the planned walk | Done (F27 closed) |
| 133.8 | F22 (thread scaling) | Open (a later stage); the user's idea -- crossings that cannot be significant are not candidates, the merge by significant segments -- is Stage 134 |

## 133.1 Open boundary chains (F23)

On crossings, the regions of the pieces of circles 19 and 20 (the lens where they overlap) had 337 violations of
the piece-plan visibility sweep: 330 were VIS-CLOSED. The boundary is chained by exact equality of the edges' ends.
Two vertices that are one point were each computed twice and came out about 2·10⁻¹² apart:

- the crossing of the two circles: each carrier's edge ends at its own root on the member through it;
- the player's point (1400, 390), the crossing of line 18 and circle 17 in frame 0: it lies on members whose points
  of interest are its images in the final frame, so the point-of-interest snap does not apply there.

The single-edge "open chains" were fragments of one chain broken at those points.

**The fix** (`Builder::chain`): only where a chain would stop, with no edge starting exactly at its end, the edge
that starts within rounding of the coordinates' scale (1e-9) goes on from there, or the loop closes at its start.
That is the tolerance `drop_slivers` and the point-of-interest snap use. A global weld of all ends was tried first:
it made zero-length edges and changed the joins at pinches (603 violations), so it was dropped.

Regions whose chains closed before are untouched: the region digests, the sweep goldens and the sessions are
unchanged. F23 now has 5 violations, and its entry in `data/sweeps.json` names their two kinds (133.3).

## 133.2 A circular portal's marker

The user: on a circular portal the marker is not a half ring but an arc that meets the portal's circle at both ends,
on the beam's side: the side it came from at the entry, the side it leaves by at the exit. The ring of radius `r`
centred on the portal's circle (radius `R`) meets it at `±acos(-r / 2R)` from the outward normal. The marker is the
ring's arc between those points on the chosen side, closed along the circle. A line portal keeps the half ring.

The test `present: a wrap and a portal are marked by two half rings` checks it on the portals playground's circular
portals 12 and 13. Two sessions' draw lists are re-recorded (playground portals: shots and tour).

## 133.3 A piece's end is the crossing it rounds (F23 closed)

The last 5 F23 violations (4 VIS-CELL-CONSTANT, 1 VIS-REGULAR) came from the pieces' stored ends. A piece of
circle 19 or 20 ends at the float32 rounding of the circles' crossing, (1400, 587.550049), not at the crossing
(1400, 587.550020): 3·10⁻⁵ px off the other carrier. That left sub-pixel slivers between the true crossing (a point
of interest: the member through it) and the stored end (another), where a walk departs from one piece and stops at
the other circle at once.

The user chose to read the end as the crossing (GAME_SPEC 9.7). An end within one float32 ulp, in each coordinate,
of a crossing of its carrier with another surface's carrier is that crossing, computed at the walk's own precision:

- oracle: `_carrier_crossings`, `_piece_end`, `plan_spans_of(spans, surfaces, plan)`;
- native: `Solver::carrier_crossings`, `piece_spans` (resolved once per walk into `Walk::spans`, read by `in_span`),
  and `plan_piece_spans` for the region's piece-end points of interest.

F23 has no violations left, and its known entry is gone from `data/sweeps.json`. Nothing else changed: the
fixtures, the corpus, the goldens, the region digests and the sessions are all identical. Tests:
`test_stage133.py` (oracle) and `crossings: a piece's end that rounds a crossing is the crossing (F23)` (every tier).

## 133.4 DIRECTION-ONLY at a crossing of carriers (F24 closed)

F24 (TDD_34): on crossings, the plan [15 L, the piece (900, 250)–(1000, 450)] with player (1000, 250) and cursor
(0, 540) failed DIRECTION-ONLY ("hit 5 s=16 vs s=18 without plan"). The physical walk reflects off 15, 16, 14 and
10, and then passes **exactly** through the crossing of mirrors 15 and 16 at (1000, 450): the oracle puts the hit
there at 50 digits. The game's walk follows the plan's exact line. It knows the carriers are concurrent there, hits
15 and then 16 at the same point, and orders them by provenance.

The check's reference is a shot aimed at the cursor's image without the plan, (1672, −296), outside the level where
no cursor can be. It decides at a numerical tier and passes beside the crossing, so its next hit is 18.

As with the hit exactly at a segment's endpoint (Stage 131), the comparison ends at a hit of the game's walk within
rounding of a crossing of two carriers. The game's point is stored rounded, so an exact incidence test misses it.
This makes the check more lenient only past such hits. The sweeps, goldens and sessions are unchanged.

The sweep's positions no longer reached the case (they were trimmed in Stage 129). The bounded position rule now
takes `extra` positions, and the piece-plan group lists F24's player and cursor: 26,000 combos, no violation. F24's
known entry is gone from `data/sweeps.json`.

## 133.5 A circle normal's exact float hit (F21 closed)

F21 (TDD_33): on the projective playground with [9 R], four region casts climbed all six tiers and took the 64-limb
walk unconverged. The casts' cursors are line 6's endpoints, (700, 800) and (700, 200). Under circle 9's normal
projection (centre (700, 500), r 250), their back-propagated hit points H are (700, 750) and (700, 250): exactly
where circle 9's carrier crosses line 6. The hits of 6 and 9 there are one point, with a margin of exactly 0.

Stage 125 made a projective's hit point exact data when it is floats exactly (K11), but only for line normal and
directional projections: a circle normal's H is a root. The rule now covers it by proof, not by computation:

- the numeric H, rounded to floats;
- exactly on the circle, from the circle's exact record;
- exactly collinear with the target X and the centre.

H is then exact data (`exact_float_hit`, `_exact_float_hit`). Line 6's crossing there is snapped to it and ties by
id. Full circles keep the earlier rule: they have no data points, and return before it.

**The cost:** on the [9 R] region from the player's start, the unconverged climbs go from 4 to 0, and the BigFloat
walking from 18 ms to 1 ms. At 8 threads the region takes 12 ms instead of 18. On one thread it stays about 60 ms:
the time saved roughly matches what Stage 131's longer walks added.

**What changes:**

- the oracle's K14 test (`test_stage113.py`): at the exact crossing (700, 250) the planned walk passes 6 (unapplied,
  the tie by id) before 9 fires, and physically 6 fires first. The test's point -- the walks fire different
  projectives there, two frames -- stands.
- 4 GUT trace rows (`test_a_circle_normal_fork_follows_the_click`): the same decisions, hits, cursor and agreement.
  The first step's parameter moves by a few ulps (340.5877273185287 → 340.58772731852804): the hit point is now the
  exact float point, not a rounded computation.
- Nothing else: no fixture, corpus entry, sweep golden, region digest or session.

## 133.6 The lanes of a long path (F26 closed)

F26 (TDD_36): since Stage 131 a circle beam laps until its tenth significant event, so pre_inverted's previews grew
to about 130 merged steps. The bench's cursor orbit cost 0.69 ms per frame on average, 5.2 ms at worst. The solve
was not the cost (0.07 ms on average, no climbs). Painting the path was, and almost all of that (4.8 of 5 ms) was the
side-by-side lanes. Those compare every pair of steps (about 8,400 pairs), sampling 15 points of each for a pair on
one carrier, each sample with an `atan2`. Every lap of a circle beam lies on one circle.

`on_one_another` now answers no sooner where no sample could lie inside the other step:

- arcs: their angle ranges on the circle, with a pixel's margin, do not meet;
- lines: neither step's ends could run through the other (more than half a pixel off its line on one side, or short
  of 2 px into its extent at one end).

That runs after the "same ends" rule, which short steps rely on. The lanes are identical: every session's draw list
is unchanged.

**The result:** painting averages 0.22 ms instead of 0.58, and 1.2 ms at worst instead of 5.1. The bench's cursor
orbit on pre_inverted is 0.26 ms on average instead of 0.69, and 1.4 ms at worst instead of 5.2.

## 133.7 The order of pass-through crossings in a cast's planned walk (F27 closed)

F27 (TDD_36): on crossings, the bench's plan [9R piece, 11R piece] from (300, 500) is unreachable, so its region
is empty, yet it cost 26 ms at 8 threads and 72 ms on one. Stage 131's longer walks doubled the cost of each of its
30 climbs (to 30 ms of BigFloat on one thread). 52 of every 60 declined decisions were in planned walks, at the
order of two crossings that fire nothing.

A region cast reads three things of its planned walk: the entries' consumption, the steps before them, and those
steps' frames. None depends on the order of crossings that fire nothing. A region cast's planned walk
(`Ladder::cast_unreliable`, a solve with stops) now takes its first decision needing precision past those crossings
whose neighbours fire nothing either: the next entry or the cursor beside it still counts. The preview and the
firing keep every decision.

Two-idle-laps reading rule (`horizon`): a planned walk that goes round twice on one beam without consuming an entry
repeats itself, so the region reads nothing further.

**Result:** climbs 30 → 11, unconverged 1 → 0, BigFloat 30 → 5 ms; the region 72 → 47 ms on one thread, 26 → 15 ms
at 8. Every region digest, sweep and session is unchanged.
