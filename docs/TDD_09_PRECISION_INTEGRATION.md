# TDD Document 9: Precision Integration, Legacy Deprecation, State Flags & Ghost Mode

**Stages 80--87** | Bring every surface type onto the precision beam solver
(`scripts/math/precision/`), delete the legacy frame-based `Tracer`, implement
the state system (§9.4, §10.6), add ghost mode, and make projectives plannable
(§13.3/13.4).

Background and benchmark data: `docs/precision_benchmark_findings.md`.

### Stage Status

| Stage | Topic | Status |
|-------|-------|--------|
| 80 | Neutral trace types, generalized beam geometry, oracle v2 | Done (awaiting doc confirmation) |
| 81 | Reflections, partial/one-sided inversions, terminals, passes | Done (user sign-off) |
| 82 | Portals (rigid motion) | Done (user sign-off) |
| 83 | Projective effects (physical) + full switch-over | Done (user sign-off) |
| 84 | Delete the legacy solver | Done (user sign-off) |
| 85 | State flags (ConfigResolver + StateChange) | Done (user sign-off) |
| 86 | Ghost mode & toggler surfaces | Done (user sign-off) |
| 87 | Plannable projectives (mixed chains) | Planned |

**Regression Test Policy:** After implementing Stage N, run ALL tests. The full
suite must pass before proceeding to Stage N+1. No exceptions.

**TDD protocol (every stage):**
1. **RED:** write the stage's specification tests first, with concrete inputs
   and expected values. Expected values come from closed forms or from the
   mpmath oracle (`docs/research/oracle/`, outputs in `bench/**`). Never compute
   them inside the test with the code under test. Run the tests and confirm they
   fail for the right reason: a missing feature or a wrong value, not a parse
   error.
2. **GREEN:** implement the minimum production code that makes them pass.
3. **REFACTOR** with the full suite green.
4. **GATE:** the following must all hold:
   - full suite green;
   - benchmark and sweep criteria met;
   - FastSolver↔generic bit-identical equivalence for every new effect map;
   - user sign-off on the interactive checklist.

   Any interactive failure, F12 dump or sweep violation becomes a new red test
   before its fix.

**Test oracles, in order of preference:**
1. closed forms;
2. the mpmath oracle;
3. the legacy `Tracer`, for semantic parity at shallow depth only, and only
   until Stage 84.

**Feedback Loop Protocol and USER VALIDATION GATE:** as in TDD_05 (mandatory
sign-off; the user's word is final).

---

## Target architecture (what the tests specify)

All effects are exact closed-form maps of the visual beam state `(p, τ, κ)` at a
hit, where the beam is the oriented generalized circle
`z(t) = p + tτ/(1 − iκt/2)`.

| Effect | Beam map at hit h | Stack semantics |
|---|---|---|
| Reflection, line (unit dir u) | `τ' = u²·conj(τ_h)`, `κ' = −κ` | self-inverse; pop on re-hit of same source |
| Reflection, arc/circle carrier | = inversion in the carrier | self-inverse |
| Inversion (full or partial arc) | `τ' = −conj(τ_h)ν²`, `κ' = −κ − 2·cross(τ_h, h−c)/R²` | self-inverse |
| Rigid motion (portal A→B) | `p' = T(h)`, `τ' = e^{iθ}τ_h`, `κ' = κ` | push; pop on paired surface; resume at `T⁻¹(hit)` |
| Projective | `p' = h`, `τ'` = exit direction, `κ' = 0` | clears the stack (new epoch) |
| Terminal | trace ends | — |
| Pass | event and step only | counts toward MAX_HITS |

**Geometry:**
- exact line and circle hits;
- the cross-ratio on-segment test, computed at the tier's precision;
- endpoint, blockage and junction rules (legacy semantics; endpoint identity by
  provenance or exact sign);
- side in visual space.

**Anchored planned path:**
- generic Möbius/anti-Möbius difference chains;
- points at ∞ allowed.

**Provenance stack:** cancelling re-hits restore the earlier beam exactly.

---

## Stage 80: Neutral trace types, generalized beam geometry, oracle v2

### Overview
Infrastructure only, with no gameplay change:
- trace data types independent of the legacy `Tracer`;
- the VisualConverter metadata fix;
- line and segment geometry for the beam solver (exact hits, on-segment test,
  side, endpoint blockage);
- an oracle that knows segments, with corpora generated from the real `.tscn`
  levels.

### Prerequisites
Stage 79 plus the precision solver (commit `5e7d2f3`).

### What Is Introduced

| Category | Item |
|---|---|
| Types | `TraceStep`, `TracePath`, `TraceHit` (`scripts/math/trace_*.gd`); `Tracer.Step`, `Tracer.TracedPath` and `Intersection.HitRecord` become aliases |
| Fix | `VisualConverter.prepare_for_display` keeps step metadata on clipped and split steps |
| Geometry | `BeamGeom` (`scripts/math/precision/beam_geom.gd`), generic over Num tiers: segment carriers, exact beam∩line and beam∩circle roots, cross-ratio on-segment, side, endpoint blocked sides, endpoint provenance |
| Geometry | FastSolver raw-float twins of the line hits (with the inline dual) |
| Oracle | `docs/research/oracle/geometry.py` (segments, lines, sides, on-segment), `tscn_import.py`, pytest suite |

### Unit Tests Added (written first)

`tests/test_stage80_trace_types.gd`:
1. `test_trace_step_fields_and_aliases`
2. `test_converter_keeps_meta_on_split`
3. `test_physical_hits_survive_clipping`

`tests/test_stage80_beam_geometry.gd`:
1. `test_line_hits_closed_form`
2. `test_arc_beam_vs_line`
3. `test_on_segment_exact_at_endpoints`
4. `test_on_segment_generic_matches_legacy`
5. `test_side_visual_space`
6. `test_endpoint_blocked_sides_ported`
7. `test_fast_twin_lines`

`docs/research/oracle/test_oracle_v2.py`:
1. line/segment hits and sides vs closed forms
2. `.tscn` importer round trip

### Interactive User Tests
None (no gameplay change). The user confirms this document.

### Validation Summary

| Check | Status | Notes |
|-------|--------|-------|
| Red tests fail for the right reason | [x] | 11/11 failed on assertions with API stubs, no parse errors (commit `26ad6d4`). Two initially vacuous tests were strengthened before committing red. |
| All unit tests pass | [x] | 11/11 Stage 80 tests; oracle pytest 40/40, including 27 scene round trips. |
| All prior regression tests pass | [x] | 848/848. |
| Stage complete | [ ] | Awaiting user confirmation of this document. |

Notes:
- `test_tangent_line_double_root` first used κ = 0.1. That value is not exact in binary, so the line genuinely missed the circle. It now uses κ = 0.125, which is exact.
- The oracle `.tscn` importer reproduces the game's surfaces exactly, including float32 rounding and ids, for every level scene.

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/trace_step.gd`, `trace_path.gd`, `trace_hit.gd` | Create | Neutral trace types (`copy_meta_from` helper) |
| `scripts/math/tracer.gd`, `intersection.gd` | Modify | `Step`, `TracedPath` and `HitRecord` become preload aliases |
| `scripts/visual/visual_converter.gd` | Modify | `_piece` keeps metadata on clipped and split steps; `_snap_step` uses `copy_meta_from` |
| `scripts/math/precision/beam_geom.gd` | Create | Precision-generic segment geometry |
| `scripts/math/precision/fast_solver.gd` | Modify | `line_hits` raw-float twin with dual |
| `scripts/testing/dump_scene_surfaces.gd` | Create | Game-side surface dump (importer ground truth) |
| `docs/research/oracle/{geometry,tscn_import,gen_fixtures_stage80,test_oracle_v2}.py` | Create | Oracle v2 |
| `bench/fixtures/stage80_geometry.json`, `bench/scenes/*.surfaces.json` | Create | Fixtures |
| `tests/test_stage80_trace_types.gd`, `tests/test_stage80_beam_geometry.gd` | Create | Stage 80 specification tests |

---

## Stage 81: Reflections, partial/one-sided inversions, terminals, passes

### Overview
The beam solver takes over every scene made of mirrors, arcs, blocks and
pass-throughs.

### Unit Tests Added (written first)

`tests/test_stage81_effect_maps.gd`:
- `test_line_reflection_beam_map`
- `test_arc_reflection_equals_inversion`
- `test_fast_twin_reflection`

**Oracle data:**
- `bench/fixtures/stage81_rules.json`: hand-built rule scenes (`gen_fixtures_stage81.py`).
- `bench/scenes_corpus/`: 264 scenarios over the 22 supported real levels, 12 per level (`gen_scene_corpus.py`). Each is stable when re-traced at double precision. Plans reach depth 10 in the mirror levels.

**Oracle validation against legacy** (first 8 events, before writing the red tests). Every difference was classified as a legacy error or an intended change; none is an oracle error.

| Difference | Count | Classification |
|---|---|---|
| Same event, point off by ≤ 0.2 px | 11 | Legacy float32: far-away points, post-inversion arcs |
| `inversion_grid` event divergence | 10 | Legacy precision loss. Oracle v2 equals the validated v1 oracle on all 24 traces of these scenarios (v2's wall and pass events ignored) |
| Legacy hit point not on its claimed surface (`two_sided__06`, `__07`) | 2 | Legacy wrong |
| Side label (`two_sided__04`, event 7) | 1 | Legacy wrong. The beam crossed x = 1660 moving +x (RIGHT), reflected at x = 1843 and travels to x = 934. A circle or line crosses a line at most twice, so this second crossing is in −x, which is LEFT. Legacy's conjugating-frame flip mislabels it |
| Origin re-hit (`semi_circle_mirror__01`, planned) | 1 | Intended change: the departure re-hit is an ordinary hit (*Legacy quirks*) |

**End events:**
- `escape` (a straight beam) has no point.
- `origin` (a circle closing its loop) is at the departure point `beam.at(t0)`.

`tests/test_stage81_trace_rules.gd` (expected values from the oracle corpus):
- `test_terminal_ends_trace_at_hit`
- `test_pass_surfaces_emit_events`
- `test_max_hits_32_counts_all_events`
- `test_one_sided_mirror_inert_side_passes`
- `test_partial_inversion_arc_off_segment_passes`
- `test_y_junction_blocks_both_sides`
- `test_partial_blockage_does_not_leak`
- `test_planned_reflection_chain_reaches_cursor`
- `test_zero_length_rule_same_in_planned`
- `test_departure_rehit_is_ordinary_hit`
- `test_targets_recorded_on_segment`
- `test_fast_twin_trace_rules`: the FastSolver walk is bit-identical to the generic float64 walk
- `test_planned_reflection_chain_reaches_cursor` (corpus)
- `test_scene_corpus_matches_oracle`: all 264 scenarios, planned and physical, ≤ 0.01 px

`tests/test_solver_parity.gd`, using the harness `scripts/testing/solver_parity.gd`:
- `test_scene_defs_match_dump`: the solver's game-surface conversion equals the dumped data the oracle traced
- `test_parity_shallow_all_supported_scenes`: legacy vs beam on the corpus. A difference is allowed only where beam agrees with the oracle

`tests/test_stage81_scene_switch.gd`:
- `test_mirror_scenes_use_beam`
- `test_portal_and_projective_scenes_fall_back` (until Stages 82 and 83)

**Added during GREEN** (each from a failure found while greening; red first):
- `test_stage81_trace_rules.gd::test_provenance_points_snap_to_endpoints`: oracle fixtures for every exactness rule below (cursor at a junction, player on an arc endpoint, a point carried through an inversion, collinear data, a reflected exact line, a mirror-only plan, a line through an inversion center, a tangent line).
- `tests/test_stage81_bigfloat_exact.gd`: BigFloat division and square root are correctly truncated, so representable results are exact. It fails on the old code: 80000/200 gave 399.99… and sqrt(1) gave 0.99….

### What Is Introduced

| Category | Item |
|---|---|
| Solver | One rule walker (`BeamSolver.walk_from`) mirroring oracle `trace2`, over two math backends: `BeamSolver.NumBackend` (any tier) and `FastSolver.Backend` (raw float64 with the inline dual, bit-identical to F64Num). The anchored chain (`AnchorChain`) is a hook: in PLANNED mode, and in PHYSICAL mode until the first divergence. |
| Scene | `BeamScene` (scene defs: `BeamScene.defs_from_surfaces`, which is also what the dump script and the oracle use). `BeamEffects` holds the beam, point and pole maps. |
| Exactness | `Exact` (`scripts/math/precision/exact.gd`): exact predicates (orient, on-circle, incircle, implicit lines, line reflection, homogeneous points, circumcenter, tangency). A float64 evaluation with a certified error bound handles the cheap case, and exact BigFloat arithmetic handles everything else. |
| Projective parameter | t = ∞ is an exact value in every tier (`Num.is_inf`, `BigFloat.infinity`). It is the antipode of a circle beam, where degree-dropping roots lie. |
| Precision | `PrecisionLadder` and `ErrorEstimate` work on scene defs. Planned shares physical's climb when they agree through the cursor. The FastSolver scene is cached across frames. |
| Game | `BeamTracer.scene_supported` (capability per effect type). Mirror, arc, block and pass scenes use the beam solver. The F12 dump lists the beam events. |

### Exactness rules (oracle `trace2` = solver)

Rounding never decides an endpoint, an incidence, a tangency or a root at the antipode:
- **Known points:** exact data points a beam passes through. These are:
  - the player;
  - the cursor (on B_K);
  - an exact departure point;
  - known points lying on the applied surface's carrier (the effect fixes its carrier, so they carry over to the new beam);
  - segment endpoints on an exact line.

  Pops restore the earlier beam's known points.

  A surface whose carrier contains a known point exactly is hit exactly there: its root nearest that point takes the point's parameter and the exact point. A cursor exactly at the departure point is reached immediately.
- **Exact lines:** kept in implicit form a x + b y = c with exact coefficients. They come from:
  - a direction trace;
  - a trace without plan;
  - a plan made only of mirror reflections (the aim image is then an exact rational point).

  An exact line stays exact through every line reflection. An exact line through an inversion center stays exactly straight.
- **Tangency:** an exact line exactly tangent to a circle carrier touches it without crossing, so it is not an event.
- **MAX_HITS:** a trace cut at 32 events ends at the next crossing, drawn up to it with no effect.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**
- [x] Mirrors, arcs and blocks look identical to before at shallow depth.
- [x] Deep mirror plans stay attached to the cursor.
- [x] One-sided mirrors and pass-throughs behave as before.
- [x] Firing follows the preview.
- [x] Aiming exactly at segment endpoints and room corners (cursor or player on them) behaves sensibly: corners block, endpoint grazes pass.
- [x] Rule changes are acceptable:
  - the departure point is re-hit after a full loop;
  - a reflection exactly at the cursor reaches the cursor immediately;
  - a tangent touch is not a hit.

### Validation Summary

| Check | Status | Notes |
|-------|--------|-------|
| Red tests fail for the right reason | [x] | 19/20 failed on assertions with stubs (commit `c4a40c3`). The projective/portal fallback test was already true. |
| All unit tests pass | [x] | Full suite 872/872 (848 prior + 24 Stage 81) |
| Scene corpus vs oracle | [x] | 264/264 scenarios, planned and physical, full traces, ≤ 0.01 px |
| Parity harness | [x] | 503 same, 25 legacy wrong (beam = oracle), 0 unclassified |
| Invariant sweeps (all scenes, combos, plans, precision) | [x] | 0 violations, including endpoint, corner, collinear and tangency coincidences |
| FastSolver ↔ generic | [x] | bit-identical events on fixtures and the bench corpus; gains match DualNum |
| Benchmarks (v2 corpora) | [x] | Ladder 161/161 (depth 1–15) and stress 40/40 (depth 16–25), planned and physical, ≤ 0.01 px. Legacy Tracer: 3/161 and 0/40. f64 alone: planned 161/161, physical 112/161 (needs the ladder). |
| Estimator calibration | [x] | Coverage: planned 100%, physical 99.7%. 0 missed escalations. Anchored-prefix events now get a per-beam angle gain, which grows with distance from the anchor. |
| Per-frame cost (warm caches, f64 accepted) | [x] | inversion_grid 5.2 ms (legacy 5.9), circle_no_walls 3.5 (2.75), main 2.5 (0.95), two_mirrors 1.2. Exact lines are lazy: a float bound plus exact coefficients only at near-coincidences. |
| Stage complete | [x] | User signed off on the interactive checklist and the rule changes. |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/precision/beam_solver.gd` | Rewrite | Generalized walker, backends, anchored chain, provenance |
| `scripts/math/precision/fast_solver.gd` | Rewrite | Raw-float math and twins (reflection, classification, antipode), walker backend |
| `scripts/math/precision/beam_scene.gd`, `beam_effects.gd`, `exact.gd` | Create | Scene defs, beam maps, exact predicates |
| `scripts/math/precision/big_float.gd`, `f64_num.gd`, `dual_num.gd`, `num_ctx.gd`, `hp_beam.gd`, `beam_geom.gd` | Modify | Correctly truncated div and sqrt; exact infinity; antipode; degree-drop roots; cached winding |
| `scripts/math/precision/precision_ladder.gd`, `error_estimate.gd`, `beam_tracer.gd` | Modify | Scene defs, shared climb, scene cache, capability check, `max` and on-segment in paths |
| `scripts/visual/visual_converter.gd` | Modify | Zero-length piece for a clipped hit-less step to infinity (path starts at the player) |
| `scripts/game/debug_dumper.gd`, `scripts/testing/{dump_scene_surfaces,precision_bench,run_precision_bench,solver_parity}.gd` | Modify/Create | Beam events in the F12 dump, shared defs conversion, bench on scene defs, parity harness |
| `docs/research/oracle/{trace2,beam,geometry,gen_fixtures_stage81,gen_scene_corpus,regen_bench_v2}.py` | Modify/Create | Oracle v2 rules, exactness, projective parameter, corpora |
| `bench/scenarios`, `bench/expected`, `bench/stress/*`, `bench/scenes_corpus`, `bench/fixtures/stage81_rules.json` | Regenerate | v2 semantics |
| `tests/test_stage81_*.gd`, `tests/test_solver_parity.gd`; `tests/test_precision_{fast_solver,integration}.gd`, `tests/test_stage78_arc_midpoint_investigation.gd` | Create / Modify | Stage 81 tests; migration to the new API; legacy investigation pinned to Tracer |

---

## Stage 82: Portals (rigid motion)

### Overview
Portal pairs on the beam solver.

**Transform.** A portal surface S carries the rigid motion T(z) = a z + d, with a = unit(cos θ, sin θ) of the float64 values. Its pair is S' = T(S), built as T of the source; it carries T⁻¹, and scene defs record the pair id. A hit at h maps the beam onto S':
- p' = T(h), τ' = a·τ_h, κ' = κ;
- the new beam departs S'.

**Stack.** Hitting the pair of the top entry pops it: the earlier beam object is restored, resumed at T⁻¹ of the point hit. Legacy's quirk of excluding the just-hit surface at the exit after a pop is **fixed**: the departure surface is the one the beam actually leaves.

**Exactness through portals.**
- Known endpoints map to the pair's corresponding endpoints.
- Other known points survive only when T(k) is exactly representable.
- Exact lines carry through translations.
- Plans mixing mirrors and θ = 0 portals keep an exact aim image.

**Oracle data:**
- `bench/fixtures/stage82_portals.json` (`gen_fixtures_stage82.py`).
- The scene corpus now includes the two portal levels: 288 scenarios. The corpus is now seeded per scene.

### Unit Tests Added (written first)
`tests/test_stage82_portals.gd`:
- `test_portal_beam_map`: closed form. The outgoing beam contains T(z), ahead of the exit, with κ preserved.
- `test_portal_translate_and_departure`: covers the departure root on the paired surface.
- `test_pop_across_pair_resumes_exactly`: the restored beam is the original object and stays exactly straight.
- `test_portal_stack_rules`: a mirror between the portal hits means the pair pushes its own transform.
- `test_portal_rotation_and_arcs`
- `test_plan_through_portal_level`
- `test_plan_through_portal_reaches_cursor`: 24 corpus scenarios, ≤ 0.01 px, cursor exact.
- `test_after_portal_gap_flag`
- `test_fast_twin_portal`

`tests/test_stage81_scene_switch.gd`: the portal levels move to the beam solver.

Moved to Stage 84: `test_portal_pair_construction_without_frames`. Rewriting `create_portal_pair` without `Point.transformed` belongs with deleting the provenance transforms, which Stage 71's tests still assert. The beam solver does not use them.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**
- [x] The portal path appears on the paired side with a visible gap.
- [x] Planning through portals works.
- [x] Firing follows the preview.
- [x] Line and arc portals in `test_levels/portals` and `playground/portals` behave as before, including going back through the pair.

### Validation Summary

| Check | Status | Notes |
|-------|--------|-------|
| Red tests fail for the right reason | [x] | 9/9 portal tests plus the scene switch failed on assertions (commit `9b853c2`) |
| All unit tests pass | [x] | Full suite 881/881 |
| Scene corpus vs oracle | [x] | 288/288, including 24 portal-level scenarios; planned and physical ≤ 0.01 px, cursor exact |
| Parity harness | [x] | 542 same, 34 legacy wrong (beam = oracle), 0 unclassified |
| Benchmarks | [x] | Ladder 161/161 and stress 40/40 |
| Estimator calibration | [x] | Planned 98.2%, physical 99.2%, 0 missed escalations. Anchored-prefix gains have their own constant (`PREFIX_GAIN_SCALE`, C = 64): prefix errors do not compound. |
| Per-frame cost (median / p90) | [x] | portals levels 2.4/5.1 and 2.0/3.5 ms; main 1.7/2.3; inversion_grid 6.3/7.9. Rare chaotic frames legitimately climb to 112 bits (cached afterwards). |
| Stage complete | [x] | User signed off. |

Notes:
- **Fixed:** the planned residual compared a portal's hit point with the next beam, which starts at T(hit). That forced needless climbs; teleports are now excluded from the next-beam check.
- **Legacy diagnostics pinned to Tracer:** `test_portal_refl_gap.gd`'s frame diagnostics read `step.frame`, which the beam solver does not have. They are removed with Tracer in Stage 84.

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/precision/beam_scene.gd` | Modify | Portal data per surface (a at the tier and its float64 twin, d, pair); pair ids in scene defs |
| `scripts/math/precision/beam_effects.gd` | Modify | Rigid beam map; point and pole maps with inverses |
| `scripts/math/precision/beam_solver.gd` | Modify | Pop on the pair; departure from the paired surface; anchored chain for portals; known points and exact lines through portals; teleport and popped event flags |
| `scripts/math/precision/fast_solver.gd`, `exact.gd` | Modify | Raw rigid-map twin; lazy translated exact lines |
| `scripts/math/precision/beam_tracer.gd`, `precision_ladder.gd` | Modify | The step after a portal starts at the exit and carries `after_portal`; stack from event flags; teleport residual fix |
| `docs/research/oracle/{trace2,tscn_import,gen_fixtures_stage82,gen_scene_corpus,test_trace2}.py` | Modify/Create | Portal semantics in the oracle, fixtures, corpus |
| `tests/test_stage82_portals.gd`; `tests/test_stage81_{scene_switch,trace_rules}.gd`, `tests/test_precision_integration.gd`, `tests/test_portal_refl_gap.gd` | Create/Modify | Stage 82 specification; portal levels on beam; legacy diagnostics pinned |

---

## Stage 83: Projective effects (physical) + full switch-over

### Overview
Projectives on the beam solver, and every scene switched to it.

**Projective exit.** At the hit h the beam leaves as a straight line from h along ±axis. The axis is:
- the carrier normal (line normal projection);
- the radius h − c (circle normal projection);
- the fixed normal (directional projections).

The sign is exact:
- **through:** sign(exit·∇f) = sign(dir·∇f), so the beam exits to the far side;
- **back:** it returns to the approach side;
- an axis tangent to the carrier exits along +axis (through) or −axis (back).

This replaces legacy's `determine_side(hit + axis)` probe. That probe had an effective threshold of > −0.5 instead of > 0 for circle-directional surfaces.

**After a projective:**
- the stack clears and a new epoch begins;
- the beam departs the surface;
- plan bookkeeping by id.

**PLANNED mode** consumes a projective plan entry without applying it, and aiming skips it, until Stage 87. Legacy instead discarded the whole plan.

**Degenerate inputs are native.** When a point at infinity appears in the anchored construction (player or cursor exactly at an inversion center), the solver walks forward from the exact aim direction. When the aim image itself is at infinity, it walks straight at the cursor.

**Oracle data:**
- `bench/fixtures/stage83_projective.json` (`gen_fixtures_stage83.py`).
- The scene corpus covers all 29 scenes: 348 scenarios.

### Unit Tests Added (written first)
- `test_projective_exit_directions`
- `test_projective_resets_stack_and_epoch`
- `test_projective_straight_beam_exact`
- `test_planned_passes_projectives_until_87`
- `test_every_scene_uses_beam`
- `test_degenerate_inputs_native`
- New invariants:
  - `test_EFFECT_LAW_detects_wrong_tangent`
  - `test_EVENT_ON_BEAM_detects_offset_point`
  - `test_BEAM_ORDER_detects_reordered_events`
  - `test_PLAN_EFFECTS_APPLIED_by_surface_ids`
  - `test_DIRECTION_ONLY_without_legacy`
- `test_invariant_sweep` and `test_invariant_combos`: zero violations over
  beam-solved runs.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**
- [x] Every playground scene behaves as before or better.
- [x] Projective scenes:
  - `projective`, `projective_lines`, `projective_arcs`, `directional_lines`, `directional_arcs`;
  - through and back variants exit to the right side.
- [x] Standing at circle centers or aiming there causes no glitches. The path is native, with no fallback.
- [x] Rule changes are acceptable:
  - a plan containing a projective aims through its other entries (legacy ignored the whole plan);
  - circle-directional exits use the exact side rule.

### Exactness rules added in Stage 83 (oracle `trace2` = solver)
- **Projective exits** are exact lines when the hit point is exact and the axis is exact data.
- **Through an inversion center:** any beam through a known point that is exactly the inversion center inverts to an exactly straight beam.
- **Vias** (segment midpoints and arc vias) are known points on exact lines, like endpoints.
- **Concurrency:** on an exact line, adjacent crossings of two line carriers that meet the line at one point (exact 3×3 determinant) share a parameter. The id rule orders them, and the pair's zero gap is not a decision margin.
- **The departure re-hit** uses the departure record only at its own parameter (it matters after pops).
- **`param_of` near a circle beam's antipode** uses the well-conditioned form 1/Re(den/w). The two forms are the same algebra for points on the beam; straight beams keep Re(w/den), which is the orthogonal projection of an off-beam cursor.

### Validation Summary

| Check | Status | Notes |
|-------|--------|-------|
| Red tests fail for the right reason | [x] | Projective, switch-over and invariant tests failed on assertions with stubs (commit `76a0f4c`) |
| All unit tests pass | [x] | Full suite 893/893 |
| Scene corpus vs oracle | [x] | 348/348 over all 29 scenes; planned and physical ≤ 0.01 px |
| Parity harness | [x] | 644 same, 52 legacy wrong (beam = oracle), 0 unclassified |
| Invariant sweeps | [x] | Zero violations: every scene, plan sweeps, all 30 effect combos (projective and directional now included), precision sweeps |
| Benchmarks | [x] | Ladder 161/161 and stress 40/40 |
| Estimator calibration | [x] | Planned 98.2%, physical 99.2%, 0 missed escalations |
| Per-frame cost (median / p90) | [x] | inversion_grid 6.5/7.9 ms; projective 2.9/7.6; projective_lines 1.5/1.8; directional_arcs 1.8/2.1; main 1.7/2.3; portals 2.3/10.4 |
| Stage complete | [x] | User signed off. |

**Invariant checker:**
- **New:** `EFFECT_LAW`, `EVENT_ON_BEAM`, `BEAM_ORDER` (pure checks over solver events).
- **Rewritten on events:** `PLAN_EFFECTS_APPLIED`, `HITPOINT_ON_CARRIER`, `ON_SEGMENT_CONSISTENCY`.
- **Rewritten without legacy ray parameters:** `PHYSICAL_CONTINUITY`.
- **Rewritten for the beam solver:** `DIRECTION_ONLY` compares the planned shot against a converged shot without plan aimed at the image. The comparison is bounded by tangent-linear sensitivity to the float64 rounding of that image.
- **Retired from `check_all`:** `S18`, `BACK_TRANSFORM`, `RAY_ALIGNMENT`, `ARC_MIDPOINT`, `PARAM_MONOTONICITY`, `POST_INVERSION_ARC`, `SHARED_RAY` (legacy frames).

**Fallbacks:** the renderer and the firing code fall back to Tracer only under the debug toggle `BeamTracer.LEGACY_FALLBACK` (off by default), or when explicitly pinned (`use_beam_tracer = false`, legacy diagnostics).

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/precision/beam_scene.gd`, `beam_effects.gd`, `fast_solver.gd` | Modify | Projective sides; exact exit rule and its raw twin (with the circle-normal derivative) |
| `scripts/math/precision/beam_solver.gd` | Modify | Projective firing (stack clear, epochs, exact exit lines); native degenerate inputs; straightening through inversion centers; vias as known points; concurrency ties; departure-record parameter match |
| `scripts/math/precision/hp_beam.gd` | Modify | Well-conditioned `param_of` near the antipode (circle beams) |
| `scripts/math/precision/exact.gd` | Modify | Concurrency predicate; scalar certified filters |
| `scripts/math/precision/beam_tracer.gd`, `scripts/visual/path_renderer.gd`, `scripts/game/game_manager.gd` | Modify | Epoch frame ids; legacy fallback behind `LEGACY_FALLBACK` |
| `scripts/testing/invariant_checker.gd` | Modify | Event-based invariants; legacy-frame checks retired |
| `docs/research/oracle/{trace2,test_trace2,gen_fixtures_stage83,gen_scene_corpus}.py` | Modify/Create | Projectives, degenerate aims, exactness rules, all-scene corpus |
| `tests/test_stage83_{projective,invariants}.gd`; `tests/test_stage81_*`, `test_precision_integration.gd`, `test_invariant_combos.gd` | Create/Modify | Stage 83 specification; switch-over; all combos checked |

---

## Stage 84: Delete the legacy solver

### Overview
The beam solver is the only solver. The legacy frame-based machinery is deleted:
- `Tracer`, `Planner`, `TransformCache`, `TrackedTransform`, `MobiusTransform`;
- the intersection module (`Intersection.project_onto_carrier` moved to `VisualConverter`, with an exact center test instead of `< 1e-12`);
- the Möbius and tracked-transform state of effects, and `Effect.normalized`;
- the projections' `apply_forward` (the beam's exact exit rule is in `BeamEffects`);
- Point and Segment provenance transforms, and `GeneralizedCircle.transformed_by`;
- `TraceStep.frame`;
- the parity harness, the `s0_tracer` bench strategy, `LEGACY_FALLBACK` / `use_beam_tracer`, and the retired invariant checks.

**What replaced them:**
- `Effect.is_self_inverse()` (reflection, inversion) drives surface self-links.
- Portal pairs are built directly: B = T(A) with T(z) = e^{iθ}z + d, and the inverse motion is (−θ, −e^{−iθ}d). Each effect knows its pair (`partner()`, held weakly). `BeamScene` pairs portals through `partner()`, and a portal without its paired surface is unsupported.
- `TraceStep.IDENTITY_FRAME` (0) replaces `MobiusTransform.IDENTITY_ID`.

**Test migration:**
- Behavioural tests were ported through `TestTrace` (`scripts/testing/test_trace.gd`), which has `Tracer.trace`'s signature and runs `BeamTracer.trace_pair`.
- Legacy-internals tests were deleted. All 298 are listed in [`docs/stage84_test_migration.md`](stage84_test_migration.md), each with its intent and either the covering test or the reason.
- Assertions that encoded legacy representation details were changed to the beam semantics, each with a comment:
  - per-step frame objects become `frame_id` transitions;
  - "frame resets to identity after a projective" becomes "a fresh epoch frame";
  - the step ray is the shared aim ray, not the caller's object;
  - lone portal surfaces become full pairs.
- Legacy-parity geometry tests (`test_stage80_beam_geometry`) now compare against legacy results frozen before deletion.

**Expectation changes (review at sign-off):**
- `test_stage43_bounce_point_on_carrier`: PLANNED fires on the first crossing matching id and side, here off the arc at (100,200), not the legacy (300,200). This is the Stage 81 rule.
- `test_trace_ends_violation_1` and `test_player_blocks_ray_looping_through_infinity`: with the cursor on a wall corner, the trace ends at the cursor (cursor-first tie). The beam has no player block.

### Unit Tests Added (written first)
- `test_no_legacy_symbols`: no legacy files; no legacy identifiers in `scripts/` (comments stripped).
- `test_surface_self_links_without_tracked`: self-links follow `is_self_inverse()`.
- `test_portal_pair_construction_without_frames`: target points equal the closed form exactly; the inverse motion; `partner()`; `BeamScene` pairing.
- Ported behavioural tests (TestTrace), with the migration table.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**
- [x] Smoke test every scene (`scenes/`, `scenes/test_levels/`, `scenes/playground/`):
  - the preview draws;
  - plans work;
  - firing works;
  - the F12 dump works (it now prints the beam aim, not the Planner aim).
- [x] Portal scenes: chevrons and plan links unchanged; paths through portals unchanged.
- [x] Mirror and inversion surfaces still self-link in the plan UI.

### Validation Summary

| Check | Status | Notes |
|-------|--------|-------|
| Red tests fail for the right reason | [x] | Before deletion: 595 pass; only the 3 Stage 84 tests failed, on assertions (commit `9d70b56`) |
| Ported tests pass before deletion | [x] | Full suite, legacy code still present |
| All unit tests pass | [x] | Full suite 598/598 after deletion (893 before: 298 deleted, 3 added) |
| Scene corpus vs oracle, ladder, sweeps | [x] | Part of the suite: `test_stage81_trace_rules`, `test_precision_*`, `test_invariant_*` (zero violations) |
| Stage complete | [x] | User signed off (expectation changes accepted). |

### Files Modified/Created/Deleted

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/{tracer,planner,transform_cache,tracked_transform,mobius_transform,intersection}.gd`, `scripts/testing/solver_parity.gd` | Delete | Legacy solver |
| `scripts/math/trace_mode.gd`, `scripts/testing/test_trace.gd` | Create | `TraceMode` constants; `TestTrace` helper |
| `scripts/math/effects/*.gd` | Modify | Effects without Möbius/tracked state; `is_self_inverse`; portal `partner()`; direct `create_portal_pair`; `apply_forward` removed |
| `scripts/math/{point,segment,generalized_circle,trace_step}.gd` | Modify | Provenance transforms, `transformed_by`, `TraceStep.frame` removed; `IDENTITY_FRAME` |
| `scripts/game/surface.gd`, `scripts/math/precision/beam_scene.gd` | Modify | Self-links by `is_self_inverse`; portal pairing by `partner()` |
| `scripts/math/precision/beam_tracer.gd`, `scripts/visual/{path_renderer,visual_converter}.gd`, `scripts/game/{game_manager,debug_dumper}.gd` | Modify | No fallback; no frames; `project_onto_carrier` moved |
| `scripts/testing/{invariant_checker,precision_bench,run_precision_bench,dump_scene_surfaces,violation_viewer}.gd`, `Makefile` | Modify | Retired checks and `s0_tracer` removed |
| `tests/*` | Modify/Delete | Ports and deletions (see the migration table); `tests/test_stage84_no_legacy.gd` |
| `GAME_SPEC.md` (2.5.0), `CHANGELOG.md`, `CLAUDE.md`, `docs/precision_benchmark_findings.md`, `docs/stage84_test_migration.md` | Modify/Create | Docs describe the beam solver |

---

## Stage 85: State flags (§9.4 ConfigResolver + §10.6 StateChange)

### Overview
Surfaces whose behaviour depends on game-state flags, and sides that write flags.

**Resolvers:**
- `FixedResolver` gives one config per side.
- `CategoricalResolver` maps a flag's value to the configs of both sides, keyed by the value's JSON spelling (`StateChange.flag_key`, so `2` and `2.0` are the same key). A missing key or value falls back to `DEFAULT`, then to the first entry, with a warning.
- Categorical sides are pass, terminal, reflection or inversion. Portals and projectives are fixed.

**State changes:**
- `StateChange` has two ops: `SET` a value, or `TOGGLE` the flag's truth value (a missing key counts as false). Any key may be written.
- A change fires when its side fires, the same rule as effects:
  - PHYSICAL: a fully blocked hit.
  - PLANNED: the consumed plan entry.
- The side's config is resolved before its own change is applied. Along a trace, the last writer wins.
- PHYSICAL plan bookkeeping counts a hit that fires only a state change.

**Planning:** forward state simulation (§13.7). Each plan entry resolves under the flags left by the entries before it. The anchor chain, the exact aim image and the fixed-point rule all use it. An entry that resolves to a pass is consumed without a frame change.

**Divergence:** the physical trace stays on the anchored path only while it fires the same effects *and* the same state changes as the plan.

**Pops** restore beams, not flags. A pop also needs the same map: an involution undone.

**Frames include the state:** a frame is identified by (epoch, transform stack, flags), so a state change starts a new frame. A toggler fired only by the physical trace (not planned) therefore shows as a divergence where it happens, not after the cursor. When the toggler is in the plan, both traces toggle and stay aligned.

**Cursor after a frame-less completion:** a plan completed without a frame change makes the cursor a candidate on the same beam when it lies ahead. Without this, a plan ending at a toggler, or at a door that resolves to a pass, never reaches the cursor. The same rule now also covers an unapplied projective entry (legacy quirk 3: the cursor part; applying projectives remains Stage 87).

**Game:**
- The preview starts from the game state's flags.
- Firing keeps the physical trace's final flags; they become the game state when the arrow lands.
- The checkpoint taken at fire time restores them on undo.
- Surfaces redraw and click detection resolve under the current state.
- The F12 dump prints the flags, and each event's state change.

**Level exports:** `state_key`, `toggle_lines` (pass and toggle, plannable), `gate_lines` (block unless the flag is true) and `flag_mirror_lines` (mirror only while the flag is true). The test scene is `scenes/test_levels/flag_switches.tscn`, which the sweeps pick up.

**Oracle data:** `bench/fixtures/stage85_flags.json` (`gen_fixtures_stage85.py`, 11 scenarios).

### Unit Tests Added (written first)
- `test_stage85_resolvers.gd`:
  - `test_fixed_resolver_default`
  - `test_categorical_resolver_lookup_and_DEFAULT_fallback`
  - `test_active_side_config_delegates`
- `test_stage85_state_changes.gd`:
  - `test_set_and_toggle_ops`
  - `test_state_change_fires_only_when_side_fires`
  - `test_last_writer_wins_along_trace`
  - `test_effect_and_state_change_coexist`
  - `test_categorical_mirror_switches_mid_trace`
  - `test_planned_applies_state_on_consumed_entry`
  - `test_planner_forward_state_simulation`
  - `test_pops_restore_beams_not_flags`
  - `test_fast_twin_flags`
  - `test_state_promoted_after_shot_and_undo_restores`
  - `test_state_change_starts_new_frame` (added after the sweep found post-cursor divergence on `flag_switches`)

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**
Scene: `scenes/test_levels/flag_switches.tscn`.
- **Toggler and gate:** the toggler is the vertical line at x = 800; the gate is at x = 1100.
  - Initially closed, the gate is drawn as a block.
  - [x] A shot through the toggler opens the gate within the same preview: the beam passes it.
  - [x] After firing, the gate is drawn open.
  - [x] The next preview through the toggler closes it again and blocks.
- **Flag mirror:** the diagonal line near the top.
  - [x] It reflects only while the flag is true, and it is plannable only then.
- [x] A plan through the toggler (click it) reaches the cursor.
- [x] Undo restores the previous flags and the drawing.

### Validation Summary

| Check | Status | Notes |
|-------|--------|-------|
| Red tests fail for the right reason | [x] | 12 failed on assertions with stubs (commit `b59474f`) |
| All unit tests pass | [x] | Full suite: 608 tests, plus every sweep shard (`run_tests_parallel.sh`, 5 min) |
| Oracle fixtures (both tiers) and fast twin | [x] | 11 scenarios at f64 and 4 limbs; FastSolver bit-identical, flags included |
| Sweeps, including flag_switches | [x] | Zero violations. The first run found post-cursor divergence on `flag_switches`, fixed by frames that include the state |
| Stage complete | [x] | User signed off |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/game/{state_change,config_resolver,fixed_resolver,categorical_resolver}.gd` | Create | StateChange and the resolvers |
| `scripts/game/{surface,level_settings,game_manager,surface_node,click_detector,plan_hud,debug_dumper}.gd` | Modify | Resolver on Surface; exports; promotion and undo; state-aware drawing and clicking; dump |
| `scripts/math/precision/{beam_scene,beam_solver,fast_solver,error_estimate,precision_ladder,beam_tracer}.gd` | Modify | Categorical tables and state changes in scene defs; flags in the walk; forward simulation; divergence and pop rules; flags through every entry point; `Pair.final_flags` |
| `scripts/visual/path_renderer.gd`, `scripts/testing/invariant_checker.gd` | Modify | Traces from the game state's flags |
| `docs/research/oracle/{trace2,gen_fixtures_stage85}.py`, `bench/fixtures/stage85_flags.json` | Modify/Create | Spec and fixtures |
| `scenes/test_levels/flag_switches.tscn`, `tests/test_stage85_*.gd` | Create | Test level and specification |

---

## Stage 86: Ghost mode & toggler surfaces

### Overview
**Ghost** is a reserved flag that lasts one shot.
- `BeamTracer.trace_pair` drops it from the starting flags, and the game drops it before promoting a shot's flags. A ghost shot never leaves the game ghosted.
- **Togglers:** a ghost toggler is a pass side with a `ghost` toggle.
- **While ghosted, everything else passes,** together with its state change: mirrors, inversions, portals, projectives, terminals and room walls. Togglers still fire.
- **Loops:** a loop around the beam that changed the state is walked again (oracle and solver). A ghost arrow leaving through a wall therefore wraps through infinity, re-enters through the opposite wall, and materializes at the next toggler, or at the same one a loop later. A loop that changed nothing ends the trace as before, and MAX_HITS bounds everything.
- **Rendering:** ghost steps are drawn paler and translucent. Ghost togglers are pale cyan and flag togglers yellow.
- **Levels:** the export is `ghost_toggler_lines`. The demo scene is `scenes/test_levels/ghost_room.tscn`.

This follows your clarifications, which replace the original Stage 86 plan where they differ:
- ghost is per shot;
- a level has several togglers;
- walls react;
- ghost arrows loop through infinity.

**Plan entries inside a ghost stretch** resolve to a pass under forward simulation and are consumed without a frame change. Refusing them in the plan UI, as the original plan proposed, is not implemented.

**Oracle data:** `bench/fixtures/stage86_ghost.json` (`gen_fixtures_stage86.py`, 7 scenarios).

### Unit Tests Added (written first)
- `test_ghost_toggler_toggles_flag`
- `test_ghost_passes_everything`
- `test_ghost_ends_at_next_toggler`
- `test_ghost_loops_through_infinity`
- `test_planned_and_physical_agree_through_ghost`
- `test_plan_entry_inside_ghost_stretch_is_a_pass`
- `test_fast_twin_ghost`
- `test_ghost_steps_flagged`
- `test_ghost_toggler_rendering`
- `test_ghost_room_demo`
- `test_ghost_is_per_shot`

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**
Scene: `scenes/test_levels/ghost_room.tscn`.
- **Layout:**
  - the player is at (900, 540);
  - the ghost togglers are the pale vertical lines at x = 1000 (A) and x = 650 (B);
  - a block stands at x = 1100;
  - the blue diagonal mirror is near x = 750.
- [x] **Shooting right through A:** the path turns pale, crosses the block and the right room wall, and leaves the screen. It comes back from the left edge, crosses the left room wall, and turns solid again at B. The mirror then reflects it down to the bottom wall.
- [x] **The arrow** flies the same path (out of the screen and back).
- [x] **Ghost is per shot:** the next shot starts solid, and after any shot the game is not ghosted.
- [x] **Planning:** clicking A and B into the plan keeps the preview aligned through the ghost stretch.

### Validation Summary

| Check | Status | Notes |
|-------|--------|-------|
| Red tests fail for the right reason | [x] | 9 failed on assertions with stubs (commit `dd343a2`) |
| All unit tests pass | [x] | Full suite: 619 tests, plus every sweep shard |
| Oracle fixtures (both tiers) and fast twin | [x] | 7 scenarios at f64 and 4 limbs; FastSolver bit-identical; earlier oracle data unchanged |
| Sweeps, including ghost_room | [x] | Zero violations |
| Stage complete | [x] | User signed off |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/precision/{beam_scene,beam_solver,beam_tracer}.gd` | Modify | Ghost resolution; loop again after a state change; ghost steps; per-shot start |
| `scripts/math/trace_step.gd` | Modify | `ghost` |
| `scripts/game/{level_settings,game_manager,surface_node}.gd`, `scripts/visual/path_renderer.gd` | Modify | Export; per-shot promotion; toggler colours; ghost drawing |
| `docs/research/oracle/{trace2,gen_fixtures_stage86}.py`, `bench/fixtures/stage86_ghost.json` | Modify/Create | Spec and fixtures |
| `scenes/test_levels/ghost_room.tscn`, `tests/test_stage86_ghost.gd` | Create | Demo and specification |

---

## Stage 87: Plannable projectives (§13.3/13.4 mixed chains)

### Unit Tests Added (written first)
- `test_back_propagation_per_type`
- `test_back_propagation_off_segment_invalid`
- `test_mixed_chain_reaches_cursor`
- `test_projective_last_entry_injects_cursor`
- `test_planned_applies_projective`
- `test_physical_matches_planned_when_realizable`

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**
- [ ] Plans through projective surfaces reach the cursor.
- [ ] Invalid placements are refused.

---

## Legacy quirks: explicit resolution

| Legacy behaviour (tracer.gd) | Resolution | Specifying test |
|---|---|---|
| Partial endpoint blockage leaks into later hits | Fix: accumulate only across hits at the same point | `test_partial_blockage_does_not_leak` (81) |
| PHYSICAL plan matching ignores side; PLANNED checks it | Keep | `test_partial_inversion_arc_off_segment_passes` (81) |
| Projective plan entry consumed but never applied | Keep until 87, then fix | `test_planned_passes_projectives_until_87` → inverted in 87 |
| Zero-length hits in PLANNED require a full block | Fix | `test_zero_length_rule_same_in_planned` (81) |
| Origin-after-wrap re-applies the origin surface, ignoring rules | Change: an ordinary hit | `test_departure_rehit_is_ordinary_hit` (81) |
| Cursor step ends at the cursor even when off the ray | Keep | ported cursor tests (84) |
| Endpoint and collinear detection by float `==` | Replace: provenance plus exact sign | `test_on_segment_exact_at_endpoints` (80) |
| MAX_HITS counts the origin and cursor too | Keep (§12.6) | `test_max_hits_32_counts_all_events` (81) |
| `targets_hit` recorded in PLANNED | Keep for preview; promote only from physical | `test_state_promoted_after_shot_and_undo_restores` (85) |
