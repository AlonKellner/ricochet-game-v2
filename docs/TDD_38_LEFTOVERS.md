# TDD 38: The leftovers (Stage 133)

After Stage 132 the user asked to proceed to the open findings: F23, F24, F21 and F22 (TDD_33, TDD_34), and F26 and
F27 (TDD_36). Along the way, a request from the user's testing of Stage 132.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 133.1 | F23: the region's open boundary chains on the overlapping circles' pieces | Done (337 → 5 violations) |
| 133.2 | A circular portal's marker follows its circle (the user) | Done |
| 133.3 | F23's remaining cells and whisker | Open |
| 133.4 | F24, F21, F22, F26, F27 | Open |

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
