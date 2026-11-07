# TDD 84: Counterpart surfaces -- the preview reaches a planned surface's neighbourhood before it fades (Stage 206)

Your request: "if a circular surface is planned, the ray may never actually reach that surface, if the ray is outside
of the tangent cone ... This is fine, but it interacts strangely with the fading system. The preview system should
always help the player plan their sequence of hits in a smooth manner, such that moving the cursor slightly produces
walks which are similar to the expected ideal walks ... currently when a circle is planned with a linear ray, a grazing
ray will hit the surface, and then an adjacent non grazing ray will fade after the cursor, potentially very far from the
target surface." The idea: "for each surface to have a counterpart which will always have a hit, and when a surface is
planned and not hit at all -- the counterpart hitpoint is used as an off-segment divergence ... The counterpart surface
is the circle which is centered around the ray origin, with a radius equal to the length of the planned surface tangent
... A hit off the counterpart segment means that the original surface had no hits at all ... the ray reaches it and only
then starts to fade." And for a planned line the aim faces away from: "the parallel line to the target planned line,
same distance from the origin but rotated 180 degrees around the origin, making all non parallel rays hit at least one
of the lines without looping through infinity." Generalized "to any mobius frame of reference".

Your decisions (2026-11-07): the counterpart stands in whenever the stretch before the next passage of the frame's
infinity misses the surface, with the loop on or off -- "Infinity is plannable, and passing through infinity unplanned
is a divergence. This means that the preview, fading and planning should all assume that if infinity is not planned,
the player is not interested with it, therefore it should not be previewed"; a planned terminal's counterpart ends the
planned walk, drawn whole to it; a planned-only dot marks the point, like an off-segment crossing of a planned surface;
the implementation "may use an actual carrier definition, to make the hitpoints actual candidates in the solver ...
using as much of the existing framework as possible".

The rules are GAME_SPEC 12.1 (the counterpart: the construction, the event, the dead plan), 4.3 (the fade, the dot),
12.6 (the limits), 13.5 (a planned terminal's), 14.1 (the PLANNED row), 14.5 (the merge).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 206.1 | The spec (GAME_SPEC 4.3, 12.1, 12.6, 13.5, 14.1, 14.5; 2.102.0); the oracle (`trace2.py`: `_counterpart`, `_counterpart_rep`, the `counterpart` event, `Walk.missed`); the fixtures (`gen_fixtures_stage206.py`: 20 walks; Stages 87, 111, 112, 199, 201 and the perf ties re-generated; the corpus) | Done |
| 206.2 | The solver (`Solver::counterpart`, `Solver::counterpart_rep`, the candidates block, the walk's `Counterpart` event, `Walk::missed`), the geometry (`geom::circle_seg`, `line_seg`, the inversions), the gates (`Solver::counterparts`, `Ladder::counterparts`, `Tracer::solve`: the realization's wide budget and the region's casts compute none) | Done |
| 206.3 | The tracer (`TraceStep::counterpart`), the merge (no rule change; the display's snap spares a counterpart's end), the invariants (a planned trace may end at a counterpart) | Done |
| 206.4 | The drawing (`reveal_of`: a counterpart is the first unplanned event; `event_marks`: the planned walk's dot, red, no ring) | Done |
| 206.5 | Tests: the fixtures at f64, Dual and 4 limbs; the region's and the wide budget's walks compute none; the preview's continuity across the tangent cone's edge, the dot, a planned terminal behind the aim; the fade's invariants swept over a mirror ball | Done |
| 206.6 | Goldens: `sweeps.json` (40660 combos, all counterpart-inserted), `fixtures.json` (18 fields), `corpus.json` (490 fields: 485 counterpart-inserted, 5 cut-at-counterpart); the session golden (16 sessions: the preview and draw list alone); the GUT traces (37 rows, the planned walks alone; the ledger annotated); `regions.json` unchanged: the user's approval (2026-11-07) | Done |
| 206.7 | The user's interactive sign-off (2026-11-07: "Works perfectly") | Done |

## Decisions

**The counterpart, in the frame where the beam is a straight line.** With p the beam's current point and Q the frame's
infinity, I is the inversion about Q through p (the identity when Q is the real infinity); S' = I(S). A carrier not
through Q (S' a circle, p outside it): K' is the circle about p with the tangent length as radius -- its arc between the
tangent points is crossed exactly when S' is -- and the counterpart is I(K'). A carrier through Q (S' a line; the
identity frame: a planned line): the point reflection of S' through p, mapped back -- every aim not parallel to the
line meets one of the two. Whether S passes Q is exact through the pole record where there is one, else the tier's
value (as R1's sign falls back). Degenerate (p at Q, a beam departing from Q, the beam departing S's own carrier -- a
tangent length of zero by provenance): none. The construction is Mobius-natural: in world coordinates K is the
Apollonian circle with limit points p and Q through the two points where the pencil's tangent beams touch S.

**The miss, decided by the crossings the walk already has.** The counterpart's segment is never computed: the walk asks,
per loop, whether S's carrier has a root on the stretch from the current point to the next passage of the frame's
infinity (the wrap of a straight beam, or Q; the exact filters -- a tangent touch, a parallel carrier, a tangency at Q --
leave no root). Where it has not, the counterpart's crossings on the stretch are candidates ordered like a surface's,
after the snaps and the pins (a counterpart is no carrier of the scene: no snap, no concurrency tie, no double root).
No new predicate, no epsilon.

**Before infinity, with the loop on or off** (the user). A plan reached only through an unplanned passage of infinity
is not previewed: the realization's walks (a wide budget, geometry) still pick the orientation that crosses a planned
line from its planned side, so an aim facing away from a planned line now meets the line's mirror image through the
player and fades there -- symmetric about the player, as asked -- instead of running to infinity.

**A cursor before a planned terminal comes first** (Stage 201). The terminal's counterpart crossings are candidates from
the loop's start, but one before the cursor is passed: the cursor is reached (on the way to the terminal, or on an aim
that misses it), and past it a counterpart crossing fires -- the planned walk ends there, whole to it (Stage 205's
miss). A crossing behind the cursor fires nothing: the walk runs on as before.

**The plan is dead after a counterpart.** `Walk::missed`: no entry fires, no planned surface is a candidate, the cursor
never comes; the walk goes on in PLANNED mode, passing everything, to the limits -- as a never-reached plan does today.
Not the physical rules: the fade past the counterpart is short, and the simplest tail is the straight one.

**A counterpart coinciding exactly with a carrier of the scene is that carrier** (`counterpart_rep`, K6). The far
mirror's image through a hit on the middle one is the near one in a row of equally spaced mirrors (the corpus's
three_mirrors): the counterpart shares the near mirror's roots, so the crossings there tie exactly and the id orders
them -- the same at every tier, no zero margin for the ladder. Decided on exact data where p is exact: the identity
frame, the beam on an exact line, p the player or the line's crossing of a line departed from. Else no claim.

**Only the preview's planned walk.** PHYSICAL walks compute none (the shot is as it was: the session golden's shots,
flags and bodies are identical; the GUT traces' physical steps too), the realization's decision walks none (the wide
budget, like `infinity_ends`), the region's casts none (`Tracer::solve(..., counterparts=false)`): `regions.json` is
bit-identical.

**The dot.** A counterpart is significant (it cuts a segment, counts toward both limits) but marks no carrier: the
small dot, red (the planned walk's alone, also where the step to it is the walks' common part), not a ring. The display
snaps a step's end onto its surface's carrier; a counterpart step names S but ends off S's carrier, so it is spared.

**A flicker found play-testing (the user's dumps, pulling_1).** A planned terminal missed with the cursor beyond its
counterpart: the counterpart's crossing before the cursor is passed (Stage 201: the cursor comes first), the walk runs
on to infinity and its drawn end is clipped onto the level's edge -- where the physical walk hits the edge wall, the
same drawn point an ulp apart. The truncation marker matched that point against the merged steps' ends by float32
equality, for its colour and its fade, so it flickered between white at full strength and the step's colour faded
away as the cursor moved. It matches as the merge pairs ends now (`near_pt`, the display's "same drawn point"), in
`Session::truncation_markers` and `paint_path`: a display rule, no decision of the solver's.

**Out of scope, noted.** A cursor reached only through an unplanned wrap (the plan spent, the cursor beyond infinity)
is still reached by the planned walk (Stage 111's `mw_*_image` cases): no counterpart exists for the cursor. By the
same principle it would not be previewed; a later stage.

## Tests

`rc_tests`
- `counterpart: the Stage 206 fixtures match the oracle at f64, Dual and 4 limbs` (`bench/fixtures/stage206_counterpart.json`,
  20 walks): the circle outside the cone (off and on, planned and physical), the exact tangency (the counterpart point
  is the tangent point), a grazing hit, the circle behind, the line faced away from (off and on), the parallel line
  (none), infinity planned then the circle (the far crossing after the wrap), a terminal missed with the cursor before
  it (planned: cursor then counterpart; physical: cursor then the wall), a wall before the counterpart, the origin inside
  the planned circle (none), an inverted frame (the point on the inverted beam, at the tangent length in the frame), the
  symmetric mirrors' tie.
- `counterpart: a wide budget (the realization's walks) and the region's solves compute none`.
- The fixtures re-generated: `stage199_infinity_loop` (`planned_entry_the_aim_misses`: a terminal's counterpart ends
  both walks), `stage201_terminal_cursor` (`circle_missed`, `the_other_way`: cursor, counterpart), `stage111_orientation`
  (the pass-side mirror unplanned: the counterpart at x = -200), `stage112_parallel` (the K10 chains with their wraps
  planned, `with_wraps`), `stage87_click_forks` and `perf_circle_ties` (plans reached through an unplanned wrap: missed),
  the corpus (147 scenarios' planned walks).

`rcg_tests` (`test_bodies.cpp`)
- `preview: a planned circle the aim misses runs whole to the tangent length and fades there, continuous across the
  cone's edge`: a mirror ball, the cursor on a small circle about its centre: hits inside the cone, counterpart points
  at the tangent length outside it, the end moving less than 20 px per degree across the edge; the counterpart step whole.
- `preview: a counterpart point gets the planned walk's dot, no ring`.
- `preview: a planned terminal behind the aim: whole to its counterpart, the mirror image through the player`.
- `preview: a truncation marker at the same drawn point as a faded step's end fades with it` (the user's two dumps).
- `session: off, a planned entry the aim misses goes no further than infinity (Stage 199)`: the box (a terminal): the
  cursor first, then its counterpart.
- `preview: the fade's invariants over plans and cursors (Stage 205)`: `check_fade` requires the planned walk whole up to
  a counterpart; a sweep over the mirror ball added.

The oracle: `test_trace2.py` (the pass-side mirror unplanned, the circle normal's far click, the K10 exits with the
wraps planned); the fixture schema (`counterpart`: a surface, a point, nothing applied).
