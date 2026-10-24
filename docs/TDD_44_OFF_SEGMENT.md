# TDD 44: Off-segment crossings are not events (Stage 139)

The cast-cost program's Stage C (docs/TDD_42_CAST_COST.md, the plan's §3). Stage 137 measured it (B.7): of the
walks' hit events, those off their segment on a surface no remaining plan entry names were hit_effects 42% (its
worst side 53%), crossings 36%, portals 34%, projective 26%, tangent_infinity 17%, inversion_grid 0%. The user's
rule (2026-10-23): "the goal of the walks is to produce the sequence of significant hitpoints; non-significant
hitpoints such as off-segment are a byproduct, but should not be a requirement." The user accepted (2026-10-24)
that their debugging dots go with them.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 139.1 | The rule in GAME_SPEC (12.1, 12.6, 4.3, 14.5, the principles' table) | Done |
| 139.2 | The oracle: the silence after `classify`, `_planned_surface`, the carrier's representative as the roots' source; the tests | Done |
| 139.3 | The Stage 139 fixtures (`gen_fixtures_stage139.py`, `bench/fixtures/stage139_off_segment.json`), the generators that encoded the old rule | Done |
| 139.4 | The native walker: the pre-root filter (`exact::Line::misses_segment`, `misses_circle`), the silence, the margin carry, the roots' source; the tests | Done |
| 139.5 | `make regen`: the fixtures and the corpus | Done |
| 139.6 | The judge (`stage139_judge.py`), the user's approval, the re-bless | Judged; awaiting the user's approval |
| 139.7 | The measurement (regions, casts' time), docs, `make gates`, sign-off | Measured; gates and sign-off pending |

## 139.1 The rule

A candidate's crossing that lies **off its segment**, on a surface **no remaining plan entry names**, is not an
event: not counted by the 64 limit (§12.6), not drawn (§4.3: its half-opacity dot goes), no decision. Everything
else stays: a crossing on the segment that fires nothing (a side that passes, a hit outside a planned piece) is an
event and a dot; a plan surface's crossing is an event on or off its segment, because the plan's bookkeeping, the
quiet entries (B1: a planned terminal crossed off its segment is consumed) and the planned shadow (B5) read it.

The walker may **skip computing** such a crossing where a rigorous test proves it off the segment: for a straight
beam with an exact line, the line's float64 shadow with rigorous bounds (`exact::Line`: `fa, fb, fc` with
`ea, eb, ec`) puts both endpoints strictly on one side (`misses_segment`), or, for an arc's carrier, the line's
distance from the centre strictly beyond the radius (`misses_circle`). A proof or nothing: where the bounds cannot
decide, the crossing is computed and classified at the tier's precision as before (§31.3, no epsilons). Circle
beams (after an inversion) have no bounded shadow of their own and skip nothing: their off-segment crossings are
classified and silenced.

**Margins.** A computed crossing the tier classifies off the segment is a *silent decision*: no event, but the gap
that made it off the segment (`gaps`: the distance to the nearer endpoint, and to the neighbouring candidates) is
carried into the next event's margin (with the event's `why`), so the ladder (§31.5) still climbs where a crossing
lies a hair off an endpoint -- the decision it protects is "nothing happened here", and a wrong one would be a
reflection. A crossing the filter proves off contributes nothing: it was decided rigorously at this tier.

**The roots' source.** The surfaces on one carrier share its roots (K6); the segment they are computed on was the
first candidate's. It is now the carrier's representative's (`carrier_rep`, the lowest id on it), candidate or
not: one deterministic source, whatever the walk's candidates are, so dropping a candidate before its roots changes
no other surface's crossing. A numeric change where the first surface on a carrier was not a candidate (judged).

**Unchanged.** The read horizon (`Ladder::horizon`, two events past what the caller reads) is kept as it is and
measured: with fewer events the two extra events lie further along the walk.

## 139.2-139.4 The implementation

- Oracle (`trace2.py`): in `walk_from`, before the point groups and the count, a surface candidate is classified
  and skipped when off its segment and `_planned_surface` is false (the remaining entries' ids); `_build_cands`
  takes a carrier's roots from `by_id[group]`, the group's lowest id. No pre-root filter: the oracle is the rule,
  the native filter an optimisation whose events are the same (the fixtures) and whose rigour is tested on its own.
- Native (`solver.hpp`): `planned_surface(w, s)` (also `candidate`'s first test); `candidates()` skips a non-plan
  surface the exact line `w.line` proves missed, and computes a carrier's roots on `find(carrier_rep)`;
  `walk_from` classifies a surface candidate first (`pre`), skips it when off its segment and unplanned, carrying
  `gp[ci]` and its `why` into a `Carry` that `fold(ci)` folds into the next event's margin (the origin, infinity,
  cursor, shared-stop and hit emits); the later `classify` reuses `pre`. `exact::Line::misses_segment` and
  `misses_circle` (exact.hpp/cpp) decide by the shadow alone and never ask for the exact coefficients.
- Tests: `test_trace2.py` (`test_off_segment_crossings_do_not_count`, `test_planned_surface_off_segment_stays_an_event`,
  the wrap and junction tests moved onto `thin` surfaces crossed on their segments);
  `native/tests/test_off_segment.cpp`: the 8 fixtures at f64, Dual and 4 limbs, and the randomized proofs that
  `misses_segment` never drops a segment the exact line meets (random exact lines, reflected ones too, segments with
  an endpoint on or an ulp off the line) and that `misses_circle` only fires when the exact distance exceeds the radius.

## 139.5 The fixtures and the corpus

`make regen-apply` (the corpus: 150 of 888 scenarios changed -- their off-segment hits gone -- 414 byte-only files
restored, never committed). Fixture files changed: 13, holding 66 changed fixtures, every one shorter by its
off-segment hits of unplanned surfaces (the counts are events before -> after):

| File | Changed fixtures |
|---|---|
| `stage81_rules` (7) | `partial_inversion_off_segment_physical` 3 -> 1, `partial_blockage_does_not_leak` 3 -> 2, `departure_rehit_off_segment_planned` 21 -> 16, `player_at_arc_endpoint` 3 -> 2, `collinear_endpoint_*` 4 -> 3 / 3 -> 2, `planned_mirror_exact_image_physical` 5 -> 3 |
| `stage82_portals` (3) | `portal_pop_departure_rehit` 15 -> 11 (the exit beam's off-segment pass of the source and, after the pop, of the pair), `portal_arc` 3 -> 2, `physical_through_portal_level` 4 -> 3 |
| `stage83_projective` (3), `stage85_flags` (2), `stage87_projective_plans` (2) | one off-segment hit fewer each (`state_change_off_segment_does_not_fire` 2 -> 1: the non-firing crossing is no event at all) |
| `stage88_infinity` (3) | `wrap_between_ahead_and_wrapped_crossings` on thin walls crossed on their segments (the generator's case); `mirror_infinity_mirror_*` 13 -> 12 |
| `stage101_infinity_point` (5), `stage105_tangent_at_infinity` (5), `stage106_pop_departure` (4) | the pre-transformed scenes' off-segment crossings (`k1_portals_vertical_*` 18 -> 11, `departing_from_q_rehit` 16 -> 11, `straight_beam_concurrent_at_q` 2 -> 1: the mirror 4's carrier crossed off its segment) |
| `stage108_piece_plans` (5) | one fewer each: the other piece's carrier crossing stays only where the plan names the surface |
| `stage110_frame_entries` (7), `stage111_orientation` (14) | the frame-entry and orientation scenes' off-segment crossings (`hit_effects_16R_physical` 7 -> 4, `inversion_arc_image_*` 20 -> 15/16) |
| `perf_circle_ties` (6) | `k9_pre_inverted_760_planned` 28 -> 18, `k7_pre_inverted_1350_planned` 16 -> 11, the tie cases 3 -> 2 |
| `stage139_off_segment` (new, 8) | the mirror beside the ray (physical: no event; planned, two sides: the plan surface's event), seventy walls crossed off their segments (one event: before, the 64th cut the trace), the planned wall consumed quietly off its segment, a thin wall crossed on its segment (an event), a hair (2^-40) off an endpoint, a circle beam's off-segment mirror |

Generators that encoded the old rule: `gen_fixtures_stage110.py`'s `side_toward` probes with a long mirror along the
carrier (its crossing on the segment, an event); `gen_fixtures_stage88.py`'s wrap case uses thin walls crossed on
their segments; `gen_fixtures_stage110_frames.py` follows `side_toward`. Native tests moved onto surfaces crossed on
their segments: `K6: surfaces on one carrier share their crossings` (states on the far side, approached from the
side that passes); `Stage 105: on coincidence-rich scenes` compares fewer events (its floor 4000 -> 3000). The
`DIRECTION-ONLY` invariant compares the hits both walks make events of (on their segments, or applied): a plan
surface's off-segment crossing is an event of the planned shot alone.

## 139.6 The judge

`make judge RULES=stage139` against main@b986b18 (`native/build-ref/judge/report.md`; 121431 re-bless lines):

- **Sweep combos:** 120981 changed; 120941 `offseg-removed` (the new trace is the old one minus hits that fired
  nothing, every other event equal within rounding), 40 `exact-exit`; no `DECISION-CHANGED`, no walk cut by the old
  64 count among the sweeps (`continues-past-max`: 0). The aim is the same in every combo.
- **Fixture and corpus fields:** 450 (corpus 396, fixtures 54): 352 "off-segment hits removed", 35 equal (other
  fields of the same walks), 63 error-bound consequences (`ladder/physical_err`, `planned_err`: the margins fold).
- **Region digests:** unchanged -- `bench/goldens/regions.json` passes as it is: the casts decide by significant
  events, which the rule does not touch. Over every plannable side at six players (`rc_tool regions`), 254 of 256
  plan hashes are equal; the two that differ (`playground/portals [10L]`, `test_levels/portals [7R]`) were dumped on
  both trees: the loops' points are identical and one edge's inclusivity flips at the player's own height, where the
  horizontal cast through the portal pair used to be cut by the 64 count (the off-segment crossings of the other
  carriers filled it) and now reaches the ten significant events.
- **Sessions:** 44 of 47 differ, 862 records. Every difference is in the preview's draw list and the flight: the
  half-opacity dots (radius 2) of off-segment crossings are gone (2621 removed); the dashes and strokes are
  re-cut at fewer step ends (the same geometry: the removed events were vertices of the path), and the
  debugging lane and pass labels follow; where two off-segment crossings tied at one point the old path had a
  zero-length step that broke a pass. The flights end at the same points (the largest landing difference 0.0019 px,
  float32 accumulation over fewer vertices) with the same frame counts (82 flights).
- **GUT rows:** 198 rows in 15 files, every one the old hits minus off-segment unfired ones (physical 180 walks,
  planned 113).

The re-bless after approval: `make rebless APPROVED=1` (sweeps, fixtures, corpus), `make sessions`,
`RC_GUT_TRACES_WRITE`.

## 139.7 The measurement

`rc_tool regions --all` (six players, six threads, three repetitions), main@b986b18 against the branch, paired:

| | main | Stage 139 |
|---|---|---|
| the 256 plans' medians summed | 428 ms | 418 ms |
| plans over 4 ms | 21 | 21 |
| the worst side | 6.73 ms (hit_effects [12L]) | 6.26 ms (crossings [17L]) |
| climbs (BigFloat walks) | 388 | 298 |
| hit events per walk | 0.56 | 0.45 |
| the off-segment unplanned share of hit events (B.7) | 21.9% | 0.1% (plan surfaces') |

Re-timed alone, nine repetitions, two rounds each: hit_effects [12L] 6.08/6.30 -> 5.09/5.09 ms, [12R] 5.66/5.70 ->
4.86/4.88, [14L] 2.41/2.70 -> 2.08/2.11 (the level whose share was 42%: -17 to -20%); inversion_grid and
projective_concentric, which the first run showed slower, are equal within their rounds (noise: 2.6-2.9 both ways).
The pre-root filter removes the roots and the sort slots of most unplanned line crossings; the gain is where
those were (hit_effects, crossings), as Stage 137's measurement predicted (B.7). The read horizon's two extra
events cost nothing measurable, so it is left as it is.

## Tests

| Test | What it checks |
|---|---|
| `test_trace2.py`: `test_off_segment_crossings_do_not_count`, `test_planned_surface_off_segment_stays_an_event`, `test_pass_surface_is_event_but_no_effect`, `test_off_segment_passes_in_physical`, `test_infinity_always_on` | the rule in the oracle: no event, no count; plan surfaces kept; on-segment passes kept |
| `off segment: the Stage 139 fixtures match the oracle at f64, Dual and 4 limbs` | the native walker equals the oracle on the 8 fixtures; the only off-segment events are plan surfaces' |
| `off segment: Line::misses_segment never drops a segment the exact line meets` | the pre-root filter is a proof: 4000 random lines and their reflections, 24000 segments |
| `off segment: Line::misses_circle only when the exact distance exceeds the radius` | the arc carrier's filter is a proof |
| the goldens, the region digests, the sessions, the GUT rows (re-blessed on approval) | the rule's consequences, judged |
