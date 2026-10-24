# TDD 43: Shared points stop the arrow (Stage 138)

After Stage 137 the user saw a circular hairline in the region of crossings [17L piece 0], coming out of the crossing
of the mirror circle 17 and the wall 18. It was Stage 129's third kind of whisker: the member through the planned
piece's end, where the physical walk reflected off the circle exactly at the wall's crossing (the id decided which
of the two surfaces fired) and the planned walk consumed the entry at the piece's end, so the two agreed on that
one line and nowhere beside it. The user (2026-10-24): "I think that endpoints should be excluded as on-segment
hitpoints for effects... In fact, we might even say that an endpoint hitpoint is always blocking, to avoid arrows
going through exactly at shared endpoints." Extended to every shared point and accepted: "I accept your analysis of
the extended second variant, let's go for it."

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 138.1 | The rule in GAME_SPEC (11.1 item 3, 11.4, 12.1, 14.1, 9.7, 10.6, 15.1, 24.2, 31.3) | Done |
| 138.2 | The oracle: `_shared_surfaces`, `_consequence`, the stop; `_junction_blocked` gone; the tests | Done |
| 138.3 | The Stage 138 fixtures (`gen_fixtures_stage138.py`, `bench/fixtures/stage138_shared_points.json`) and the native test | Done |
| 138.4 | The native walker: `shared_surfaces`, the stop; `full_block` gone | Done |
| 138.5 | `make regen`: the fixtures and the corpus | Done |
| 138.6 | The judge (`stage138_judge.py`), the user's approval, the re-bless (goldens, region digests, sessions, GUT rows) | Done (approved 2026-10-24; re-blessed: sweeps 4030 slots, fixtures 9, regions 42, 2 sessions, 13 GUT rows) |
| 138.7 | Docs, `make gates`, sign-off | Pending |

## 138.1 The rule

A hit point is **shared** when two or more present surfaces hold it: an exact endpoint of one on the segment of
another (a T-junction), coinciding endpoints (a corner), or a proper crossing, interior to both (a planned piece's
end). The walk knows a shared point exactly, never by distance: by provenance (a known point; an endpoint equal by
data) or by an exact tie (two crossings at one parameter). At a shared point the hit belongs to the surfaces whose
approached side has a **consequence** there -- an effect other than a pass, a state change, a landing effect, a
target: exactly a candidate's side (Stage 134). Two or more: no single surface owns the hit, and nothing is chosen
by the id -- the hit is **terminal** in the physical walk, nothing fires, and the event carries the lowest id among
them. Exactly one: it owns the hit, a solid hit of that surface. A **lone endpoint** -- no other present surface
holds it -- is a graze, as before.

Three refinements fell out of the fixtures:

- **The planned walk is unchanged.** It computes its entries' surfaces alone (Stage 134), so it cannot know a point
  is shared with a surface it does not compute; making it depend on whether the other surface happens to be planned
  would be inconsistent. It follows its plan; the physical walk stops; the walks diverge there, and a cursor beyond
  such a point is outside the region. That is what removes the hairline's member: its planned walk consumes the
  piece's entry at the cut, its physical walk stops.
- **A pure pass-through holds no point.** It is never computed (Stage 134), so a mirror crossing a pure pass reflects
  as before: the mirror alone has a consequence and owns the hit.
- **Togglers count.** A mirror crossing a toggler, or two togglers crossing, are two consequences at one point: the
  arrow stops and neither fires. Letting the owner fire and the toggler's change fire "too" turned out to depend on
  the id again (the owner's effect restarts the loop before the toggler's candidate is processed).

## 138.2-138.4 The implementation

- Oracle (`trace2.py`): `classify` returns the hit's endpoint index instead of the junction's blockage;
  `_shared_surfaces` lists the other present surfaces holding the point (a candidate at the same parameter on its
  segment; an endpoint equal to the point); `_consequence` is `_candidate`'s side test; the stop is emitted before
  the mode's effect decision. `_junction_blocked` and the `endpoint_blocked_sides` import are gone (the geometry
  kernel keeps the function for its golden).
- Native (`solver.hpp`): `Classified::at_ep` replaces `full_block`; `shared_surfaces` mirrors the oracle's over the
  loop's candidates (`t.cmp == 0`) and the scene's endpoints (`geom::endpoint_at`); the stop emits one applied,
  terminal, non-firing hit and ends the walk; `solid = on_seg && (at_ep == 0 || shared)`.
- Tests: `test_trace2.py` (the corner, the T-junction, the crossings, the togglers, the lone endpoint; the circle
  beam's concurrent crossings now stop), `native/tests/test_shared_points.cpp` on the 14 Stage 138 fixtures at f64,
  Dual and 4 limbs.

## 138.5 The fixtures and the corpus

`make regen-apply` (the corpus: 888 scenarios identical; no byte-only file committed). The fixtures the rule changes, each
checked by its generator's updated assertion:

| Fixture | Before | After |
|---|---|---|
| `stage81_rules/y_junction_fires_lower_id`, `cursor_at_junction_physical`, `cursor_at_junction_planned` | the lowest id fired at the Y junction, the walk went on to the wall | one terminal hit of the lowest id (1), nothing fires (the planned one has no plan: it is the physical walk) |
| `stage105_tangent_at_infinity/straight_beam_concurrent_at_q` | the mirror (2) and the flag-writing line (3) concurrent at Q: 15 events, laps through Q | 2 events: the stop at Q, carrying id 2 |
| `stage107_hit_effects/presence_junction_both` | the shared endpoint of two collinear mirrors, both present: the upper fired | a terminal, non-firing hit; `presence_junction_absent` (a lone endpoint) grazes as before |
| `stage110_frame_entries/two_writes_one_entry_physical` | two writers crossing at one point both wrote; the cursor was reached (a rescue) | the walk stops at their crossing (1 event), no write, no rescue (`rescued=False`) |
| `stage110_visibility` | the pins at the room's corners (560, 240), (620, 180), (560, 840) inside | outside: a ray exactly through a corner no longer passes |
| `perf_circle_ties/circle_beam_tie_falling_low`, `_rising_low` | two mirrors tied on a circle beam: the lowest id reflected, 7 events | the stop, 3 events |
| `stage138_shared_points` (new, 14) | -- | the corner, the T-junction, the crossings (mirror+wall, mirror+toggler, two togglers, mirror+pure pass), the lone endpoint; physical and planned |

`test_quiet_entries.cpp` ("the preview's two paths agree up to the cursor") skips `two_writes_one_entry` with the other
diverging controls (7 pairs).

## 138.6 The judge

`make judge RULES=stage138` against main@2e3c27e (`native/build-ref/judge/report.md`, 4081 re-bless lines):

- **Sweep combos:** 4030 changed, every one `shared-point-stop` (the new trace is the old one's prefix, then one
  terminal hit at the same point; the aim equal in all 4030; the planned tiers unchanged). No `DECISION-CHANGED`.
- **Fixture fields:** 9 (the three `stage81_rules` fixtures at f64, fast and 4 limbs: the native walk equals the
  regenerated fixture).
- **Region digests:** 42. Forty are the empty plan and the infinity plan at (500, 300) on every test level and
  playground: dumped on both trees, the loops' points are identical and only the inclusivity of the edges through the
  room's corners flips (the crit cast through a corner ended at Q before -- both endpoints grazed -- and stops at the
  corner now). `playground__portals [5L]` and `crossings [9L]` at (960, 540): the same, at the portals' shared
  endpoint (400, 550) and the cut (600, 330). `rc_tool regions` over every plannable side (6 players): 182 plan hashes
  equal, 6 differ, all on crossings ([9R], [11L], [11R], [14R], [15R], [17L]), the two plans whose pieces end at a
  crossing (9 x 10 at (600, 330), 17 x 18 at (1400, 390)) and the plans whose walks reach those cuts.
- **The hairline** (the F12 dump of 2026-10-24T11-04-00, crossings [17L piece 0], player (1436.67, 500)): replayed
  on both trees, main has the one whisker from (1400, 390); the candidate has none, same cells, members and casts.
- **Sessions:** 2 of 47 differ, both on crossings at the click planning [9R piece 0] from (300, 500):
  `test_levels__crossings__shots` (3 of 38 records) and `__tour` (2 of 55). The region loses one whisker, from the cut
  (600, 330) to (200, 103.3) -- the hairline's kind. In `shots` the arrow aimed at (1180, 520) used to pass the
  crossing of mirrors 15 and 16 at (1000, 450) (15 fired, 16 re-applied at the same point) and land on the wall at
  (902.9, 900) after 23 frames; now it stops at (1000, 450) after 21.
- **GUT rows:** 13 in 3 files. `test_side_hitpoints` (7): walks through the room's corner (560, 840), where the
  two walls both applied and the walk lapped five times before, now one terminal hit, `fired` false.
  `test_stage113_projective_visibility` (4): the terminal hit at the corners (0, 600) and (0, 0) no longer fires.
  `test_stage72_rigid_motion_integration` (2): two collinear portals overlapping along x = 200 (a test-only
  construction; no shipped level has overlapping collinear surfaces) -- every point of the overlap is shared, the
  arrow stops at (200, 300) where portal 1 used to fire.

The re-bless after approval: `make rebless APPROVED=1`, `make sessions`, `RC_GUT_TRACES_WRITE`, `RC_REGIONS_WRITE`
(the digest's 42 regions are in the re-bless lines).

## Tests

| Test | What it checks |
|---|---|
| `test_trace2.py`: `test_corner_stops_the_arrow`, `test_t_junction_stops_the_arrow`, `test_crossing_stops_the_physical_walk`, `test_shared_point_of_two_togglers_stops_the_arrow`, `test_lone_endpoint_grazes`, `test_circle_beam_concurrent_crossings_tie_by_id` | the rule in the oracle |
| `shared points: the Stage 138 fixtures match the oracle at f64, Dual and 4 limbs` | the native walker equals the oracle on 14 fixtures; 5 physical stops |
| the goldens, the region digests, the sessions, the GUT rows (re-blessed on approval) | the rule's consequences, judged |
