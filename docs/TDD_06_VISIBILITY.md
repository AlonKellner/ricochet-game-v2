# TDD Document 6: Visibility System

**Stages 34a--38** | Visibility polygon, obstruction, UX invariants, plan-aware visibility

**Implementation order note:** Stage numbers (34a--38) are preserved from the original TDD_04 for traceability. These stages are implemented AFTER TDD_05's planned future stages (70--78: rigid motion, projective effects, compound effects). This matches GAME_SPEC §28: "Phase 7 (visibility) requires Phase 6 (all effects)." The visibility system must handle arc carriers from circle inversion, transformed surfaces from rigid motion, and frame resets from projective effects.

> **Status: HISTORY — superseded by [TDD_14_VISIBILITY.md](TDD_14_VISIBILITY.md) (Stage 110).** Stages 34a–38 were never implemented. They were written for the frame-based solver deleted in Stage 84 (`MobiusTransform`, `see_through`, obstruction sorting, the parallel-source mode). Stage 110 built the visibility region on the beam solver: casts are pair traces, the region is the non-divergence set by construction, and the invariant VISIBILITY-EQUALS-NON-DIVERGENCE pins it. The text below is kept for its history and its references.

### Stage Status

| Stage | Topic | Status |
|-------|-------|--------|
| 34a | Visibility Infrastructure | Superseded (Stage 110) |
| 34b | Visibility Rendering and Edge Cases | Superseded (Stage 110) |
| 35 | Visibility with Obstructing Surfaces | Superseded (Stage 110) |
| 36 | Visibility Predicts Non-Divergence (No Plan) | Superseded (Stage 110) |
| 37 | Visibility After Planned Reflection | Superseded (Stage 110) |
| 38 | Multi-Step Visibility | Superseded (Stage 110) |

**Regression Test Policy:** After implementing Stage N, run ALL tests from Stages 1 through N. The full test suite must pass before proceeding to Stage N+1. No exceptions.

**Feedback Loop Protocol (applies to every stage):**
1. AI implements the stage (code + unit tests).
2. AI runs all unit tests (new + all prior). All must pass.
3. User performs interactive tests from the checklist.
4. User provides feedback (pass/fail per item + notes on unexpected behavior).
5. AI converts any failing interactive tests into automated tests.
6. AI applies fixes and re-runs all tests.
7. Repeat steps 3--6 until all checks pass.
8. Stage is marked complete. Proceed to next stage.

**USER VALIDATION GATE (MANDATORY)**

No stage is complete until the user has personally verified every interactive test item and provided explicit sign-off. After every behavioral feature (not just infrastructure), the user must SEE the behavior working in the running game before the stage is marked complete.

1. User MUST test every interactive test item marked with `[ ]`.
2. User MUST provide written pass/fail feedback for EACH item.
3. Any "fail" or "unexpected behavior" becomes a NEW automated test before proceeding.
4. AI MUST NOT proceed to the next stage until ALL interactive items are `[x]`.
5. The user's word is final. If the user says it doesn't look right, it doesn't ship.

---

## Prior Art Summary (current state)

The following systems exist and are tested. This summary reflects the actual codebase as of v1.6.0, not the original TDD_05 plan.

- **Project skeleton:** Godot 4.6 project with GUT testing framework, directory structure (`scripts/math/`, `scripts/visual/`, `scripts/game/`, `tests/`).
- **Player:** `CharacterBody2D` with WASD movement, gravity-aware mode, jump.
- **Cursor:** World-space mouse tracking via `get_global_mouse_position()`.
- **Math layer:** `Direction`, `Ray`, `GeneralizedCircle` (with Hermitian carrier transform), `Segment` (three-point with carrier derivation, side determination, `full` flag for unbounded segments), `Point` (with provenance and unique ID), `TransformCache` (provenance-keyed bidirectional store).
- **Intersection system:** `intersect_line_with_carrier` (line-line and line-circle cases), segment bounds filtering, three-tier provenance endpoint detection (no epsilon), side determination at hit point.
- **Hit selection:** `HitRecord`, `find_all_hits` (forward/beyond partition, tie-breaking by surface ID, skip_segment exclusion).
- **Surface system:** `Surface` (segment + policy), `SideConfig` (effect + state_change + interactive flag).
- **Effect system (partial):**
  - `Effect` base class with `Kind` enum (`PASS`, `TERMINAL`, `TRANSFORMATIVE`, `PROJECTIVE`)
  - `TransformativeEffect` base class
  - `ReflectionEffect` -- mirrors ray across carrier line. Anti-conformal. Self-inverse.
  - `CircleInversionEffect` -- inverts ray through carrier circle. Anti-conformal. Self-inverse. Produces arc paths in visual frame.
  - `TerminalEffect` (block) -- stops the ray.
  - **Not yet implemented:** `RigidMotionEffect`, projective effects, `CompoundTransformativeEffect`
- **MobiusTransform:** Full implementation with conformal/anti-conformal composition, inversion, Hermitian carrier transformation.
- **Physical trace loop:** Stage-based hitpoint walk, up to 32 hits, pass-through handling, source-based cancellation on transform stack, no surface filtering. Frame updates for transformative effects.
- **Planning system:** Image chain method, transformative sub-chains. Plan construction/removal UI. **Not yet implemented:** projective break points, mixed chain algorithm.
- **Step tree:** Planned vs physical paths, divergence detection, 5 step types rendered.
- **Arrow flight:** Spacebar fires, game freeze, arrow animates at 1600 u/s along traced path (line and arc segments), skip via key.
- **Checkpoint system:** Save before shot, undo (Z), full reset (R), deep-copy game state.
- **Plan retention:** Plan preserved after shot, resolves to current surface state.
- **Arc rendering:** `draw_arc()` integration for circle inversion visual paths.
- **Invariant sweep:** 24 invariant checks across 10 test scenes, zero violations.

**Prerequisites for this document:** TDD_05 planned future stages (70--78) must be completed first — visibility requires rigid motion, projective effects, and compound effects.

---

## Stage 34a: Visibility Infrastructure

### Overview
Build the visibility computation infrastructure: collect points of interest from surface endpoints, sort them radially using cross-product signs (not atan2), and construct simple visibility regions. Introduces the `see_through` parameter on `find_all_hits` (§11.6), distinct from `origin_on_seg` / `skip_segment`.

### Prerequisites
Stage 78 (all advanced effects -- visibility must handle arc carriers from circle inversion, transformed surfaces from rigid motion, and frame resets from projective effects).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Script | `scripts/math/visibility.gd` -- visibility computation | §15.1, §15.2, §15.3 |
| Behavior | Points of interest: all surface endpoints (start, end) -- no tangent points yet | §15.2 |
| Behavior | Cast rays toward each POI using `find_all_hits` | §15.2 |
| Behavior | Radial ordering: cross-product sign against fixed reference direction (not atan2) | §15.5, §31.2 |
| Function | `build_regions_from_casts()` -> Array[GeneralizedPolygon] | §15.3 |
| Behavior | Full 360 degree cast from player position (iteration 0 of §15.2 loop) | §15.2 |
| Behavior | Visibility casts use same `find_all_hits` pipeline as physical tracing | Principle 14, §15.6 |
| Parameter | `find_all_hits` gains `see_through: Set[Segment]` parameter -- segments in this set are transparent (rays pass through without recording a hit). Distinct from `origin_on_seg` / `skip_segment`. | §11.6 |

### Overview Note

The visibility system requires a `see_through` parameter on `find_all_hits` (§11.6), distinct from `origin_on_seg` / `skip_segment`. `see_through` segments are treated as transparent; `origin_on_seg` handles the departure segment. Both can be active simultaneously.

### Unit Tests Added

1. **`test_stage34a_empty_room_full_visibility`**: Player in an empty room (only 4 wall surfaces). Expected: visibility covers the entire room interior. One region spanning the full space.
2. **`test_stage34a_single_block_creates_shadow`**: Player at (100, 300). A short block surface at (300, 250)--(300, 350). Expected: visibility has a shadow wedge behind the block (relative to the player). The region does NOT extend into the shadow.
3. **`test_stage34a_radial_ordering_cross_product`**: Generate 8 POIs at cardinal and diagonal directions. Sort by radial ordering. Expected: sorted CCW (or CW, consistently) using cross-product sign -- NOT atan2. Validates: §15.5, §31.2.
4. **`test_stage34a_build_regions_produces_polygons`**: Cast toward known POIs. Build regions. Expected: result is Array[GeneralizedPolygon] with at least one region. Each region has >= 3 vertices.
5. **`test_stage34a_S13_no_self_intersection`**: Build visibility regions for several test scenes. For each region, verify no edge crosses another edge of the same region. Invariant validated: S13.
6. **`test_stage34a_S14_edges_on_geometry`**: For each edge of each visibility region, verify it lies on either a surface carrier or a ray from the player origin. Invariant validated: S14.
7. **`test_stage34a_S15_non_overlapping`**: Build visibility regions. For each pair of regions, verify they do not overlap (sample interior points of one region, check they are not inside the other). Invariant validated: S15.
8. **`test_stage34a_visibility_uses_same_intersection`**: Verify that visibility casts use `find_all_hits` -- the same function as physical tracing. (Implementation test: mock or trace call to confirm shared pipeline.) Validates: Principle 14, §15.6.
9. **`test_stage34a_see_through_parameter`**: Call `find_all_hits` with `see_through={surface_A}`. Ray would hit A first. Expected: A skipped, next surface returned. Validates: §11.6.
10. **`test_stage34a_see_through_distinct_from_origin`**: Set both `see_through={A}` and `origin_on_seg=B`. Ray hits A then B then C. Expected: C returned. Validates: §11.6 distinctness.
11. **`test_stage34a_build_regions_exact_vertices`**: Feed `build_regions_from_casts` a hand-constructed array of 6 CastResults with known obstruction sides and hit points. Verify the output polygon has the exact expected vertices (coordinate comparison, not just property tests). Validates: region builder produces correct geometry.
12. **`test_stage34a_three_pois_same_angle`**: Three surface endpoints are collinear with the player (all at the same radial angle from the origin). Expected: radial ordering handles them via provenance/surface ID tie-breaking. No sorting instability, no crash. Validates: n>2 radial ordering edge case.

### Interactive User Tests

Minimal visual change -- visibility rendering comes in Stage 34b. Run GUT to verify infrastructure tests pass.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S13 | Visibility no self-intersection: region boundaries do not cross themselves | Unit test: edge intersection check | Yes |
| S14 | Visibility edges on geometry: every edge lies on a surface carrier or a ray from origin | Unit test: edge-on-geometry check | Yes |
| S15 | Visibility non-overlapping: distinct regions do not overlap | Unit test: sample point containment | Yes |
| S16 | No NaN/Inf in output | Unit test: visibility vertices are finite | Inherited |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| **Math** | Stages 4--12 | Math primitives, intersection | Run GUT tests |
| **Math** | Stages 13--20 | Surfaces, effects, trace, MobiusTransform | Run GUT tests |
| **Math** | Stages 40--51 | Advanced effects (circle inversion, rigid motion, projective, compound) | Run GUT tests |
| **Visual** | Stages 5, 21--30, 41 | Preview rendering (line + arc) | Observe |
| **Interaction** | Stages 31--33 | Arrow flight, checkpoint, plan retention | Fire, undo, observe |

### Expected Visual State

No visual change from Stage 51. The visibility infrastructure is tested via GUT unit tests only. Rendering is introduced in Stage 34b.

### Feedback Loop Protocol

See standard protocol at top of document.

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | N/A -- infrastructure only, no behavioral change |
| Failing interactive tests automated | [ ] | N/A |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/visibility.gd` | Create | Visibility computation: POI collection, ray casting, radial ordering, region building |
| `tests/test_stage34a_visibility_infra.gd` | Create | Visibility infrastructure unit tests |

---

## Stage 34b: Visibility Rendering and Edge Cases

### Overview
Complete the visibility system with obstruction determination (CW/CCW/both-sided), multi-region construction, white semi-transparent rendering, and adversarial edge cases. The visibility polygon is now visible to the user.

### Prerequisites
Stage 34a (visibility infrastructure -- POI collection, radial ordering, and basic region building must be in place before adding obstruction determination, rendering, and edge-case hardening).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Script | `scripts/visual/visibility_renderer.gd` -- renders visibility regions | §22.2, §20.4 |
| Behavior | Obstruction side determination (CW/CCW/both) | §15.5 |
| Behavior | Multi-region construction from complex obstruction patterns | §15.3 |
| Visual | White semi-transparent fill for visibility regions | §22.2 |
| Constant | z_index=10 for visibility layer (between surfaces at 0 and preview at 20) | §20.4 |

### Unit Tests Added

1. **`test_stage34b_obstruction_endpoint_one_sided`**: A surface endpoint creates a one-sided obstruction (CW or CCW, not both). Validates: §15.5.
2. **`test_stage34b_obstruction_interior_both_sided`**: A ray hitting the interior of a surface (not at an endpoint) creates a both-sided obstruction. Validates: §15.5.
3. **`test_stage34b_z_index_correct`**: Visibility renderer z_index == 10. Surface nodes z_index == 0. Path renderer z_index == 20. Validates: §20.4.
4. **`test_stage34b_radial_ordering_180_degrees`**: Two POIs at exactly 180 degrees from each other relative to the origin. Verify they are ordered consistently (cross-product sign is 0 at 180 degrees -- must fall back to tie-breaking by provenance). Validates: edge case in radial sorting.
5. **`test_stage34b_radial_ordering_close_angular`**: Two POIs separated by < 0.01 radians. Verify they are ordered correctly and don't swap due to floating-point noise. Validates: numerical stability of angular sorting.
6. **`test_stage34b_filter_to_cone_wide`**: A truncating cone that spans > 180 degrees (possible with a wide mirror). Verify POIs inside the wide cone are kept and those outside are filtered. Validates: `filter_to_cone` with major-arc cones.
7. **`test_stage34b_intersect_regions_split_surface`**: A surface partially obstructed by two separate blockers, creating two disjoint lit sub-segments. Verify `intersect_regions_with_surface` returns both lit sub-segments. Validates: multi-segment illumination.
8. **`test_stage34b_passthrough_transparent_to_visibility`**: Pass-through surface (null effect, interactive=false) between player and a block surface. Compute visibility. Expected: visibility region is bounded by the block surface, NOT by the pass-through. The pass-through does not cast a shadow. Validates: §15.6 visibility consistency -- pass-through surfaces don't obstruct visibility.
9. **`test_stage34b_see_through_and_origin_interaction`**: Ray starts on segment A (`origin_on_seg=A`). Segment B is in `see_through`. Segment C is `skip_segment`. Segment D is ahead. Expected: ray handles A (origin segment), passes through B (see-through), skips C (skip_segment), hits D. All three exclusion mechanisms work simultaneously. Validates: §11.4 + §11.6 interaction.
10. **`test_stage34b_visibility_perf_smoke`**: Compute visibility for a scene with 8 surfaces. Measure time via `Time.get_ticks_usec()`. Expected: < 20ms. This is an early warning signal, not a hard pass/fail. Log the value. Validates: visibility architecture is viable before more complexity is added.
11. **`test_stage34b_build_regions_both_sided_obstruction`**: CastResult at a mid-segment hit (both-sided obstruction). Expected: region breaks at this point -- two separate regions, not one continuous region. Validates: both-sided obstruction splits regions correctly.
12. **`test_stage34b_build_regions_enclosed_player`**: Player completely surrounded by surfaces (no gaps). Expected: zero visibility regions (or a single degenerate region). Validates: enclosed-player edge case.
13. **`test_stage34b_build_regions_adjacent_endpoints`**: Two surfaces share an endpoint. CastResults at the shared point. Expected: regions merge correctly at shared endpoints, no gap between them. Validates: region continuity at shared geometry.
14. **`test_stage34b_build_regions_wrap_around_reference`**: Cast results span 360 degrees (full rotation around origin). The region wraps past the reference direction. Expected: a single region encompassing the full circle (no gap at the reference boundary). Validates: wrap-around in radial ordering.
15. **`test_stage34b_build_regions_four_plus_regions`**: Three surfaces creating interlocking shadows that split visibility into 4+ disjoint regions. Expected: all regions are distinct, non-overlapping, and cover the correct angular spans. Validates: complex multi-region construction.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

- [ ] Press Play. A white semi-transparent region fills the room interior (with no internal surfaces, the entire room is visible).
- [ ] Add a block surface inside the room. Observe a shadow region behind the block (from the player's perspective). The shadow is NOT filled with white.
- [ ] Move the player with WASD. The visibility polygon updates in real time as the player moves -- shadow boundaries shift.
- [ ] The visibility region is drawn BEHIND the preview lines (preview lines are on top of the white fill).
- [ ] The visibility region is drawn IN FRONT of the surfaces (surfaces are visible through the semi-transparent fill).
- [ ] With multiple block surfaces, multiple shadow regions appear. The visibility polygon correctly shows the union of visible areas.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S13 | Visibility no self-intersection: region boundaries do not cross themselves | Unit test: edge intersection check | Reinforced |
| S14 | Visibility edges on geometry: every edge lies on a surface carrier or a ray from origin | Unit test: edge-on-geometry check | Reinforced |
| S15 | Visibility non-overlapping: distinct regions do not overlap | Unit test: sample point containment | Reinforced |
| S16 | No NaN/Inf in output | Unit test: visibility vertices are finite | Inherited |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| **Math** | Stages 4--12 | Math primitives, intersection | Run GUT tests |
| **Math** | Stages 13--20 | Surfaces, effects, trace, MobiusTransform | Run GUT tests |
| **Math** | Stages 40--51 | Advanced effects | Run GUT tests |
| **Math** | Stage 34a | Visibility infrastructure (POI, radial ordering, basic regions) | Run GUT tests |
| **Visual** | Stages 5, 21--30, 41 | Preview rendering (line + arc) | Observe |
| **Interaction** | Stages 31--33 | Arrow flight, checkpoint, plan retention | Fire, undo, observe |

### Expected Visual State

The room has a white semi-transparent fill in all areas visible from the player's position. Shadow regions behind block surfaces are unfilled (dark background shows through). The preview lines draw on top of the visibility fill. Surfaces draw below the visibility fill but are visible through the semi-transparency. As the player moves, the visibility polygon updates in real time.

### Feedback Loop Protocol

See standard protocol at top of document.

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/visual/visibility_renderer.gd` | Create | Renders visibility regions as white semi-transparent filled polygons |
| `scripts/math/visibility.gd` | Modify | Add obstruction determination, multi-region construction, edge-case handling |
| `scenes/main.tscn` | Modify | Add visibility renderer node at z_index=10 |
| `tests/test_stage34b_visibility_render.gd` | Create | Visibility rendering and edge case unit tests |

---

## Stage 35: Visibility with Obstructing Surfaces

### Overview
Refine obstruction side determination for scenes with multiple surfaces. Surface endpoints create one-sided obstructions (CW or CCW depending on surface orientation), while mid-segment interior hits create both-sided obstructions. Verify that shadow regions are computed correctly for multiple surfaces creating separate shadows, overlapping shadows, and wedge shadows between the player and walls.

### Prerequisites
Stage 34b (visibility rendering and edge cases -- the full visibility system with rendering must be in place before refining multi-surface obstruction).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Behavior | Refined obstruction side determination for multiple surfaces | §15.3, §15.5 |
| Behavior | Surface endpoint one-sided obstruction (CW or CCW based on surface orientation) | §15.5 |
| Behavior | Mid-segment interior hit creates both-sided obstruction | §15.5 |
| Behavior | Shadow wedge from single block between player and wall | §15.3 |
| Behavior | Multiple surfaces create separate shadow regions | §15.3 |
| Behavior | Shadow edges align with surface geometry | §15.5 |

### Unit Tests Added

1. **`test_stage35_two_blocks_separate_shadows`**: Player at center. Two block surfaces at different angles. Expected: two distinct shadow regions, each behind its respective block. Regions do not merge unless geometrically adjacent.
2. **`test_stage35_wedge_shadow_between_player_and_wall`**: Player at (100, 300). Block surface at (300, 200)--(300, 400). Room wall at x=500. Expected: a wedge-shaped shadow behind the block, expanding from the block's edges to the far wall.
3. **`test_stage35_endpoint_obstruction_cw`**: A surface oriented so its endpoint creates a CW obstruction. Verify the cast result records CW at that endpoint. Validates: §15.5.
4. **`test_stage35_endpoint_obstruction_ccw`**: A surface oriented so its endpoint creates a CCW obstruction. Verify the cast result records CCW. Validates: §15.5.
5. **`test_stage35_interior_hit_both_sided`**: Cast a ray that hits a surface in the middle (not at an endpoint). Expected: obstruction == BOTH. Validates: §15.5.
6. **`test_stage35_adjacent_surfaces_shared_endpoint`**: Two surfaces meeting at a shared endpoint (corner). Expected: the corner obstructs both CW and CCW (one from each surface). The visibility correctly handles the corner without gaps or overlaps.
7. **`test_stage35_shadow_edges_align_with_surface`**: For a block surface with known geometry, the shadow boundary rays pass through the surface's endpoints. The shadow edges are collinear with player-to-endpoint rays. Validates: shadow edge alignment.
8. **`test_stage35_S13_multiple_surfaces`**: With 3+ block surfaces creating complex shadows, visibility regions still have no self-intersection. Invariant validated: S13.
9. **`test_stage35_S15_multiple_surfaces`**: With complex shadow geometry, distinct visibility regions do not overlap. Invariant validated: S15.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

- [ ] Place two block surfaces at different positions. Move the player. Observe two distinct shadow regions, each cast by its respective block.
- [ ] Place a block surface between the player and the far wall. Observe the wedge-shaped shadow expanding behind the block.
- [ ] Place two surfaces end-to-end forming an L-shape. The shadow behind the L is correctly computed with no gaps at the corner.
- [ ] Move the player around the room. Shadows update continuously. No visual artifacts (flickering, incorrect shadow edges, shadow leaking through surfaces).
- [ ] Place surfaces close together. Shadows merge or separate correctly as the player moves.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S13 | Visibility no self-intersection | Unit test: multiple surfaces | Reinforced |
| S14 | Visibility edges on geometry | Unit test: edge check with multiple surfaces | Reinforced |
| S15 | Visibility non-overlapping | Unit test: multiple surfaces, overlap check | Reinforced |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| **Math** | Stages 4--12 | Math primitives, intersection | Run GUT tests |
| **Math** | Stages 13--20 | Surfaces, effects, trace, MobiusTransform | Run GUT tests |
| **Math** | Stages 40--51 | Advanced effects | Run GUT tests |
| **Math** | Stages 34a--34b | Visibility infrastructure, rendering, edge cases | Run GUT tests |
| **Visual** | Stages 5, 21--30, 41 | Preview rendering (line + arc) | Observe |
| **Interaction** | Stages 31--33 | Arrow flight, checkpoint, plan retention | Fire, undo, observe |

### Expected Visual State

Room with multiple block surfaces. White semi-transparent visibility fill shows all areas the player can see. Each block casts a shadow wedge away from the player. Shadow edges pass through block endpoints. Moving the player causes all shadows to update smoothly. No artifacts at surface corners or near surface boundaries.

### Feedback Loop Protocol

See standard protocol at top of document.

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/visibility.gd` | Modify | Refined obstruction side determination, multi-surface shadow computation |
| `tests/test_stage35_visibility_obstruction.gd` | Create | Multi-surface obstruction and shadow tests |

---

## Stage 36: Visibility Predicts Non-Divergence (No Plan)

### Overview
With an empty plan, the visibility region shows everywhere a straight-line shot reaches without obstruction. This stage validates the critical UX invariants: cursor inside the visibility region guarantees no divergence (UX1), and divergence implies the cursor is outside the visibility region (UX2). A sweep test across a grid of player x cursor positions (e.g., 10x10 x 10x10 = 10,000 combinations) verifies these invariants exhaustively.

### Prerequisites
Stage 35 (visibility with obstructing surfaces -- shadows must be correct for the invariant to hold).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Invariant test | UX1: cursor inside visibility -> no divergence | §15.1, §29.3 |
| Invariant test | UX2: divergence -> cursor outside visibility | §15.1, §29.3 |
| Test infrastructure | Grid sweep: player positions x cursor positions | §29.3 |

### Unit Tests Added

1. **`test_stage36_UX1_cursor_in_visibility_no_divergence`**: For a scene with block surfaces, sweep a 10x10 grid of player positions x 10x10 grid of cursor positions (10,000 combinations). For each combination: compute visibility regions (empty plan). If cursor is inside any visibility region, compute step tree (empty plan). Expected: step tree has no divergence (divergence_index == null). Invariant validated: UX1.
2. **`test_stage36_UX2_divergence_implies_outside_visibility`**: Same grid sweep. For each combination: compute step tree (empty plan). If divergence_index != null, check cursor is NOT inside any visibility region. Expected: all divergence cases have cursor outside visibility. Invariant validated: UX2.
3. **`test_stage36_empty_plan_visibility_is_straight_line_reach`**: Player at (100, 300). Block surface at (300, 200)--(300, 400). Cursor at (200, 300) (in front of block). Expected: cursor is inside visibility, step tree has no divergence (straight shot reaches cursor). Cursor at (400, 300) (behind block). Expected: cursor is outside visibility (block obstructs).
4. **`test_stage36_visibility_boundary_precision`**: Cursor placed exactly on the boundary of a visibility region (on a shadow edge). Expected: the system does not crash; the result is deterministic (either inside or outside, consistently). Validates: stability at boundaries.
5. **`test_stage36_multiple_scenes_sweep`**: Run the grid sweep on at least 3 different test scenes (empty room, single block, two blocks at different angles). All 3 scenes must satisfy UX1 and UX2 for all grid positions.

### Interactive User Tests

- [ ] With empty plan, move the cursor inside the white visibility region. The preview shows only green lines (no divergence).
- [ ] Move the cursor into a shadow region (behind a block). The preview shows divergence: green up to the block, then yellow (physical blocked) and/or red (planned continuation).
- [ ] Move the cursor along the edge of a shadow. At the boundary, the preview transitions cleanly between aligned and diverged states.
- [ ] Move the player to different positions. The invariant holds at all player positions: cursor in white = no divergence, cursor in shadow = divergence.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| UX1 | Visibility predicts non-divergence: cursor in visibility -> planned == physical | Grid sweep: 10,000+ combinations | Yes |
| UX2 | Divergence implies outside visibility | Grid sweep: contrapositive of UX1 | Yes |
| S13 | Visibility no self-intersection | Inherited | Inherited |
| S14 | Visibility edges on geometry | Inherited | Inherited |
| S15 | Visibility non-overlapping | Inherited | Inherited |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| **Math** | Stages 4--12 | Math primitives, intersection | Run GUT tests |
| **Math** | Stages 13--20 | Surfaces, effects, trace, MobiusTransform | Run GUT tests |
| **Math** | Stages 40--51 | Advanced effects | Run GUT tests |
| **Math** | Stages 34a--35 | Visibility infrastructure, rendering, obstructions | Run GUT tests |
| **Visual** | Stages 5, 21--30, 41 | Preview rendering (line + arc) | Observe |
| **Interaction** | Stages 31--33 | Arrow flight, checkpoint, plan retention | Fire, undo, observe |

### Expected Visual State

Same as Stage 35. The visual appearance does not change -- the validation is in the invariant tests, not new visuals. The white visibility region continues to accurately represent where the cursor can be placed without causing divergence.

### Feedback Loop Protocol

See standard protocol at top of document.

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `tests/test_stage36_visibility_invariants.gd` | Create | UX1/UX2 grid sweep tests |
| `scripts/math/visibility.gd` | Modify | Add point-in-region query for invariant testing |

---

## Stage 37: Visibility After Planned Reflection

### Overview
Implement visibility iteration 1 from the §15.2 loop: after a planned reflective surface. The visibility region transforms through the planned surface's Mobius transform, with the origin moving to the inverse-transformed position and the truncating segments set to the lit (illuminated) sub-segment of the planned surface. This narrows the visibility cone to only directions reachable through the planned reflection.

### Prerequisites
Stage 36 (visibility predicts non-divergence for empty plan -- the base iteration must be correct before adding plan iterations), Stage 20 (full MobiusTransform -- needed for frame composition and origin transformation).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Behavior | Visibility iteration 1: after a single planned reflective surface | §15.2 |
| Behavior | Transformative effect propagation: `M = current_frame.compose(effect.get_mobius())` | §15.2 |
| Behavior | Origin transformation: `current_origin = cache.apply(current_origin, M.inverse())` | §15.2 |
| Behavior | Truncating segments: `lit_segments = intersect_regions_with_surface(regions, planned_surface)` | §15.2 |
| Function | `intersect_regions_with_surface(regions, surface)` -- find illuminated sub-segment | §15.3 |
| Function | `filter_to_cone(points, origin, truncating_segments)` -- keep POIs within angular span | §15.3 |
| Behavior | Transform all surfaces into current frame for iteration 1 | §15.2 |

### Unit Tests Added

1. **`test_stage37_lit_segments_computation`**: Player at (100, 300). Mirror at (300, 200)--(300, 400). No obstructions between player and mirror. Expected: `intersect_regions_with_surface` returns the full mirror segment as lit. Validates: §15.3.
2. **`test_stage37_lit_segments_partial_obstruction`**: Player at (100, 300). Block partially obstructing the mirror. Expected: only the unobstructed portion of the mirror is lit.
3. **`test_stage37_filter_to_cone`**: Given truncating segments defining a narrow cone. Generate POIs inside and outside the cone. Expected: `filter_to_cone` keeps only POIs inside. Validates: §15.3.
4. **`test_stage37_visibility_after_reflection_origin_transformed`**: Player at (100, 300). Plan: [{mirror at x=300, LEFT}]. Expected: after iteration 1, the effective origin for the visibility cast is the reflection of the player through the mirror (at (500, 300)). Validates: §15.2 origin transform.
5. **`test_stage37_visibility_after_reflection_narrowed`**: Same setup. Expected: visibility region after reflection is narrower than the full 360-degree region -- it only covers angles reachable through the lit portion of the mirror.
6. **`test_stage37_UX1_with_single_plan`**: Grid sweep (5x5 player x 5x5 cursor = 625 combinations) with plan [{mirror, LEFT}]. For each combination: if cursor is inside the post-reflection visibility region, verify no divergence in the step tree. Invariant validated: UX1 (now with plan).
7. **`test_stage37_UX2_with_single_plan`**: Same sweep. If step tree diverges, verify cursor is outside the post-reflection visibility region. Invariant validated: UX2 (now with plan).
8. **`test_stage37_visibility_cone_matches_mirror_span`**: The visibility cone after reflection spans exactly the angular range covered by the lit portion of the mirror (as seen from the reflected origin). Validates: truncating segment correctness.
9. **`test_stage37_frame_composition_correct`**: After planning a reflection, the current_frame in the visibility computation matches `IDENTITY.compose(reflection_mobius)`. Validates: §15.2 frame update.
10. **`test_stage37_filter_to_cone_rejects_outside_poi`**: Construct a truncating cone (from a planned reflection) and a POI that is just outside the cone boundary (by 0.01 radians). Expected: `filter_to_cone` correctly rejects this POI. If it incorrectly includes it, the visibility region would be too large, potentially causing UX1 violations. Validates: `filter_to_cone` precision at angular boundaries.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

- [ ] Add a reflective surface to the plan. Observe the visibility region changes: it now shows a reflected region beyond the mirror, representing where the cursor can be placed for a valid reflected shot.
- [ ] The visibility region beyond the mirror is narrower than the full room -- it only covers angles reachable through the mirror.
- [ ] Move the cursor into the reflected visibility region. The preview shows a green aligned path bouncing off the mirror.
- [ ] Move the cursor outside the reflected visibility region (but in a previously visible area). The preview shows divergence.
- [ ] Move the player. The reflected visibility region updates in real time.
- [ ] If a block partially obstructs the mirror from the player's view, the reflected visibility region is smaller (only the visible portion of the mirror contributes).

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| UX1 | Visibility predicts non-divergence (now with plan) | Grid sweep with single plan entry | Reinforced |
| UX2 | Divergence implies outside visibility (now with plan) | Grid sweep with single plan entry | Reinforced |
| S13 | Visibility no self-intersection | Unit test | Inherited |
| S14 | Visibility edges on geometry | Unit test | Inherited |
| S15 | Visibility non-overlapping | Unit test | Inherited |
| S2 | Transform round-trip | Inherited from Stage 20 | Inherited |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| **Math** | Stages 4--12 | Math primitives, intersection | Run GUT tests |
| **Math** | Stages 13--20 | Surfaces, effects, trace, MobiusTransform | Run GUT tests |
| **Math** | Stages 40--51 | Advanced effects | Run GUT tests |
| **Math** | Stages 34a--36 | Visibility infrastructure, rendering, obstructions, UX1/UX2 invariants | Run GUT tests |
| **Visual** | Stages 5, 21--30, 41 | Preview rendering (line + arc) | Observe |
| **Interaction** | Stages 31--33 | Arrow flight, checkpoint, plan retention | Fire, undo, observe |

### Expected Visual State

With a plan containing one reflective surface: the visibility region shows both the direct visibility (areas the player can see without obstruction) and the reflected visibility (areas reachable through the planned mirror). The reflected region appears as a cone of white fill extending from the mirror into the reflected space. Shadows from surfaces on the "other side" of the mirror appear correctly in the reflected visibility.

### Feedback Loop Protocol

See standard protocol at top of document.

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/visibility.gd` | Modify | Add iteration 1 logic: frame composition, origin transform, truncating segments, filter_to_cone |
| `scripts/visual/visibility_renderer.gd` | Modify | Render multi-iteration visibility regions |
| `tests/test_stage37_visibility_reflection.gd` | Create | Visibility after planned reflection tests |

---

## Stage 38: Multi-Step Visibility

### Overview
Extend the visibility loop to iterate for each planned surface (the full §15.2 main loop). Each iteration transforms surfaces into the current frame, collects POIs (including truncating segment endpoints), filters to the cone, casts, and builds regions. Multi-bounce visibility regions narrow as more surfaces are planned. A terminal effect in the plan results in empty regions (no valid aim positions).

### Prerequisites
Stage 37 (visibility after single planned reflection -- the iteration mechanism must work for one step before extending to N steps).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Behavior | Visibility loop iterates for each planned surface (full §15.2 main loop) | §15.2 |
| Behavior | Each iteration: transform surfaces, collect POIs (including truncating segment endpoints), filter to cone, cast, build regions | §15.2 |
| Behavior | Multi-bounce visibility regions narrow as more surfaces are planned | §15.2 |
| Behavior | Terminal effect in plan: `regions = []` (no valid aim positions) | §15.2 |

### Unit Tests Added

1. **`test_stage38_two_mirror_visibility`**: Player at (50, 300). Plan: [{mirror_A at x=200, LEFT}, {mirror_B at y=100, RIGHT}] (from §16.1). Expected: visibility region after 2 iterations is narrower than after 1 iteration. The region represents valid cursor positions for the two-bounce shot.
2. **`test_stage38_three_mirror_visibility`**: Plan with 3 reflective surfaces. Expected: visibility region narrows with each additional planned surface. Region after 3 iterations is a subset of region after 2 iterations.
3. **`test_stage38_terminal_in_plan_empty_regions`**: Plan: [{mirror, LEFT}, {block_surface, LEFT (interactive)}]. Expected: `regions == []` after processing the terminal entry. No valid aim positions because the arrow will stop at the block. Validates: §15.2 terminal handling.
4. **`test_stage38_UX1_multi_step`**: Grid sweep (5x5 x 5x5 = 625 combinations) with a 2-mirror plan. For each combination: if cursor is inside multi-step visibility, verify no divergence. Invariant validated: UX1 (multi-step).
5. **`test_stage38_UX2_multi_step`**: Same sweep. If divergence, cursor must be outside multi-step visibility. Invariant validated: UX2 (multi-step).
6. **`test_stage38_S13_multi_step`**: Multi-step visibility regions have no self-intersecting boundaries. Invariant validated: S13.
7. **`test_stage38_S14_multi_step`**: Multi-step visibility region edges lie on surface carriers or rays from the (transformed) origin. Invariant validated: S14.
8. **`test_stage38_S15_multi_step`**: Multi-step visibility regions do not overlap. Invariant validated: S15.
9. **`test_stage38_each_iteration_uses_updated_frame`**: After each planned surface, the frame is updated via `compose`. Verify that iteration 2 uses the composed frame from iteration 1, not the identity. Validates: §15.2 frame propagation.
10. **`test_stage38_truncating_segments_from_prior_iteration`**: After iteration 1, the truncating segments are the lit portion of planned surface 1. Iteration 2 uses these as its truncating boundary. Verify that POIs outside the truncating cone are filtered out.
11. **`test_stage38_unreachable_entry_skipped`**: Plan with an unreachable entry (geometrically unreachable). The visibility loop skips the unreachable entry and continues with the next. Validates: unreachable entry handling in visibility.
12. **`test_stage38_empty_plan_falls_through`**: Plan is empty. Expected: visibility loop runs only iteration 0 (same as Stage 34a/36 results). Validates: backwards compatibility.
13. **`test_stage38_multi_step_visibility_perf_smoke`**: Compute multi-step visibility (2 planned reflections) with 8 surfaces. Measure time. Expected: < 30ms. Validates: multi-step visibility doesn't blow up the per-frame budget.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

- [ ] Create a plan with 2 reflective surfaces. The visibility region shows where the cursor can be placed for a valid 2-bounce shot. The region is visibly smaller than with a 1-surface plan.
- [ ] Add a third reflective surface to the plan. The visibility region narrows further.
- [ ] Remove surfaces from the plan (right-click). The visibility region expands as fewer constraints apply.
- [ ] Add a block surface to the plan (if interactive). The visibility region becomes empty (no white fill anywhere) -- there are no valid aim positions because the arrow will be blocked.
- [ ] Move the cursor within the multi-step visibility region. Preview shows full green alignment through all planned bounces.
- [ ] Move the cursor outside the multi-step visibility region. Preview shows divergence at some point in the chain.
- [ ] Clear the plan (C). Visibility returns to the full empty-plan polygon (Stage 34b behavior).
- [ ] With a complex plan, move the player. Visibility updates smoothly in real time.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| UX1 | Visibility predicts non-divergence (multi-step plan) | Grid sweep with multi-surface plan | Reinforced |
| UX2 | Divergence implies outside visibility (multi-step plan) | Grid sweep with multi-surface plan | Reinforced |
| S13 | Visibility no self-intersection (multi-step regions) | Unit test | Reinforced |
| S14 | Visibility edges on geometry (multi-step) | Unit test | Reinforced |
| S15 | Visibility non-overlapping (multi-step) | Unit test | Reinforced |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| **Math** | Stages 4--12 | Math primitives, intersection | Run GUT tests |
| **Math** | Stages 13--20 | Surfaces, effects, trace, MobiusTransform | Run GUT tests |
| **Math** | Stages 40--51 | Advanced effects | Run GUT tests |
| **Math** | Stages 34a--36 | Visibility infrastructure, rendering, obstructions, UX1/UX2 (empty plan) | Run GUT tests |
| **Math** | Stage 37 | Visibility after single planned reflection | Run GUT tests |
| **Visual** | Stages 5, 21--30, 41 | Preview rendering with all step types (line + arc) | Observe |
| **Interaction** | Stages 31--33 | Arrow flight, checkpoint, plan retention | Fire, undo, observe |
| **Interaction** | Stages 25--26 | Plan construction/removal | Click surfaces |

### Expected Visual State

With a multi-surface plan, the visibility region is a narrow cone or wedge representing the valid cursor positions for the entire planned shot sequence. The region narrows as more surfaces are added to the plan. With a terminal surface in the plan, no visibility region is shown (empty). The visibility region updates in real time as the player moves, the cursor moves, or the plan changes. All shadow boundaries are clean, with no artifacts.

### Feedback Loop Protocol

See standard protocol at top of document.

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/visibility.gd` | Modify | Full multi-iteration visibility loop (§15.2 main loop) |
| `scripts/visual/visibility_renderer.gd` | Modify | Render narrowing multi-step visibility regions |
| `tests/test_stage38_visibility_multistep.gd` | Create | Multi-step visibility loop tests |

---

## Appendix A: Invariant Introduction Map (All 29 Invariants, Status After Stage 38)

| Invariant | Full ID | Introduced | First Testable | Fully Testable | Status After Stage 38 |
|-----------|---------|-----------|----------------|----------------|----------------------|
| Carrier <-> via round-trip | S1 | Stage 8 | Stage 8 | Stage 65 | Tested (line + circle) |
| Transform round-trip | S2 | Stage 20 | Stage 20 | Stage 65 | Tested (rotation, reflection, inversion, rigid motion) |
| Determinism | S3 | Stage 14 | Stage 14 | Stage 65 | Tested |
| Divergence monotonic | S4 | Stage 26 | Stage 26 | Stage 65 | Tested |
| Aligned provenance | S5 | Stage 25 | Stage 25 | Stage 65 | Tested |
| Aligned match | S6 | Stage 25 | Stage 25 | Stage 65 | Tested |
| Per-entry state | S7 | -- | -- | Stage 54+ | Not yet introduced |
| Forward-first ordering | S8 | Stage 11 | Stage 11 | Stage 65 | Tested |
| Exclusion respected | S9 | Stage 16 | Stage 16 | Stage 65 | Tested |
| Projective resets frame | S10 | Stage 73 | Stage 73 | Stage 65 | Tested |
| Three points on carrier | S11 | Stage 7 | Stage 7 | Stage 65 | Tested |
| Side determination | S12 | Stage 7 | Stage 7 | Stage 65 | Tested |
| Visibility no self-intersect | S13 | Stage 34a | Stage 34a | Stage 65 | Tested (infra, rendering/edge cases, multi-surface, multi-step) |
| Visibility edges on geometry | S14 | Stage 34a | Stage 34a | Stage 65 | Tested (infra, rendering/edge cases, multi-surface, multi-step) |
| Visibility non-overlapping | S15 | Stage 34a | Stage 34a | Stage 65 | Tested (infra, rendering/edge cases, multi-surface, multi-step) |
| No NaN/Inf in output | S16 | Stage 4 | Stage 4 | Stage 65 | Tested (Direction, Ray, Mobius, visibility) |
| Provenance IDs unique | S17 | Stage 8 | Stage 8 | Stage 65 | Tested (Points + Transform IDs) |
| Frame determinant non-zero | S18 | Stage 20 | Stage 20 | Stage 65 | Tested (construction + composition) |
| Trace preserves real state | S19 | -- | -- | Stage 53+ | Not yet introduced |
| Visibility predicts non-div. | UX1 | Stage 36 | Stage 36 | Stage 65 | Tested (empty plan + single plan + multi-step) |
| Divergence -> outside vis. | UX2 | Stage 36 | Stage 36 | Stage 65 | Tested (empty plan + single plan + multi-step) |
| Preview matches flight | UX3 | Stage 17 | Stage 17 | Stage 65 | Tested (with and without plan) |
| Same shot = same result | UX4 | Stage 17 | Stage 17 | Stage 65 | Tested (with and without plan) |
| Undo fully restores | UX5 | Stage 32 | Stage 32 | Stage 65 | Tested (position, velocity, state, plan, targets_hit) |
| All targets reachable | UX6 | -- | -- | Stage 55+ | Not yet introduced |
| Solid path to cursor | UX7 | Stage 5 (partial) | Stage 5 | Stage 65 | Tested (line + reflection + arc) |
| Block stops arrow | UX9 | Stage 13 | Stage 13 | Stage 65 | Tested |
| State changes visible | UX10 | -- | -- | Stage 57+ | Not yet introduced |
| Empty plan = fire straight | UX11 | Stage 5 (partial) | Stage 15 | Stage 65 | Tested |

---

## Appendix B: Cumulative Test Count After Stage 38

| Category | Count |
|----------|-------|
| Unit tests (Stages 1--33) | ~209 (estimated from prior documents) |
| Unit tests (Stages 40--51) | ~TBD (advanced effects) |
| Unit tests (Stage 34a) | 12 |
| Unit tests (Stage 34b) | 15 |
| Unit tests (Stage 35) | 9 |
| Unit tests (Stage 36) | 5 |
| Unit tests (Stage 37) | 10 |
| Unit tests (Stage 38) | 13 |
| **Total unit tests** | **~209 + TDD_05 count + 64** |
| Interactive test items (Stages 1--33) | ~105 (estimated) |
| Interactive test items (Stages 40--51) | ~TBD (advanced effects) |
| Interactive test items (Stages 34a--38) | 29 |
| **Total interactive test items** | **~105 + TDD_05 count + 29** |
| Invariants actively tested | 17 (S1--S6, S8--S18, UX5) |
| Invariants partially covered | 2 (UX7, UX11) |
| Invariants newly testable this document | 5 (S13, S14, S15, UX1, UX2) |
| Invariants not yet introduced | 4 (S7, S19, UX6, UX10) |
