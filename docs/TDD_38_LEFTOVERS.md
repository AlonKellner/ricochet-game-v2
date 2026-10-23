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
| 133.5 | F21, F22, F26, F27 | Open |

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
