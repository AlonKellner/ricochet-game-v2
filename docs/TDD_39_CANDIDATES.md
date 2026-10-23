# TDD 39: Candidate surfaces and the merge by segments (Stage 134)

After Stage 133 the user asked why most planned walks reach the 64-event limit, and proposed that a planned walk
compute only its planned surfaces while it waits on them. The user's design, in their words:

- "If a surface may change the state of the game if it's hit at a certain time, it must be a candidate surface. If
  even when hit, the surface won't have any effect on the game or the path or any significance -- the surface should
  not even be a candidate." The same holds in ghost mode.
- "The paths are still separated by significant events ... reduce the merge work to the full straight segments
  without a significant event."
- The 64 limit is a protection, not a game rule: it counts computed crossings, and goldens of paths that reach it may
  change.
- The crossings that fire nothing keep their dots, for debugging, coloured by the walk that computed them: yellow the
  physical walk, red the planned walk, green both.
- During the stage: no lap detection anywhere -- every lap passes the frame's infinity and the ray's origin, both
  significant, so a walk ends within five laps.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 134.1 | Candidates in the oracle and natively; the rebuild where the walk's state changes | Done |
| 134.2 | Visibility by significant points (the oracle's `visible`, the region's casts, the preview) | Done |
| 134.3 | The merge by segments; the coloured dots | Done |
| 134.4 | The invariants restated for segments | Done |
| 134.5 | Bugs the corpus and the fixtures found | Done |
| 134.6 | The user's report: ghost_room's region inverted after an origin entry | Done |
| 134.7 | No lap detection (the user) | Done |
| 134.8 | Judge, approval, re-bless | Done (approved) |
| 134.9 | A side whose hit changes the game at the landing is a candidate | Done |

## 134.1 Candidates

A surface's crossings are computed only where hitting it could matter (`_candidate`, `Solver::candidate`; GAME_SPEC
12.1):

- **PLANNED** (before the cursor): the surfaces of the plan's remaining entries;
- **PHYSICAL** (and the planned walk past its cursor): a surface present under the walk's flags with a side whose
  effect is not a pass (ghost-aware) or which changes the state, a target, and the remaining entries' surfaces;
- always: the passages of the frame's infinity and of the ray's origin, and the cursor.

Where the walk's state changes at an event -- its flags, an entry consumed, the cursor -- the candidates are rebuilt
for the rest of the loop, keeping those past the last one processed. A rebuild reuses the loop's carrier roots, so a
carrier's crossing stays one point (K6).

**The consequence the user approved:** a planned walk waiting on its entries no longer fires a state change no entry
plans. The physical walk does, so that state change is an unplanned significant event before the cursor: a
divergence, like an unplanned infinity. To keep a cursor beyond it visible, plan the toggler.

## 134.2 Visibility by significant points

The two walks compute different candidates, so they no longer correspond event by event. They are compared by their
**significant points**: the runs of events at one drawn point that hold a significant event, each with the frame the
run leaves it in. By point, because surfaces crossed at one point in another order, or one write for two, leave it in
one frame (Stage 110's B5).

- **V1:** both walks reach the cursor. A physical walk that ends at a terminal at the cursor's own point reaches it.
- **V2:** the same significant points before the cursor.
- **V3:** both walks leave the cursor's point in one frame.

The oracle's `visible` and native `walks_agree_at_cursor` (rc/tracer.hpp, beside `CastEvent`) are the same rule. The
region's casts find the departure by significant points too: the planned walk's consumption of the last entry, and
the physical event that is it. A step ends at a significant point when any event at its end is significant (an event
at the same point draws nothing).

## 134.3 The merge by segments

`sweep::merge_segments` cuts each display path into segments: runs of steps between significant events, or between
frame changes. It pairs them in order.

- **A pair alike** (one frame, one start and end, both significant or neither) is drawn once, from the planned path.
  A passage of infinity is one end wherever each walk's step toward it is clipped (`TraceStep::to_infinity`, kept by
  the display's clipping), and one start where the steps come back.
- **At the first pair not alike**, the walks run together to the nearer of their two ends (`fraction_on`), drawn from
  the walk whose end that is. Past it, each walk is drawn on its own.
- **Dots:** every other crossing either walk computed is an `EventDot`, coloured by the walks that computed it
  (`Session::Preview::dots`, `PathPainter::event_marks`).

## 134.4 The invariants

- **PHYSICAL-PREVIEW-MATCH:** the physical path's significant points and its end, each in order the end of a non-red
  step, and the non-red steps one unbroken route.
- **GREEN-FROM-PLAYER:** holds unless the walks leave the player apart (a realization the other way round a circle).
- **NO-POST-CURSOR-DIVERGENCE:** "aligned to the cursor" is no planned step diverged, and a tail from the end of a
  walk the limits cut is not a divergence.
- **preview_visible:** no planned step diverged, with the walks' agreement (V1-V3).

## 134.5 Bugs found on the way

Oracle:

- the rebuild did not run after the loop's last candidate (the cursor last in PLANNED mode), so crossings between the
  cursor and the departure were skipped;
- a rebuild recomputed a carrier's roots, which can round apart from the loop's shared ones;
- an empty side type (the GUT captures') is a pass, as the game reads it.

Native:

- after an in-place reseed the loop resumed by candidate index, not by order;
- a cursor at the departure point is first, not last;
- the cursor is listed while the shadow point is pending, as the oracle lists it;
- the planned-from-physical shortcut is taken only where the physical walk computed nothing the planned walk would not.

## 134.6 The user's report: ghost_room [6 L, 11 R, ∞, 12 R, O]

The beam departs mirror 6, then passes the togglers, the wrap and the ray's origin (the player's image, (660, 660)) on
one beam, and comes back to mirror 6, its departure. The region read that re-hit as the end of a full loop from the
cast's start. After an origin entry the cast starts at the origin, though, and the re-hit lies ahead of it. On those
members the region therefore held everything except the stretch from the origin to the mirror: a cursor before the
mirror (visible) was outside, and one past it (diverged) inside.

The bug came with Stage 132. The fix: a re-hit is a plain stop. Test: `region reports: ghost_room [6 L, 11 R, inf,
12 R, origin]`. On the user's plan the region now agrees with the preview on all 5,184 cursors of a 20 px grid.

## 134.7 No lap detection

The user: every lap passes the frame's infinity and the ray's origin, both significant, so a walk ends within five
laps. Lap detection should not exist. Removed:

- the walker's empty-lap rule (a lap that computed nothing ended with an origin passage made up at its departure). A
  loop that passes nothing significant is now an invariant violation: `RC_ASSERT`, an assert in trace2.py. Neither
  fired anywhere.
- the events' `lap` counter;
- the region's lap reading: infinity a stop only on the first lap, a second lap that "closes the loop", a re-hit read
  as a full loop. The far end is the first frame-changing hit, passage of infinity or passage of the origin past the
  departure, else the cut;
- F27's two-idle-laps horizon.

Nothing changed: the sweep traces, the sessions, the GUT rows, the fixtures and the region digests are byte-identical
to the build before the removal, and the bench counts are equal.

## 134.8 What changed (judged and approved)

The judge's rules are in `docs/research/stage134_judge.py`; they replay flags from the sweeps' definitions.

| Record | Changed | Classes |
|---|---|---|
| Sweep combos | 225,333 | non-candidates' crossings dropped 224,955; the planned walk skips an unplanned state change 324; a physical walk off the plan no longer reaches the cursor 54 (visibility unchanged) |
| Fixture and corpus fields | 2,117 | unapplied crossings dropped 1,834; their error bounds 269 |
| Region digests | 14 | identical boundaries bit for bit; two refinement members no longer needed (renumbered) |
| GUT trace rows | 521 | non-firing crossings dropped 404; the agreement field (now V1-V3, was V3) 116; pure passes only 1 |
| Sessions | 48 of 48 (1,406 of 1,414 records) | fewer preview events; the draw lists (segments, dots); `facing` one float32 ulp (3 hit_effects sessions again in 134.9) |

## 134.9 Deferred hit effects (found by the commit gate)

The sweep manifests are the levels' worlds after scripted shots. On hit_effects, surface 20 is a pass with no in-shot
state change, but its hit has a deferred effect: a reset of channels e-h, applied at the landing from the shot's hit
records. To the walk it looked like a pure pass, so it was no candidate and the landing never saw its hit.

By the user's rule (a surface whose hit may change the game is a candidate), a side with a deferred hit effect now is
one, ghost or not (a superset is safe). Implementation: `SideDef::landing`, set by the game's builder, written to the
definitions' JSON only when true (older definitions read back unchanged), `Surf::landing`, and the oracle's
`level_json` and `_candidate`.

- **Consequences:** the manifests are as before plus the flag; 3 hit_effects sessions gain that crossing back (one
  more preview event; the world's definitions digest); the hit_effects corpus scenarios and one fixture carry the
  flag.
- **Not changed:** the sweep goldens keep their frozen inputs (definitions from the GDScript era, without the flag).
- **NO-POST-CURSOR-DIVERGENCE:** a cut at a wrap is a cut too: the tail after the cut walk's last step, which ends
  where the beam leaves toward infinity, starts on the far side.

## The cost

`rc_tool bench --all`, against the tip of Stage 133:

- plan clicks 10-53% faster (crossings 9.9 → 7.4 ms, inversion_grid 7.3 → 5.7, two_sided_mirror 1.8 → 0.85);
- plan orbits up to 45% faster;
- player orbits 5-38% faster.
